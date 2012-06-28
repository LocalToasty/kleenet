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
#include "net/util/Functor.h"

#include <cstddef>
#include <string>

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
      klee::Array const* locate(klee::Array const* array, std::string designation, StateDistSymbols* inState);
      void iterateArrays(net::util::DynamicFunctor<klee::Array const*> const&) const;

      // utilities ...
      bool isDistributed(klee::Array const*) const;
  };
}
