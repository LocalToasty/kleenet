#include "kleenet/Searcher.h"

#include "kleenet/State.h"
#include "kleenet/KleeNet.h"

#include "net/Searcher.h"
#include "net/TimeSortedSearcher.h"

#include "klee/ExecutionState.h"


using namespace kleenet;

Searcher::Searcher(KleeNet& kn, std::auto_ptr<net::Searcher> ns)
  : klee::Searcher()
  , ns(ns) {
  kn.registerSearcher(this);
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
  typedef net::StdIteratorFactory<net::BasicState*> Fac;
  if (!added.empty())
    ns->add(Fac::build(added.begin()), Fac::build(added.end()));
  if (!removed.empty())
    ns->remove(Fac::build(removed.begin()), Fac::build(removed.end()));
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
