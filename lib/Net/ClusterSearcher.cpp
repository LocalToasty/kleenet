#include "net/ClusterSearcher.h"

#include "SchedulingInformation.h"
#include "MappingInformation.h"
#include "StateCluster.h"
#include "net/ClusterSearcherStrategy.h"

#include <assert.h>
#include <vector>

namespace net {
  struct ClusterInformation : SchedulingInformation<ClusterInformation> {
    StateCluster* location;
    ClusterInformation() : location(NULL) {}
  };
  struct ClusterInformationHandler : SchedulingInformationHandler<ClusterInformation> {
    ClusterInformationHandler() : SchedulingInformationHandler<ClusterInformation>() {}
  };
}

using namespace net;

ClusterSearcher::ClusterSearcher(util::SharedPtr<SearcherStrategy> strategy)
  : cih(new ClusterInformationHandler())
  , internalSearchers()
  , strategy(strategy) {
}

ClusterSearcher::~ClusterSearcher() {
  assert(internalSearchers.empty());
}

Time ClusterSearcher::getStateTime(BasicState* state) const {
  ClusterInformation* const si(cih->stateInfo(state));
  if (si)
    return si->virtualTime;
  return Time(0); // TODO: Specify somewhere smarter!
}

void ClusterSearcher::clear() {
  internalSearchers.clear();
}

bool ClusterSearcher::empty() const {
  return internalSearchers.empty();
}

ClusterSearcher::SearcherP ClusterSearcher::of(BasicState* state) const {
  if (cih->stateInfo(state) && cih->stateInfo(state)->location) {
    InternalSearchers::const_iterator it = internalSearchers.find(cih->stateInfo(state)->location);
    assert(it != internalSearchers.end());
    return it->second;
  }
  return SearcherP(NULL);
}

void ClusterSearcher::barrier(BasicState* state) {
  of(state)->barrier(state);
}

void ClusterSearcher::scheduleStateAt(BasicState* state, Time time, EventKind ekind) {
  SearcherP const p = of(state);
  if (p && p->toEventSearcher()) {
    p->toEventSearcher()->scheduleStateAt(state,time,ekind);
  }
}
void ClusterSearcher::yieldState(BasicState* state) {
  SearcherP const p = of(state);
  if (p && p->toEventSearcher()) {
    p->toEventSearcher()->yieldState(state);
  }
}

void ClusterSearcher::operator+=(BasicState* state) {
  cih->equipState(state);
  StateCluster* c = StateCluster::of(state);
  if (c) {
    SearcherP& sr = internalSearchers[c];
    if (!sr) {
      // hey we got a new Cluster to govern, let's get us a new Low-level searcher slave
      sr = newInternalSearcher();
      assert(sr);
      cih->stateInfo(state)->location = c;
      *strategy += c;
    }
    typedef net::SingletonIterator<net::BasicState*> It;
    sr->add(It(&state),It());
  }
  // else: ignore clusterless freak
}

void ClusterSearcher::operator-=(BasicState* state) {
  if (cih->stateInfo(state)) {
    if (StateCluster* const c = cih->stateInfo(state)->location) {
      SearcherP& sr = internalSearchers[c];
      assert(sr);
      typedef net::SingletonIterator<net::BasicState*> It;
      sr->remove(It(&state),It());
      if (sr->empty()) {
        *strategy -= c;
        internalSearchers.erase(c);
      }
    }
    // else: ignore clusterless freak
    cih->releaseState(state);
  }
}

void ClusterSearcher::notify(Observable<MappingInformation>* subject) {
  BasicState* const state = subject->observed->getState();
  assert(state);
  cih->equipState(state);
  ClusterInformation* const ci = cih->stateInfo(state);
  assert(ci);
  StateCluster* const newLocation = StateCluster::of(state);
  if (ci->location != newLocation) {
    typedef net::SingletonIterator<net::BasicState*> It;
    this->remove(It(&state),It());
    this->add(It(&state),It());
  }
}

void ClusterSearcher::notifyNew(Observable<MappingInformation>* subject, Observable<MappingInformation> const* administrator) {
  subject->add(this);
}

void ClusterSearcher::notifyDie(Observable<MappingInformation> const* administrator) {
}

BasicState* ClusterSearcher::selectState() {
  StateCluster* const sc = strategy->selectCluster();
  if (!sc)
    return NULL;
  InternalSearchers::iterator sel = internalSearchers.find(sc);
  assert(sel != internalSearchers.end());
  assert(!sel->second->empty());
  return sel->second->selectState();
}

