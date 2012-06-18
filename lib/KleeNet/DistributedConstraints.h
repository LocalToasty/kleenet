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

namespace klee {
  class Array;
}

namespace kleenet {
  class DistributedArray;

  // These are all (distributed) symbols of a given state.
  class StateDistSymbols {
    private:
      net::Node const src;
      std::map<size_t,std::map<klee::Array*,DistributedArray*> > knownArrays;
      DistributedArray& castOrMake(klee::Array&, size_t);
    public:
      explicit StateDistSymbols(net::Node const src) : src(src) {}
      klee::Array* locate(klee::Array* array, size_t forTx, StateDistSymbols* inState);
  };
}
