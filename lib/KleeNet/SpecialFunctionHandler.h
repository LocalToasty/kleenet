#pragma once

#include "klee_headers/SpecialFunctionHandler.h"

namespace klee {
  class MemoryObject;
}

namespace kleenet {
  class Executor;
  class SpecialFunctionHandler_impl;

  class SpecialFunctionHandler : public klee::SpecialFunctionHandler {
    private:
      SpecialFunctionHandler_impl& impl;
    public:
      SpecialFunctionHandler(Executor&); // note: Executor is kleenet::Executor!
      ~SpecialFunctionHandler();
  };
}
