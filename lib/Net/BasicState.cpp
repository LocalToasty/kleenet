#include "net/BasicState.h"

#include "StateDependant.h"

#include <assert.h>

using namespace net;

size_t& BasicState::tableSize() {
  // avoid static initialisation order fiasco!
  static size_t _tableSize = 0;
  return _tableSize;
}

BasicState::BasicState() : dependants(tableSize(),NULL) {
}

BasicState::BasicState(BasicState const& from) : dependants(tableSize(), NULL) {
  assert(from.dependants.size() == tableSize() && "Table size was changed after initialisation");
  for (size_t i = 0; i < tableSize(); i++) {
    StateDependantI* const dep(from.dependants[i]);
    if (dep)
      dependants[i] = static_cast<StateDependantI*>(dep->getCloner()(dep));
  }
}

BasicState::~BasicState() {}
