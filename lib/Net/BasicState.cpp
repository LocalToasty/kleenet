#include "net/BasicState.h"

#include "StateDependant.h"

#include "util/ObjectProperties.h"

#include <assert.h>

//#include <iostream> // XXX

using namespace net;

size_t& BasicState::tableSize() {
  // avoid static initialisation order fiasco!
  static size_t _tableSize = 0;
  return _tableSize;
}

BasicState::BasicState() : dependants(tableSize(),NULL), fake(util::isOnStack(this)) {
  //std::cout << "Creating STATE " << this << std::endl; // XXX
}

bool BasicState::isFake() const {
  return fake;
}

BasicState::BasicState(BasicState const& from) : dependants(tableSize(), NULL), fake(util::isOnStack(this)) {
  //std::cout << "Creating STATE " << this << " (from " << &from << ")" << std::endl; // XXX

  assert((fake || !from.fake) && "Attempt to create a non-fake state from a fake state.");
  assert(from.dependants.size() == tableSize() && "Table size was changed after initialisation");
  if (!fake) {
    for (size_t i = 0; i < tableSize(); i++) {
      StateDependantI* const dep(from.dependants[i]);
      if (dep) {
        // This will automatically set dependants[i] to the new StateDepedantI*
        static_cast<StateDependantI*>(dep->getCloner()(dep))->setState(this);
      }
    }
  }
}

BasicState::~BasicState() {
  //std::cout << "Destroying STATE " << this << std::endl; // XXX
  for (size_t i = 0; i < tableSize(); i++) {
    StateDependantI* const dep(dependants[i]);
    if (dep) {
      assert(!fake && "A fake state should not have dependants!");
      delete dep;
    }
  }
}
