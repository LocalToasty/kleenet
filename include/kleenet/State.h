//===-- State.h -------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "net/BasicState.h"

namespace kleenet {
  class KleeNet;
  class Executor;

  class State : public net::BasicState {
    friend class RegisterChildDependant;
    friend class KleeNet;
    private:
      Executor* executor;
    public:
      virtual State* branch() = 0;
      State* forceFork();
      void mergeConstraints(State&);
  };
}

