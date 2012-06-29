#pragma once

#include "klee_headers/SpecialFunctionHandler.h"

#include "net/Node.h"

#include <vector>

namespace kleenet {
  class Executor;
  class ExDataCarrier;
  class SfhNodeContainer;

  class SpecialFunctionHandler : public klee::SpecialFunctionHandler {
    private:
      Executor& netEx;
    public:
      SfhNodeContainer& nodes;
      // we have to pass the result as argument, becase we don't know what a ExDataCarrier looks like in the .h file
      // returns the length (equalling `len` iff `len` != 0)
      size_t acquireExprRange(ExDataCarrier* out, std::vector<klee::ref<klee::Expr> >* optionalOut, klee::ExecutionState& sourceState, klee::ref<klee::Expr> dataSource, size_t len /*Maybe 0*/) const;

      void memoryTransferWrapper(klee::ExecutionState &state,
                                 klee::ref<klee::Expr> dest, size_t destLen,
                                 ExDataCarrier const& src,
                                 net::Node destId);

      SpecialFunctionHandler(Executor&); // note: Executor is kleenet::Executor!
      ~SpecialFunctionHandler();
  };
}
