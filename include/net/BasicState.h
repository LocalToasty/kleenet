#pragma once

#include <vector>
#include <stddef.h>

namespace net {
  class StateDependantI;
  class RegisterChildDependant;

  class BasicState {
    friend class RegisterChildDependant;
    private:
      static size_t& tableSize();
      std::vector<StateDependantI*> dependants;
    public:
      BasicState();
      BasicState(BasicState const&);
      virtual BasicState* forceFork() = 0;
      virtual ~BasicState();
  };
}

