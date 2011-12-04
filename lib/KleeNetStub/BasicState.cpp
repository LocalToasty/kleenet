#include "net/BasicState.h"

#include "fail.h"

using namespace net;

size_t& BasicState::tableSize() {
  kleenetstub::expressLinkFailure();
  static size_t x;
  return x;
}

BasicState::BasicState() : dependants(), fake(true) {
}

BasicState::BasicState(BasicState const& from) : dependants(), fake(true) {
}

BasicState::~BasicState() {}

