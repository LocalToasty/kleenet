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

namespace klee {
  class ExecutionState;
  class SpecialFunctionHandler;
  class PTree;
}

namespace kleenet {
  class Executor : public klee::Executor {
    friend class NetExecutorBuilder;
    friend class NetExTHnd;
    friend class State;
    private:
      void addedState(klee::ExecutionState*);
      klee::PTree* getPTree() const;
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

      KleeNet const& kleeNet;
      Searcher* getNetSearcher() const;

      void terminateStateEarly(klee::ExecutionState&, llvm::Twine const&);
      void terminateStateEarly_klee(klee::ExecutionState&, llvm::Twine const&);
      void terminateStateOnExit(klee::ExecutionState&);
      void terminateStateOnExit_klee(klee::ExecutionState&);
      void terminateStateOnError(klee::ExecutionState&, llvm::Twine const&, char const*, llvm::Twine const& = "");
      void terminateStateOnError_klee(klee::ExecutionState&, llvm::Twine const&, char const*, llvm::Twine const& = "");
  };
}

