//===-- DistributedConstraints.h --------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "net/Node.h"
#include <cstddef>

namespace klee {
  class Array;
  template <typename> class ref;
  class Expr;
}

namespace kleenet {
  class DistributedArray;
  class StateDistSymbols_impl;

  // These are all (distributed) symbols of a given state.
  class StateDistSymbols {
    friend class DistributedArray;
    private:
      StateDistSymbols_impl& pimpl;
    public:
      net::Node const node;

      explicit StateDistSymbols(net::Node const node);
      StateDistSymbols(StateDistSymbols const&);
      ~StateDistSymbols();

      // `inState` is allowed to coincide with `this`.
      klee::Array const* locate(klee::Array const* array, size_t forTx, StateDistSymbols* inState);

      // utilities ...
      bool isDistributed(klee::Array const*) const;
      typedef klee::ref<klee::Expr> RefExpr;
      static RefExpr buildRead8(klee::Array const* array, size_t offset);
      static RefExpr buildCompleteRead(klee::Array const* array);
      static RefExpr buildEquality(RefExpr,RefExpr);
      static RefExpr buildEquality(klee::Array const*,klee::Array const*);
  };
}
