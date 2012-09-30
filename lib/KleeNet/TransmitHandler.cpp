#include "TransmitHandler.h"

#include "ConfigurationData.h"
#include "ExprBuilder.h"
#include "NetExecutor.h"
#include "ConstraintSet.h"

#include "net/Iterator.h"
#include "net/util/Containers.h"

#include "klee/ExecutionState.h"
#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"

#include "llvm/Support/CommandLine.h"

#include <vector>

#include "net/util/debug.h"
#include "kexPPrinter.h"

#define DD net::DEBUG<net::debug::transmit>


namespace kleenet {

  template <typename T>
  void pprint(T const& t) {
    pprint(DD(),t,"|   ");
  }

}

namespace {
  enum TxSymbolCreationParadigm {
    tscp_BYPASS,
    tscp_SYMBOLICS,
    tscp_FORCEALL
  };
  llvm::cl::opt<TxSymbolCreationParadigm>
  txSymbolConstructionChoice("sde-packet-symbol-construction"
    , llvm::cl::desc("Choose for which kind of transmission to introduce individual packets. By default symbols of the form 'tx<<id>>(node<<node_id>>)' will be created for every packet that does not entirely consist of constants (i.e. has at least one symbolic).")
    , llvm::cl::values(
        clEnumValN(tscp_BYPASS,"bypass","Bypass symbol construction alltogether. This option is useful, when the generated tests are of less importance, the actual packet data is irrelevant. Construction of these symbols is expensive, so it should be avoided if you are not interested.")
      , clEnumValN(tscp_SYMBOLICS,"symbolics","Construct packet symbols only for packets that contain any symbolic data. This is the default.")
      , clEnumValN(tscp_FORCEALL,"force","Construct packet symbols not only for symbolic packets but for every single packet, even if its completely concrete.")
      , clEnumValEnd
    )
    , llvm::cl::init(tscp_SYMBOLICS)
  );

}

using namespace kleenet;

void TransmitHandler::handleTransmission(klee::ExecutionState& receiver, klee::MemoryObject const* destMo, size_t offset, size_t size, PerReceiverData& receiverData, std::vector<klee::ref<klee::Expr> > const& constr) const {
  klee::ObjectState const* oseDest = receiver.addressSpace.findObject(destMo);
  assert(oseDest && "Destination ObjectState not found.");
  klee::ObjectState* wosDest = receiver.addressSpace.getWriteable(destMo, oseDest);
  handleTransmission(receiver,wosDest,offset,size,receiverData,constr);
}

void TransmitHandler::handleTransmission(klee::ExecutionState& receiver, klee::ObjectState* wosDest, size_t offset, size_t size, PerReceiverData& receiverData, std::vector<klee::ref<klee::Expr> > const& constr) const {
  if (   (txSymbolConstructionChoice == tscp_FORCEALL)
      || (   (receiverData.isNonConstTransmission())
          && (txSymbolConstructionChoice == tscp_SYMBOLICS))) {

    klee::Array const* const array = receiver.makeNewSymbol(receiverData.specialTxName.c_str(),size);

    for (size_t i = 0; i < size; i++) {
      ExprBuilder::RefExpr r8 = ExprBuilder::buildRead8(array,i);
      DD::cout << "| " << "Packet[" << i << "] = ";
      DD::cout << "|    "; pprint(receiverData[i]);
      wosDest->write(offset + i, r8);
      receiver.constraints.addConstraint(ExprBuilder::buildEquality(r8,receiverData[i]));
    }
  } else {
    for (size_t i = 0; i < size; i++) {
      DD::cout << "| " << "Packet[" << i << "] = ";
      DD::cout << "|    "; pprint(receiverData[i]);
      wosDest->write(offset + i, receiverData[i]);
    }
  }
  DD::cout << "| " << "Processing OFFENDING constraints:" << DD::endl;
  for (std::vector<klee::ref<klee::Expr> >::const_iterator it = constr.begin(), end = constr.end(); it != end; ++it) {
    DD::cout << "|   "; pprint(*it);
    receiver.constraints.addConstraint(*it);
  }
  DD::cout << "| " << "EOF Constraints." << DD::endl;
}

void TransmitHandler::handleTransmission(PacketInfo const& pi, net::BasicState* basicSender, net::BasicState* basicReceiver, std::vector<net::DataAtomHolder> const& data) const {
  size_t const currentTx = basicSender->getCompletedTransmissions() + 1;
  DD::cout << DD::endl
           << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << DD::endl
           << "┃ STARTING TRANSMISSION #" << currentTx << " from node `" << pi.src.id << "' to `" << pi.dest.id << "'. of size " << pi.length << "                     ┃" << DD::endl
           << "┡━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┩" << DD::endl;
  klee::ExecutionState& sender = *static_cast<State*>(basicSender)->executionState();
  klee::ExecutionState& receiver = *static_cast<State*>(basicReceiver)->executionState();

  std::vector<klee::ref<klee::Expr> > expressions;
  expressions.reserve((data.size() < pi.length)?data.size():pi.length);
  for (size_t i = 0; i < data.size() && i < pi.length; ++i) {
    expressions.push_back(dataAtomToExpr(data[i]));
  }

  ConstraintSetTransfer const cst = ConstraintSet(TransmissionKind::tx,sender,expressions.begin(),expressions.end()).extractFor(receiver);

  DD::cout << "| " << "Sender Constraints:" << DD::endl;
  DD::cout << "|   "; pprint(sender.constraints);
  DD::cout << "| " << "Receiver Constraints:" << DD::endl;
  DD::cout << "|   "; pprint(receiver.constraints);

  handleTransmission(receiver,pi.destMo,pi.offset,pi.length,cst.receiverData(),cst.extractConstraints());

  DD::cout << "│                                                                              │" << DD::endl
           << "│ EOF TRANSMISSION #" << currentTx << "                                                          │" << DD::endl
           << "└──────────────────────────────────────────────────────────────────────────────┘" << DD::endl
           << DD::endl;
}
