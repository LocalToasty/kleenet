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
