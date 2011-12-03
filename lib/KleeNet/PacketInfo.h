//===-- PacketInfo.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/Node.h"

namespace klee {
  class MemoryObject;
}

namespace kleenet {
  struct PacketInfo {
    uint64_t addr;
    uint64_t offset;
    size_t length;
    klee::MemoryObject const* destMo;
    net::Node src;
    net::Node dest;
    PacketInfo(uint64_t addr, uint64_t offset, size_t length, klee::MemoryObject const* destMo, net::Node src, net::Node dest);
    bool operator<(PacketInfo const&) const;
    operator net::Node() const;
  };
}

