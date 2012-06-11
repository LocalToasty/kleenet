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
  struct ConfigurationDataBase { // used to figure out how to handle transmissions
    virtual ~ConfigurationDataBase() {}
  };

  class State : public net::BasicState {
    friend class RegisterChildDependant;
    friend class KleeNet;
    private:
      Executor* executor;
    public:
      ConfigurationDataBase* configurationData;
      State() : net::BasicState(), executor(0), configurationData(0) {}
      State(State const& from) : net::BasicState(from), executor(from.executor), configurationData(0) {}
      ~State() {
        if (configurationData)
          delete configurationData;
      }
      virtual State* branch() = 0;
      State* forceFork();
      void mergeConstraints(State&);
  };
}

