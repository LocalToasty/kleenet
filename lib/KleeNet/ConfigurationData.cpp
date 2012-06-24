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
      size_t const currentTx;
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
      SenderTxData(ConfigurationData& cd, size_t currentTx, StateDistSymbols& distSymbolsSrc, std::vector<net::DataAtomHolder> const& data)
        : currentTx(currentTx)
        , senderSymbols()
        , cd(cd)
        , distSymbolsSrc(distSymbolsSrc)
        , seq(transform(data.begin(),data.end(),data.size(),dataAtomToExpr,klee::ref<klee::Expr>()/* type inference */))
        , constraintsComputed(false)
        , senderConstraints()
        , senderReflexiveArraysComputed(false)
        , allowMorePacketSymbols(true)
        , specialTxName(std::string("tx") + llvm::utostr(currentTx) + "(node" + llvm::itostr(distSymbolsSrc.node.id) + ")")
        {
      }
      ConstraintsGraph::ConstraintList const& computeSenderConstraints() {
        if (!constraintsComputed) {
          allowMorePacketSymbols = false;
          constraintsComputed = true;
          assert(senderConstraints.empty() && "Garbate data in our sender's constraints buffer.");
          senderConstraints = cd.cg.eval(senderSymbols);
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
                 , LazySymbolTranslator::Symbols* preImageSymbols)
  : lst(mangler,preImageSymbols)
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




ConfigurationData::PerReceiverData::PerReceiverData(SenderTxData& txData, ConfigurationData& receiverConfig, size_t const beginPrecomputeRange, size_t const endPrecomputeRange)
  : txData(txData)
  , receiverConfig(receiverConfig)
  , nmh(txData.currentTx,txData.distSymbolsSrc,receiverConfig.distSymbols)
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

bool ConfigurationData::PerReceiverData::isNonConstTransmission() const {
  return !(txData.senderSymbols.empty() && rt.symbolTable().empty());
}

klee::ref<klee::Expr> ConfigurationData::PerReceiverData::operator[](size_t index) {
  size_t const existingPacketSymbols = txData.senderSymbols.size();
  klee::ref<klee::Expr> expr = rt[index];
  assert((txData.allowMorePacketSymbols || (existingPacketSymbols == txData.senderSymbols.size())) \
    && "When translating an atom, we found completely new symbols, but we already assumed we were done with that.");
  return expr;
}

void ConfigurationData::PerReceiverData::transferNewReceiverConstraints(net::util::DynamicFunctor<klee::ref<klee::Expr> > const& transfer) { // result already translated!
  if (!constraintsComputed) {
    constraintsComputed = true;
    ConstraintsGraph::ConstraintList const& senderConstraints = txData.computeSenderConstraints();
    for (ConstraintsGraph::ConstraintList::const_iterator it = senderConstraints.begin(), end = senderConstraints.end(); it != end; ++it) {
      DD::cout << "| " << "adding constraint ... " << DD::endl;
      DD::cout << "| "; pprint(DD(),rt(*it),"| ");
      transfer(rt(*it));
    }
  }
}
std::vector<std::pair<klee::Array const*,klee::Array const*> > ConfigurationData::PerReceiverData::additionalSenderOnlyConstraints() {
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

ConfigurationData::PerReceiverData::NewSymbols ConfigurationData::PerReceiverData::newSymbols() { // rvo
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

void ConfigurationData::PerReceiverData::GeneratedSymbolInformation::addArrayToStateNames(klee::ExecutionState& state, net::Node src, net::Node dest) const {
  if (!state.arrayNames.insert(translated->name).second && !belongsTo->isDistributed(translated)) {
    klee::klee_error("%s",(std::string("In transmission of symbol '") + was->name + "' from node " + llvm::itostr(src.id) + " to node " + llvm::itostr(dest.id) + "; Symbol '" + translated->name + "' already exists on the target state. This is either a bug in KleeNet or you used the symbol '}' when specifying the name of a symbolic object which is not supported in KleeNet as it is used to mark special distributed symbols.").c_str());
  }
}

ConfigurationData::ConfigurationData(klee::ExecutionState& state, net::Node src)
  : forState(state)
  , cg(state.constraints)
  , distSymbols(src)
  , txData(0)
  {
}
ConfigurationData::ConfigurationData(ConfigurationData const& from, State* state)
  : forState(*state)
  , cg(static_cast<klee::ExecutionState*>(state)->constraints) // XXX dangerous upcast because ES may not exist yet, but cg only stores the reference, so cross your fingers XXX
  , distSymbols(from.distSymbols) // !
  , txData(0)
  {
}
ConfigurationData::~ConfigurationData() {
  if (txData)
    delete txData;
}
ConfigurationData* ConfigurationData::fork(State* state) const {
  DD::cout << "#### ConfigurationData::fork(" << state << ")" << DD::endl;
  return new ConfigurationData(*this,state);
}

SenderTxData& ConfigurationData::transmissionProperties(std::vector<net::DataAtomHolder> const& data) {
  updateSenderTxData(data);
  return *txData;
}

void ConfigurationData::configureState(klee::ExecutionState& state, KleeNet& kleenet) {
  if ((!state.configurationData) || (&(state.configurationData->self().forState) != &state)) {
    if (state.configurationData)
      delete state.configurationData;
    state.configurationData = new ConfigurationData(state,kleenet.getStateNode(state));
  }
}

void ConfigurationData::updateSenderTxData(std::vector<net::DataAtomHolder> const& data) {
  size_t const ctx = forState.getCompletedTransmissions() + 1;
  if (txData && (txData->currentTx != ctx)) {
    txData->~SenderTxData();
    txData = new(txData) SenderTxData(*this,ctx,distSymbols,data);
  } else {
    txData = new SenderTxData(*this,ctx,distSymbols,data);
  }
}
