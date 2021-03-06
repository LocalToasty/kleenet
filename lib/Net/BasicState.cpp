#include "net/BasicState.h"

#include "StateDependant.h"

#include "util/ObjectProperties.h"

#include <assert.h>

using namespace net;

size_t& BasicState::tableSize() {
  // avoid static initialisation order fiasco!
  static size_t _tableSize = 0;
  return _tableSize;
}

BasicState::BasicState()
  : dependants(tableSize(),NULL), fake(util::isOnStack(this)), completedTransmissions(0), completedPullRequests(0) {
}

bool BasicState::isFake() const {
  return fake;
}

size_t BasicState::getCompletedTransmissions() const {
  return completedTransmissions;
}
void BasicState::incCompletedTransmissions() {
  completedTransmissions++;
}

size_t BasicState::getCompletedPullRequests() const {
  return completedPullRequests;
}
void BasicState::incCompletedPullRequests() {
  completedPullRequests++;
}

BasicState::BasicState(BasicState const& from)
  : dependants(tableSize(), NULL), fake(util::isOnStack(this)), completedTransmissions(from.completedTransmissions), completedPullRequests(from.completedPullRequests) {

  assert((fake || !from.fake) && "Attempt to create a non-fake state from a fake state.");
  assert(from.dependants.size() == tableSize() && "Table size was changed after initialisation");
  if (!fake) {
    //std::cout << "BasicState " << this << " is not a fake." << std::endl;
    for (size_t i = 0; i < tableSize(); i++) {
      if (StateDependantI* const dep = from.dependants[i]) {
        if (StateDependantI* const cloned = static_cast<StateDependantI*>(dep->getCloner()(dep))) {
          // This will automatically set dependants[i] to the new StateDepedantI*
          cloned->setState(this);
        }
      }
    }
  }
}

BasicState::~BasicState() {
  //std::cout << "Destroying BasicState " << this << std::endl; // XXX
  for (size_t i = 0; i < tableSize(); i++) {
    StateDependantI* const dep(dependants[i]);
    if (dep) {
      assert(!fake && "A fake state should not have dependants!");
      delete dep;
    }
  }
}
