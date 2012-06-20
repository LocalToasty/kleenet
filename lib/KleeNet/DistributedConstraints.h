//===-- DistributedConstraints.h --------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <map>

#include "net/Node.h"
#include <map>

namespace klee {
  class Array;
}

namespace kleenet {
  class DistributedArray;

  // These are all (distributed) symbols of a given state.
  class StateDistSymbols {
    private:
      std::map<size_t,std::map<klee::Array const*,DistributedArray const*> > knownArrays;
      DistributedArray const& castOrMake(klee::Array const&, size_t);
    public:
      bool taintLocalSymbols() const;
      net::Node const node;
      explicit StateDistSymbols(net::Node const node) : knownArrays(), node(node) {}
      klee::Array const* locate(klee::Array const* array, size_t forTx, StateDistSymbols* inState);
      bool isDistributed(klee::Array const*) const;
  };
}
