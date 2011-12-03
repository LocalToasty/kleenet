#include "TransmitHandler.h"

#include "klee/ExecutionState.h"

#include "../Core/Memory.h" //yack!

#include "AtomImpl.h"

using namespace kleenet;

namespace klee {
  class ObjectState;
  class Expr;
}

void TransmitHandler::handleTransmission(PacketInfo const& pi, net::BasicState* basicSender, net::BasicState* basicReceiver, std::vector<net::DataAtomHolder> const& data) const {
  klee::ExecutionState& sender = static_cast<klee::ExecutionState&>(*basicSender);
  klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(*basicReceiver);
  // memcpy
  const klee::ObjectState* ose = receiver.addressSpace.findObject(pi.destMo);
  assert(ose && "Destination ObjectState not found.");
  klee::ObjectState* wos = receiver.addressSpace.getWriteable(pi.destMo, ose);
  std::vector<net::DataAtomHolder>::const_iterator j = data.begin(), f = data.end();
  // important remark: data might be longer or shorter than pi.length. Always obey the size dictated by PacketInfo.
  for (unsigned i = 0; i < pi.length; i++) {
    if (j == f)
      j = data.begin();
    wos->write(pi.offset + i, dataAtomToExpr(*j++));
  }
  // merge with destination state's constraints
  receiver.mergeConstraints(sender); // FIXME
}
