#include "ConstraintSet.h"

#include "ConfigurationData.h"
#include "ExprBuilder.h"

#include "klee/ExecutionState.h"
#include "llvm/Support/CommandLine.h"

#include <cstddef>
#include <utility>

namespace {
  struct DummyIterator {};

  llvm::cl::opt<kleenet::ConstraintSetTransfer::TxConstraintsTransmission>
  txConstraintsTransmission("sde-constraints-transmission"
    , llvm::cl::desc("Select how to decide which constraints to propagate on symbolic packet transmission. Default is 'closure'.")
    , llvm::cl::values(
        clEnumValN(kleenet::ConstraintSetTransfer::CLOSURE,"closure","Compute the minimal set of constraints for a given transmission and only transmit the constraints that affect the packet data.")
      , clEnumValN(kleenet::ConstraintSetTransfer::FORCEALL,"force-all","Allways transfer all constraints that affect all distributed symbols of the sender state. In some rare cases this can prevent false positives.")
      , clEnumValEnd
    )
    , llvm::cl::init(kleenet::ConstraintSetTransfer::CLOSURE)
  );
}


namespace kleenet {
  struct ConstraintSetTransfer_impl {
    typedef ConstraintSetTransfer::TxConstraintsTransmission TxConstraintsTransmission;

    ConfigurationData& senderConfig;
    PerReceiverData receiverData;
    ConstraintSetTransfer_impl(ConstraintSet_impl const&, ConfigurationData&);

    std::vector<klee::ref<klee::Expr> > extractConstraints(TxConstraintsTransmission txct) {
      if (!receiverData.isNonConstTransmission())
        return std::vector<klee::ref<klee::Expr> >();
      if (txct == ConstraintSetTransfer::USERCHOICE)
        txct = txConstraintsTransmission;
      std::vector<klee::ref<klee::Expr> > constraints;
      receiverData.transferNewReceiverConstraints(
        net::util::FunctorBuilder<klee::ref<klee::Expr>,net::util::DynamicFunctor,net::util::IterateOperator>::build(
          std::back_inserter(constraints)
        )
      , txct == ConstraintSetTransfer::FORCEALL
      );
      PerReceiverData::NewSymbols const newSymbols = receiverData.newSymbols();
      net::Node const src = senderConfig.distSymbols.node;
      net::Node const dest = receiverData.receiverConfig.distSymbols.node;
      for (PerReceiverData::NewSymbols::const_iterator it = newSymbols.begin(), end = newSymbols.end(); it != end; ++it) {
        bool const isOnSender = it->belongsTo->node == src;
        klee::ExecutionState* const state = (isOnSender?senderConfig:receiverData.receiverConfig).forState.executionState();
        it->addArrayToStateNames(*state,src,dest);
        if (isOnSender)
          state->constraints.addConstraint(ExprBuilder::buildEquality(it->was,it->translated));
      }
      return constraints;
    }
  };

  struct ConstraintSet_impl {
    static ConfigurationData& getConfiguration(ConfigurationData& data) {
      return data;
    }
    static ConfigurationData& getConfiguration(klee::ExecutionState& state) {
      ConfigurationData::configureState(state);
      return state.configurationData->self();
    }
    ConfigurationData& cd;
    TransmissionKind::Enum tk;
    typedef std::pair<SenderTxData&,size_t> TxPair;
    TxPair txData;
    template <typename T, typename Iterator>
    ConstraintSet_impl(TransmissionKind::Enum tk, T& in, Iterator begin, Iterator end)
      : cd(getConfiguration(in))
      , tk(tk)
      , txData(expand(cd,begin,end,tk))
      {
    }
    template <typename T>
    ConstraintSetTransfer transferTo(T& f) const {
      return ConstraintSetTransfer(
        ConstraintSetTransfer::Pimpl(
          new ConstraintSetTransfer_impl(
            *this
          , getConfiguration(f)
          )
        )
      );
    }

    // If we're given actual data to "transmit" we expand all expressions there.
    template <typename Iterator>
    static TxPair expand(ConfigurationData& cd, Iterator begin, Iterator end, TransmissionKind::Enum tk) {
      return std::pair<SenderTxData&,size_t>(
        cd.transmissionProperties(
          net::StdIteratorFactory<klee::ref<klee::Expr> >::build(begin)
        , net::StdIteratorFactory<klee::ref<klee::Expr> >::build(end)
        , tk
        )
      , end - begin
      );
    }

    // If we're not given data to "transmit" we assume we are supposed to transmit all our internal copies of distributed Arrays.
    static TxPair expand(ConfigurationData& cd, DummyIterator, DummyIterator, TransmissionKind::Enum tk) {
      std::vector<klee::Array const*> ownArrays;
      cd.distSymbols.iterateArrays(
        net::util::FunctorBuilder<klee::Array const*,net::util::DynamicFunctor,net::util::IterateOperator>::build(
          std::back_inserter(ownArrays)
        )
      );
      return std::pair<SenderTxData&,size_t>(
        cd.transmissionProperties(
          net::StdIteratorFactory<klee::ref<klee::Expr> >::build(ownArrays.begin(),ExprBuilder::buildCompleteRead)
        , net::StdIteratorFactory<klee::ref<klee::Expr> >::build(ownArrays.end(),ExprBuilder::buildCompleteRead)
        , tk
        )
      , ownArrays.size()
      );
    }
  };

  ConstraintSetTransfer_impl::ConstraintSetTransfer_impl(ConstraintSet_impl const& csi, ConfigurationData& transferTo)
    : senderConfig(csi.cd)
    , receiverData(csi.txData.first,transferTo,0,csi.txData.second) {
  }
}

using namespace kleenet;

typedef ConstraintSet_impl Impl;

ConstraintSet::ConstraintSet(TransmissionKind::Enum tk, klee::ExecutionState& state)
  : pimpl(new ConstraintSet_impl(tk, state, DummyIterator(), DummyIterator())) {
}

ConstraintSet::ConstraintSet(TransmissionKind::Enum tk, ConfigurationData& data)
  : pimpl(new ConstraintSet_impl(tk, data, DummyIterator(), DummyIterator())) {
}

ConstraintSet::ConstraintSet(TransmissionKind::Enum tk, klee::ExecutionState& state, It begin, It end)
  : pimpl(new ConstraintSet_impl(tk, state, begin, end)) {
}

ConstraintSet::ConstraintSet(TransmissionKind::Enum tk, ConfigurationData& data, It begin, It end)
  : pimpl(new ConstraintSet_impl(tk, data, begin, end)) {
}

ConstraintSetTransfer ConstraintSet::extractFor(klee::ExecutionState& e) {
  return pimpl->transferTo(e);
}

ConstraintSetTransfer ConstraintSet::extractFor(ConfigurationData& cd) {
  return pimpl->transferTo(cd);
}

ConstraintSetTransfer::ConstraintSetTransfer(Pimpl const& pimpl)
  : pimpl(pimpl) {
}

PerReceiverData& ConstraintSetTransfer::receiverData() const {
  return pimpl->receiverData;
}

std::vector<klee::ref<klee::Expr> > ConstraintSetTransfer::extractConstraints(TxConstraintsTransmission tct) const {
  return pimpl->extractConstraints(tct);
}

void ConstraintSetTransfer::IDel::operator()(ConstraintSetTransfer_impl* p) const {
  delete p;
}

void ConstraintSet::IDel::operator()(ConstraintSet_impl* p) const {
  delete p;
}
