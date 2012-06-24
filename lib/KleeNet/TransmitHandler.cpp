#include "TransmitHandler.h"

#include "ConfigurationData.h"

#include "klee/ExecutionState.h"
#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"

#include "llvm/Support/CommandLine.h"

#include "NetExecutor.h"

#include <vector>

#include "net/util/debug.h"
#include "kexPPrinter.h"

#define DD net::DEBUG<net::debug::external1>


namespace kleenet {

  template <typename T>
  void pprint(T const& t) {
    pprint(DD(),t,"|   ");
  }

}

namespace {
  enum TxSymbolCreationParadigm {
    BYPASS,
    SYMBOLICS,
    FORCEALL
  };
  llvm::cl::opt<TxSymbolCreationParadigm>
  txSymbolConstructionChoice("packet-symbol-construction"
    , llvm::cl::desc("Choose for which kind of transmission to introduce individual packets. By default symbols of the form 'tx<<id>>(node<<node_id>>)' will be created for every packet that does not entirely consist of constants (i.e. has at least one symbolic). This is a KleeNet extension.")
    , llvm::cl::values(
        clEnumValN(BYPASS,"bypass","Bypass symbol construction alltogether. This option is useful, when the generated tests are of less importance, the actual packet data is irrelevant. Construction of these symbols is expensive, so it should be avoided if you are not interested.")
      , clEnumValN(SYMBOLICS,"symbolics","Construct packet symbols only for packets that contain any symbolic data. This is the default.")
      , clEnumValN(FORCEALL,"force","Construct packet symbols not only for symbolic packets but for every single packet, even if its completely concrete.")
      , clEnumValEnd
    )
    , llvm::cl::init(SYMBOLICS)
  );
}

using namespace kleenet;

namespace klee {
  class ObjectState;
  class Expr;
}

namespace {
  struct ConstraintAdder {
    klee::ExecutionState& state;
    ConstraintAdder(klee::ExecutionState& state) : state(state) {}
    void operator()(klee::ref<klee::Expr> expr) const {
      state.constraints.addConstraint(expr);
    }
  };
}

void TransmitHandler::handleTransmission(PacketInfo const& pi, net::BasicState* basicSender, net::BasicState* basicReceiver, std::vector<net::DataAtomHolder> const& data) const {
  size_t const currentTx = basicSender->getCompletedTransmissions() + 1;
  DD::cout << DD::endl
           << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << DD::endl
           << "┃ STARTING TRANSMISSION #" << currentTx << " from node `" << pi.src.id << "' to `" << pi.dest.id << "'.                               ┃" << DD::endl
           << "┡━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┩" << DD::endl;
  klee::ExecutionState& sender = static_cast<klee::ExecutionState&>(*basicSender);
  klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(*basicReceiver);

  ConfigurationData::configureState(sender,kleenet);
  ConfigurationData::configureState(receiver,kleenet);

  DD::cout << "| " << "Involved states: " << &sender << " ---> " << &receiver << DD::endl;
  ConfigurationData::PerReceiverData receiverData(
      sender.configurationData->self().transmissionProperties(data)
    , receiver.configurationData->self()
    , 0, pi.length // precomputation of symbols
  );
  klee::ObjectState const* oseDest = receiver.addressSpace.findObject(pi.destMo);
  assert(oseDest && "Destination ObjectState not found.");
  klee::ObjectState* wosDest = receiver.addressSpace.getWriteable(pi.destMo, oseDest);
  bool const hasSymbolics = receiverData.isNonConstTransmission();
  if ((txSymbolConstructionChoice == FORCEALL) || (hasSymbolics && (txSymbolConstructionChoice == SYMBOLICS))) {
    klee::MemoryObject* const mo = receiver.getExecutor()->memory->allocate(pi.length,false,true,NULL);
    mo->setName(receiverData.specialTxName);
    klee::Array const* const array = new klee::Array(receiverData.specialTxName,mo->size);
    klee::ObjectState* const ose = new klee::ObjectState(mo,array);
    ose->initializeToZero();
    receiver.addressSpace.bindObject(mo,ose);
    receiver.addSymbolic(mo,array);
    ConfigurationData::PerReceiverData::GeneratedSymbolInformation(&(receiver.configurationData->self().distSymbols),array,array);

    for (unsigned i = 0; i < pi.length; i++) {
      StateDistSymbols::RefExpr r8 = StateDistSymbols::buildRead8(array,i);
      wosDest->write(pi.offset + i, r8);
      receiver.constraints.addConstraint(StateDistSymbols::buildEquality(r8,receiverData[i]));
    }
  } else {
    for (unsigned i = 0; i < pi.length; i++) {
      wosDest->write(pi.offset + i, receiverData[i]);
    }
  }
  DD::cout << "| " << "Using the following transmission context: " << &receiverData << DD::endl;
  if (hasSymbolics) {
    DD::cout << "| " << "Sender Constraints:" << DD::endl;
    DD::cout << "|   "; pprint(sender.constraints);
    DD::cout << "| " << "Receiver Constraints:" << DD::endl;
    DD::cout << "|   "; pprint(receiver.constraints);
    DD::cout << "| " << "Processing OFFENDING constraints:" << DD::endl;
    receiverData.transferNewReceiverConstraints(net::util::FunctorBuilder<klee::ref<klee::Expr>,net::util::DynamicFunctor>::build(ConstraintAdder(receiver)));
    DD::cout << "| " << "EOF Constraints." << DD::endl;
    DD::cout << "| " << "Listing OFFENDING symbols:" << DD::endl;
    ConfigurationData::PerReceiverData::NewSymbols const newSymbols = receiverData.newSymbols();
    for (ConfigurationData::PerReceiverData::NewSymbols::const_iterator it = newSymbols.begin(), end = newSymbols.end(); it != end; ++it) {
      bool const isOnSender = it->belongsTo->node == pi.src;
      DD::cout << "| " << " + (on " << (isOnSender?"sender state)    ":"receiver state)  ") << it->was->name << "  |--->  " << it->translated->name << DD::endl;
      it->addArrayToStateNames(isOnSender?sender:receiver,pi.src,pi.dest);
      if (isOnSender) {
        DD::cout << "| " << "    -- reflexive: " << it->was->name << " == " << it->translated->name << DD::endl;
        klee::ref<klee::Expr> const eq = StateDistSymbols::buildEquality(it->was,it->translated);
        DD::cout << "|   "; pprint(eq);
        sender.constraints.addConstraint(eq);
      }
    }
  } else {
    DD::cout << "| " << "All data in tx is constant. Bypassing Graph construction." << DD::endl;
  }
  DD::cout << "│                                                                              │" << DD::endl
           << "│ EOF TRANSMISSION #" << currentTx << "                                                          │" << DD::endl
           << "└──────────────────────────────────────────────────────────────────────────────┘" << DD::endl
           << DD::endl;
}
