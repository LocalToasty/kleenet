#pragma once

#include <vector>
#include <stddef.h>

namespace net {
  class StateDependantI;
  class RegisterChildDependant;

  class BasicState {
    friend class RegisterChildDependant;
    private:
      typedef StateDependantI* Dependant;
      static size_t& tableSize();
      std::vector<Dependant> dependants;
      bool const fake;
    public:
      BasicState();
      BasicState(BasicState const&);
      virtual BasicState* forceFork() = 0;
      bool isFake() const;
      virtual ~BasicState();
  };
}

