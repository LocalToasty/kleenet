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

#define DD net::DEBUG<net::debug::external1>


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

namespace klee {
  class ObjectState;
  class Expr;
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

  klee::ObjectState const* oseDest = receiver.addressSpace.findObject(pi.destMo);
  assert(oseDest && "Destination ObjectState not found.");
  klee::ObjectState* wosDest = receiver.addressSpace.getWriteable(pi.destMo, oseDest);
  bool const hasSymbolics = cst.receiverData().isNonConstTransmission();
  if (   (txSymbolConstructionChoice == tscp_FORCEALL)
      || (hasSymbolics && (txSymbolConstructionChoice == tscp_SYMBOLICS))) {
    klee::MemoryObject* const mo = receiver.getExecutor()->memory->allocate(pi.length,false,true,NULL);
    mo->setName(cst.receiverData().specialTxName);
    klee::Array const* const array = new klee::Array(cst.receiverData().specialTxName,mo->size);
    klee::ObjectState* const ose = new klee::ObjectState(mo,array);
    ose->initializeToZero();
    receiver.addressSpace.bindObject(mo,ose);
    receiver.addSymbolic(mo,array);

    for (unsigned i = 0; i < pi.length; i++) {
      ExprBuilder::RefExpr r8 = ExprBuilder::buildRead8(array,i);
      wosDest->write(pi.offset + i, r8);
      receiver.constraints.addConstraint(ExprBuilder::buildEquality(r8,cst.receiverData()[i]));
    }
  } else {
    for (unsigned i = 0; i < pi.length; i++) {
      wosDest->write(pi.offset + i, cst.receiverData()[i]);
    }
  }
  if (hasSymbolics) {
    DD::cout << "| " << "Sender Constraints:" << DD::endl;
    DD::cout << "|   "; pprint(sender.constraints);
    DD::cout << "| " << "Receiver Constraints:" << DD::endl;
    DD::cout << "|   "; pprint(receiver.constraints);
    DD::cout << "| " << "Processing OFFENDING constraints:" << DD::endl;
    std::vector<klee::ref<klee::Expr> > const constr = cst.extractConstraints();
    for (std::vector<klee::ref<klee::Expr> >::const_iterator it = constr.begin(), end = constr.end(); it != end; ++it) {
      receiver.constraints.addConstraint(*it);
    }
    DD::cout << "| " << "EOF Constraints." << DD::endl;
  } else {
    DD::cout << "| " << "All data in tx is constant. Bypassing Graph construction." << DD::endl;
  }
  DD::cout << "│                                                                              │" << DD::endl
           << "│ EOF TRANSMISSION #" << currentTx << "                                                          │" << DD::endl
           << "└──────────────────────────────────────────────────────────────────────────────┘" << DD::endl
           << DD::endl;
}
