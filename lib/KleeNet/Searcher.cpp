#include "kleenet/Searcher.h"

#include "kleenet/State.h"
#include "kleenet/KleeNet.h"

#include "net/Searcher.h"
#include "net/TimeSortedSearcher.h"

#include "klee/ExecutionState.h"


using namespace kleenet;

KleeNet* Searcher::globalKleenet = NULL;

Searcher::Searcher(std::auto_ptr<net::Searcher> ns)
  : klee::Searcher()
  , ns(ns) {
  if (globalKleenet)
    globalKleenet->newSearcher(this);
}

Searcher::~Searcher() {
}

net::Searcher* Searcher::netSearcher() const {
  return ns.get();
}

klee::ExecutionState& Searcher::selectState() {
  klee::ExecutionState* const state = static_cast<klee::ExecutionState*>(ns->selectState());
  assert(state && "The selected searcher probably ran out of states and panicked by returning NULL. This could be caused by yielding all states without them being scheduled.");
  return *state;
}
void Searcher::update(klee::ExecutionState* current, std::set<klee::ExecutionState*> const& added, std::set<klee::ExecutionState*> const& removed) {
  typedef net::StdConstIterator<net::BasicState*,std::set<klee::ExecutionState*> > It;
  if (!added.empty())
    ns->add(It(added.begin()), It(added.end()));
  if (!removed.empty())
    ns->remove(It(removed.begin()), It(removed.end()));
}
bool Searcher::empty() {
  return ns->empty();
}

net::Time Searcher::getStateTime(State* state) const {
  return ns->getStateTime(state);
}
net::Time Searcher::getStateTime(State& state) const {
  return getStateTime(&state);
}
