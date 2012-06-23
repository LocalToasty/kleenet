#include "ConfigurationData.h"

#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"

#include "klee/ExecutionState.h"

#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"
#include "klee_headers/Common.h"

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
  bGraph.addNodes(cm.begin()+knownConstraints,cm.end(),cm.size()-knownConstraints);
  for (Vec::const_iterator it = cm.begin()+knownConstraints, end = cm.end(); it != end; ++it) {
    ExtractReadEdgesVisitor<BGraph,Vec::value_type>(bGraph,*it).visit(*it);
  }
  knownConstraints = cm.size();
}





ConfigurationData::PerReceiverData::PerReceiverData(TxData& txData, StateDistSymbols& distSymbolsDest, size_t const beginPrecomputeRange, size_t const endPrecomputeRange)
  : txData(txData)
  , distSymbolsDest(distSymbolsDest)
  , nmh(txData.currentTx,txData.distSymbolsSrc,distSymbolsDest)
  , rt(nmh.mangler,txData.seq,&(txData.senderSymbols))
  , constraintsComputed(false)
  , receiverConstraints()
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

std::string ConfigurationData::TxData::makeSpecialName(size_t const currentTx, net::Node const node) {
  return std::string("tx") + llvm::utostr(currentTx) + "(node" + llvm::itostr(node.id) + ")";
}
ConfigurationData::TxData::ConList const& ConfigurationData::PerReceiverData::computeNewReceiverConstraints() { // result already translated!
  if (!constraintsComputed) {
    constraintsComputed = true;
    assert(receiverConstraints.empty() && "Garbate data in our constraints buffer.");
    std::vector<klee::ref<klee::Expr> > const& senderConstraints = txData.computeSenderConstraints();
    receiverConstraints.resize(senderConstraints.size());
    std::transform<std::vector<klee::ref<klee::Expr> >::const_iterator, std::vector<klee::ref<klee::Expr> >::iterator, ReadTransformator&>(
        senderConstraints.begin()
      , senderConstraints.end()
      , receiverConstraints.begin()
      , rt
    );
  }
  return receiverConstraints;
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
      result.push_back(GeneratedSymbolInformation(&distSymbolsDest,it->first,it->second));
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
  , txData(0) {
}
ConfigurationData::~ConfigurationData() {
  if (txData)
    delete txData;
}
ConfigurationData::TxData& ConfigurationData::transmissionProperties(std::vector<net::DataAtomHolder> const& data) {
  updateTxData(data);
  return *txData;
}

void ConfigurationData::configureState(klee::ExecutionState& state, KleeNet& kleenet) {
  if ((!state.configurationData) || (&(static_cast<ConfigurationData&>(*state.configurationData).forState) != &state)) {
    if (!state.configurationData)
      delete state.configurationData;
    state.configurationData = new ConfigurationData(state,kleenet.getStateNode(state)); //shared pointer takes care of deletion
  }
}

void ConfigurationData::updateTxData(std::vector<net::DataAtomHolder> const& data) {
  size_t const ctx = forState.getCompletedTransmissions() + 1;
  if (txData && (txData->currentTx != ctx)) {
    txData->~TxData();
    txData = new(txData) TxData(*this,ctx,distSymbols,data);
  } else {
    txData = new TxData(*this,ctx,distSymbols,data);
  }
}
