//===-- NetExecutor.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Class to perform actual execution, hides implementation details from external
// interpreter.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "klee_headers/Executor.h"

#include "kleenet/NetInterpreter.h"

#include "kleenet/KleeNet.h"

#include <vector>
#include <utility> // pair
#include <set>

namespace klee {
  class ExecutionState;
  class SpecialFunctionHandler;
  class PTree;
  class TimingSolver;
}

namespace kleenet {
  class Executor : public klee::Executor {
    friend class NetExecutorBuilder;
    friend class NetExTHnd;
    friend class State;
    friend class KleeNet;
    public:
      struct StateCondition {
        enum Enum {
          invalid = 0, // exceptional condition
          removed = -2,
          added = 2,
          active = 1 // normal condition
        };
      };
    private:
      void addedState(klee::ExecutionState*);
      klee::PTree* getPTree() const;
      klee::TimingSolver* getTimingSolver();
      std::vector<std::pair<std::set<klee::ExecutionState*>*,StateCondition::Enum> > conditionals;
    protected:
      KleeNet kleenet;
      // this is the same handler as klee::Executor::interpreterHandler but with correct type
      InterpreterHandler* const netInterpreterHandler;
      Searcher* netSearcher;
      Executor(const klee::Interpreter::InterpreterOptions &opts, InterpreterHandler *ie);
      klee::SpecialFunctionHandler* newSpecialFunctionHandler();
      klee::Searcher* constructUserSearcher(klee::Executor&);
      void run(klee::ExecutionState& initialState); // intrusively overrides klee::Executor::run
    public:
      using klee::Executor::bindLocal;
      using klee::Executor::solver;
      using klee::Executor::globalObjects;
      using klee::Executor::memory;
      StateCondition::Enum stateCondition(klee::ExecutionState*) const;

      KleeNet const& kleeNet;
      Searcher* getNetSearcher() const;

      void terminateState(klee::ExecutionState&);
      void terminateState_klee(klee::ExecutionState&);
      void terminateStateEarly(klee::ExecutionState&, llvm::Twine const&);
      void terminateStateEarly_klee(klee::ExecutionState&, llvm::Twine const&);
      void terminateStateOnExit(klee::ExecutionState&);
      void terminateStateOnExit_klee(klee::ExecutionState&);
      void terminateStateOnError(klee::ExecutionState&, llvm::Twine const&, char const*, llvm::Twine const& = "");
      void terminateStateOnError_klee(klee::ExecutionState&, llvm::Twine const&, char const*, llvm::Twine const& = "");
  };
}

