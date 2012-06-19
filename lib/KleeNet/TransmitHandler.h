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

namespace kleenet {
  class KleeNet;
  class TransmitHandler : public net::TransmitHandler<PacketInfo> {
    private:
      KleeNet& kleenet;
    public:
      TransmitHandler(KleeNet& kleenet) : kleenet(kleenet) {}
      virtual void handleTransmission(PacketInfo const& pi, net::BasicState* sender, net::BasicState* receiver, std::vector<net::DataAtomHolder> const& data) const;
  };
}

