#pragma once

#include "klee_headers/SpecialFunctionHandler.h"

#include "net/Node.h"

namespace kleenet {
  class Executor;
  class ExDataCarrier;

  class SpecialFunctionHandler : public klee::SpecialFunctionHandler {
    private:
      Executor& netEx;
    public:
      void memoryTransferWrapper(klee::ExecutionState &state,
                                 klee::ref<klee::Expr> dest, size_t destLen,
                                 ExDataCarrier const& src,
                                 net::Node destId);

      SpecialFunctionHandler(Executor&); // note: Executor is kleenet::Executor!
  };
}
