//===-- TransmitHandler.h ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "net/TransmitHandler.h"

#include "PacketInfo.h"

namespace klee {
  class ExecutionState;
  class ObjectState;
  class MemoryObject;
  template <typename> class ref;
  class Expr;
}
namespace kleenet {
  class PerReceiverData;
}

namespace kleenet {
  class TransmitHandler : public net::TransmitHandler<PacketInfo> {
    public:
      virtual void handleTransmission(klee::ExecutionState& receiver, klee::MemoryObject const* destMo, size_t offset, size_t size, PerReceiverData&, std::vector<klee::ref<klee::Expr> > const& withConstraints = std::vector<klee::ref<klee::Expr> >()) const; // low level, KleeNet specific wrapper
      virtual void handleTransmission(klee::ExecutionState& receiver, klee::ObjectState* wosDest, size_t offset, size_t size, PerReceiverData&, std::vector<klee::ref<klee::Expr> > const& withConstraints = std::vector<klee::ref<klee::Expr> >()) const; // low level, KleeNet specific wrapper
      virtual void handleTransmission(PacketInfo const& pi, net::BasicState* sender, net::BasicState* receiver, std::vector<net::DataAtomHolder> const& data) const; // generic routine, REQUIRED by net::TransmitHandler
  };
}

