#include "StateDependant.h"

#include "net/BasicState.h"

using namespace net;

RegisterChildDependant::RegisterChildDependant()
  : id(BasicState::tableSize()++)
  , pending(0) {
}

void RegisterChildDependant::doRegister(StateDependantDelayable* dep, BasicState* state) {
  assert(state && dep && "Cannot register NULL or on NULL");
  assert(!state->dependants[id] && "Dependant already registered on this slot");
  state->dependants[id] = dep;
  if (dep->pending) {
    dep->pending = false;
    pending--;
  }
}

void RegisterChildDependant::unRegister(StateDependantDelayable* dep, BasicState* state) {
  assert(state && dep && "Cannot unregister NULL or from NULL");
  assert(state->dependants[id] == dep && "Cannot unregister foreign dependant");
  state->dependants[id] = NULL;
}

void RegisterChildDependant::delayedConstruction(StateDependantDelayable* dep) {
  assert(!dep->pending && "Cannot delay an already pending object. Look for double calls.");
  dep->pending = true;
  pending++;
}

StateDependantI* RegisterChildDependant::retrieve(BasicState const* state) const {
  assert(state && "Cannot retrieve dependant from NULL");
  assert(!pending && "Cannot retrieve StateDependant while there are pending object constructions.");
  return state->dependants[id];
}

StateDependantDelayable::StateDependantDelayable() : pending(false) {
}

void StateDependantDelayable::onStateBranch() {
}
