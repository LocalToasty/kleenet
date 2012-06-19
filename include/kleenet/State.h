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
  class ConfigurationData;
  struct ConfigurationDataBase { // used to figure out how to handle transmissions
    virtual ~ConfigurationDataBase() {}
    virtual ConfigurationData& self() = 0;
    virtual ConfigurationDataBase* fork() const {
      return 0;
    }
    static ConfigurationDataBase* attemptFork(ConfigurationDataBase* from) {
      if (from)
        return from->fork();
      return 0;
    }
  };

  class State : public net::BasicState {
    friend class RegisterChildDependant;
    friend class KleeNet;
    private:
      Executor* executor;
    public:
      ConfigurationDataBase* configurationData;
      State() : net::BasicState(), executor(0), configurationData(0) {}
      State(State const& from) : net::BasicState(from), executor(from.executor), configurationData(ConfigurationDataBase::attemptFork(from.configurationData)) {}
      ~State() {
        if (configurationData)
          delete configurationData;
      }
      virtual State* branch() = 0;
      State* forceFork();
      void mergeConstraints(State&);
      Executor* getExecutor() const;
  };
}

