#include "ConfigurationData.h"

#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"

#include "klee/ExecutionState.h"

#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"
#include "klee_headers/Common.h"

#include <tr1/unordered_set>
#include <tr1/unordered_map>
#include <sstream>

#include "net/util/debug.h"
#include "kexPPrinter.h"

#define DD net::DEBUG<net::debug::slack>

namespace kleenet {

  class SenderTxData { // linear-time construction (linear in packet length)
    friend class ConfigurationData;
    friend class PerReceiverData;
    private:
      std::string const designation;
      std::set<klee::Array const*> senderSymbols;
      ConfigurationData& cd;
      StateDistSymbols& distSymbolsSrc;
      typedef ConfigurationData::ConList ConList;
      ConList const seq;
      bool constraintsComputed;
      ConstraintsGraph::ConstraintList senderConstraints; // untranslated!
      bool senderReflexiveArraysComputed;
      bool allowMorePacketSymbols; // once this flipps to false operator[] will be forbidden to find additional symbols in the packet
    public:
      std::string const specialTxName;
    private:

      template <typename T, typename It, typename Op>
      static std::vector<T> transform(It begin, It end, unsigned size, Op const& op, T /* type inference */) { // rvo takes care of us :)
        std::vector<T> v(size);
        std::transform(begin,end,v.begin(),op);
        return v;
      }
    public:
      template <typename InputIterator>
      SenderTxData(ConfigurationData& cd, std::string designation, StateDistSymbols& distSymbolsSrc, InputIterator const& begin, InputIterator const& end)
        : designation(designation)
        , senderSymbols()
        , cd(cd)
        , distSymbolsSrc(distSymbolsSrc)
        , seq(begin,end)
        , constraintsComputed(false)
        , senderConstraints()
        , senderReflexiveArraysComputed(false)
        , allowMorePacketSymbols(true)
        , specialTxName(designation + "(node" + llvm::itostr(distSymbolsSrc.node.id) + ")")
        {
      }
      ConstraintsGraph::ConstraintList const& computeSenderConstraints(bool txConstraintsTransmission) {
        if (!constraintsComputed) {
          allowMorePacketSymbols = false;
          constraintsComputed = true;
          assert(senderConstraints.empty() && "Garbage data in our sender's constraints buffer.");
          std::set<klee::Array const*> symbols(senderSymbols);
          DD::cout << "| txConstraintsTransmission = " << txConstraintsTransmission << DD::endl;
          if (txConstraintsTransmission) {
            DD::cout << "| symbols before force-adding: " << symbols.size() << DD::endl;
            distSymbolsSrc.iterateArrays(
              net::util::FunctorBuilder<klee::Array const*,net::util::DynamicFunctor,net::util::IterateOperator>::build(
                std::inserter(symbols,symbols.end())
              )
            );
            DD::cout << "| symbols after force-adding: " << symbols.size() << DD::endl;
          }
          senderConstraints = cd.cg.eval(symbols);
        }
        return senderConstraints;
      }
  };

  template <typename BGraph, typename Key> class ExtractReadEdgesVisitor : public klee::ExprVisitor { // cheap construction (depends on Key copy-construction)
    private:
      BGraph& bg;
      Key key;
    protected:
      Action visitRead(klee::ReadExpr const& re) {
        bg.addUndirectedEdge(key,re.updates.root);
        return Action::skipChildren();
      }
    public:
      ExtractReadEdgesVisitor(BGraph& bg, Key key)
        : bg(bg)
        , key(key) {
      }
  };
}

using namespace kleenet;


ReadTransformator::Action ReadTransformator::visitRead(klee::ReadExpr const& re) {
  if (!llvm::isa<klee::ConstantExpr>(re.index)) {
    std::ostringstream errorBuf;
    errorBuf << "While parsing of read expression to '"
             << re.updates.root->name;
    //klee::ExprPPrinter::printSingleExpr(errorBuf,re);
    errorBuf << "'; Encountered symbolic index which is currently not supported for packet data. Please file this as a feature request.";
    klee::klee_error("%s",errorBuf.str().c_str());
  }
  klee::ref<klee::Expr> const replacement = klee::ReadExpr::alloc( // needs review
      klee::UpdateList(lst(re.updates.root),re.updates.head) // head is cow-shared: magic
    , re.index /* XXX this could backfire, if we have complicated READ expressions in the index
                * but for now we can simply assume not to have such weird stuff*/
  );
  return Action::changeTo(replacement);
}

ReadTransformator::ReadTransformator(NameMangler& mangler
                 , Seq const& seq
                 , LazySymbolTranslator::Symbols* preImageSymbols
                 , LazySymbolTranslator::Symbols* translatedSymbols
                 )
  : lst(mangler,preImageSymbols,translatedSymbols)
  , seq(seq)
  , dynamicLookup(seq.size(),klee::ref<klee::Expr>()) {
}

klee::ref<klee::Expr> const ReadTransformator::operator[](unsigned const index) {
  assert(dynamicLookup.size() && "Epsilon cannot be expanded into non-empty sequence.");
  unsigned const normIndex = index % dynamicLookup.size();
  klee::ref<klee::Expr>& slot = dynamicLookup[normIndex];
  if (slot.isNull())
    slot = visit(seq[normIndex]);
  return slot;
}
klee::ref<klee::Expr> const ReadTransformator::operator()(klee::ref<klee::Expr> const expr) {
  return visit(expr);
}
LazySymbolTranslator::TxMap const& ReadTransformator::symbolTable() const {
  return lst.symbolTable();
}


void ConstraintsGraph::updateGraph() {
  typedef klee::ConstraintManager::constraints_ty Vec;
  for (Vec::const_iterator it = cm.begin(), end = cm.end(); it != end; ++it) {
    ExtractReadEdgesVisitor<BGraph,Constraint>(bGraph,*it).visit(*it);
  }
}




PerReceiverData::PerReceiverData(SenderTxData& txData, ConfigurationData& receiverConfig, size_t const beginPrecomputeRange, size_t const endPrecomputeRange)
  : txData(txData)
  , receiverConfig(receiverConfig)
  , nmh(txData.designation,txData.distSymbolsSrc,receiverConfig.distSymbols)
  , rt(nmh.mangler,txData.seq,&(txData.senderSymbols))
  , constraintsComputed(false)
  , specialTxName(txData.specialTxName)
{
  size_t const existingPacketSymbols = txData.senderSymbols.size();
  for (size_t it = beginPrecomputeRange; it != endPrecomputeRange; ++it)
    rt[it];
  assert((txData.allowMorePacketSymbols || (existingPacketSymbols == txData.senderSymbols.size())) \
    && "When precomuting all transmission atoms, we found completely new symbols, but we already assumed we were done with that.");
}

bool PerReceiverData::isNonConstTransmission() const {
  return !(txData.senderSymbols.empty() && rt.symbolTable().empty());
}

klee::ref<klee::Expr> PerReceiverData::operator[](size_t index) {
  size_t const existingPacketSymbols = txData.senderSymbols.size();
  klee::ref<klee::Expr> expr = rt[index];
  assert((txData.allowMorePacketSymbols || (existingPacketSymbols == txData.senderSymbols.size())) \
    && "When translating an atom, we found completely new symbols, but we already assumed we were done with that.");
  return expr;
}

// result already translated!
void PerReceiverData::transferNewReceiverConstraints(net::util::DynamicFunctor<klee::ref<klee::Expr> > const& transfer, bool txConstraintsTransmission) {
  if (!constraintsComputed) {
    constraintsComputed = true;
    ConstraintsGraph::ConstraintList const& senderConstraints = txData.computeSenderConstraints(txConstraintsTransmission);
    for (ConstraintsGraph::ConstraintList::const_iterator it = senderConstraints.begin(), end = senderConstraints.end(); it != end; ++it) {
      DD::cout << "| " << "adding constraint ... " << DD::endl;
      DD::cout << "| "; pprint(DD(),rt(*it),"| ");
      transfer(rt(*it));
    }
  }
}
std::vector<std::pair<klee::Array const*,klee::Array const*> > PerReceiverData::additionalSenderOnlyConstraints() {
  std::vector<std::pair<klee::Array const*,klee::Array const*> > senderOnlyConstraints;
  if (!txData.senderReflexiveArraysComputed) {
    txData.senderReflexiveArraysComputed = true;
    txData.allowMorePacketSymbols = false;
    assert(senderOnlyConstraints.empty() && "Garbate data in our constraints buffer.");
    for (std::set<klee::Array const*>::const_iterator it = txData.senderSymbols.begin(), end = txData.senderSymbols.end(); it != end; ++it) {
      klee::Array const* const reflex = nmh.mangler.isReflexive(*it);
      if (reflex != *it) { // it's NOT reflexive, because it self-mangles to a different object
        senderOnlyConstraints.reserve(txData.senderSymbols.size());
        senderOnlyConstraints.push_back(std::make_pair(*it,reflex));
      }
    }
  }
  return senderOnlyConstraints;
}

PerReceiverData::NewSymbols PerReceiverData::newSymbols() { // rvo
  std::vector<std::pair<klee::Array const*,klee::Array const*> > senderOnlyConstraints = additionalSenderOnlyConstraints();
  NewSymbols result;
  result.reserve(senderOnlyConstraints.size()+rt.symbolTable().size());
  for (std::vector<std::pair<klee::Array const*,klee::Array const*> >::const_iterator it = senderOnlyConstraints.begin(), end = senderOnlyConstraints.end(); it != end; ++it) {
    if (it->first != it->second)
      result.push_back(GeneratedSymbolInformation(&(txData.distSymbolsSrc),it->first,it->second));
  }
  for (LazySymbolTranslator::TxMap::const_iterator it = rt.symbolTable().begin(), end = rt.symbolTable().end(); it != end; ++it) {
    if (it->first != it->second)
      result.push_back(GeneratedSymbolInformation(&(receiverConfig.distSymbols),it->first,it->second));
  }
  return result;
}

void PerReceiverData::GeneratedSymbolInformation::addArrayToStateNames(klee::ExecutionState& state, net::Node src, net::Node dest) const {
  if (!state.arrayNames.insert(translated->name).second && !belongsTo->isDistributed(translated)) {
    klee::klee_error("%s",(std::string("In transmission of symbol '") + was->name + "' from node " + llvm::itostr(src.id) + " to node " + llvm::itostr(dest.id) + "; Symbol '" + translated->name + "' already exists on the target state. This is either a bug in KleeNet or you used the symbol '}' when specifying the name of a symbolic object which is not supported in KleeNet as it is used to mark special distributed symbols.").c_str());
  }
}

ConfigurationData::ConfigurationData(klee::ExecutionState& state, net::Node src)
  : forState(state)
  , cg(state.constraints)
  , distSymbols(src)
  , txData(0)
  , merges(0)
  {
}
ConfigurationData::ConfigurationData(ConfigurationData const& from, klee::ExecutionState* state)
  : forState(*state)
  , cg(static_cast<klee::ExecutionState*>(state)->constraints) // XXX dangerous upcast because ES may not exist yet, but cg only stores the reference, so cross your fingers XXX
  , distSymbols(from.distSymbols) // !
  , txData(0)
  , merges(from.merges)
  {
}
ConfigurationData::~ConfigurationData() {
  if (txData)
    delete txData;
}
ConfigurationData* ConfigurationData::fork(State* state) const {
  DD::cout << "#### ConfigurationData::fork(" << state << ")" << DD::endl;
  return new ConfigurationData(*this,state->executionState());
}

SenderTxData& ConfigurationData::transmissionProperties(net::ConstIteratable<klee::ref<klee::Expr> > const& begin, net::ConstIteratable<klee::ref<klee::Expr> > const& end, TransmissionKind::Enum kind) {
  std::string designation;
  switch (kind) {
    case TransmissionKind::tx:
      designation = (std::string("tx") + llvm::utostr(forState.getCompletedTransmissions() + 1));
      break;
    case TransmissionKind::pull:
      designation = std::string("pull") + llvm::utostr(forState.getCompletedPullRequests() + 1);
      break;
    case TransmissionKind::merge:
      designation = std::string("merge") + llvm::utostr(++merges);
      break;
  };
  assert(designation.size());
  std::vector<klee::ref<klee::Expr> > data;
  for (net::ConstIteratorHolder<klee::ref<klee::Expr> > it = begin; it != end; ++it) {
    data.push_back(*it);
  }
  if (txData) DD::cout << "old tx string " << txData->designation << DD::endl;
  DD::cout << "new tx string " << designation << DD::endl;
  if (txData && (txData->designation != designation)) {
    txData->~SenderTxData();
    txData = new(txData) SenderTxData(*this,designation,distSymbols,data.begin(),data.end());
  } else {
    txData = new SenderTxData(*this,designation,distSymbols,data.begin(),data.end());
  }
  return *txData;
}

void ConfigurationData::configureState(klee::ExecutionState& state) {
  if ((!state.configurationData) || (&(state.configurationData->self().forState) != &state)) {
    if (state.configurationData)
      delete state.configurationData;
    state.configurationData = new ConfigurationData(state,KleeNet::getStateNode(state));
  }
}
