#include "net/Searcher.h"

#include "klee_headers/Common.h"

using namespace net;

Searcher::~Searcher() {
}

EventSearcher* Searcher::toEventSearcher() {
  return 0;
}

void Searcher::barrier(BasicState* bs) {
  klee::klee_warning("Using barriers on a Net Searcher that doesn't support barriers. Ignoring your request.");
}

void Searcher::add(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    *this += *it;
  }
}
void Searcher::remove(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    *this -= *it;
  }
}
