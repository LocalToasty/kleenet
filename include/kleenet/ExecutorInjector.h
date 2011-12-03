//===-- ExecutorInjector.h --------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#pragma once

// This file only serves the purpose to tweak some members of klee::Executor, such that we can later override them properly.

namespace llvm {
  class Twine;
}

namespace klee {
  class Executor;
  class Searcher;
  class ExecutionState;
  class SpecialFunctionHandler;
}

namespace kleenet {
  class ExecutorInjector {
    friend class klee::Executor; // this can only be instantiated by klee::Executor!
    private:
      ExecutorInjector();
    protected:
      // KleeNet: These must be for us to react to state terminations and trigger cluster-removal
      // remove state from queue and delete
      virtual void terminateState(klee::ExecutionState&) = 0;
      // call exit handler and terminate state
      virtual void terminateStateEarly(klee::ExecutionState&, llvm::Twine const&) = 0;
      // call exit handler and terminate state
      virtual void terminateStateOnExit(klee::ExecutionState&) = 0;
      // KleeNet extension: we inject our own special-function-handlers by overriding this.
      virtual klee::SpecialFunctionHandler* newSpecialFunctionHandler() = 0;
      // KleeNet extension: we inject our own searchers by overriding this.
      virtual klee::Searcher* constructUserSearcher(klee::Executor&);
  };
}
