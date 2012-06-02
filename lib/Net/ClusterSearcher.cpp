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

void ClusterSearcher::add(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  typedef std::map<SearcherP,std::vector<BasicState*> > Cache;
  Cache cache;

  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    cih->equipState(*it);
    StateCluster* c = StateCluster::of(*it);
    if (c) {
      SearcherP& sr = internalSearchers[c];
      if (!sr) {
        // hey we got a new Cluster to govern, let's get us a new Low-level searcher slave
        sr = newInternalSearcher();
        assert(sr);
        cih->stateInfo(*it)->location = c;
        *strategy += c;
      }
      // The following, commented out, version is the straight-forward
      // method to add the new states to our slaves. However, our slaves
      // may be able to perform mass add significantly better than many
      // single add operations (think of vector resizing).
      //typedef net::SingletonIterator<net::BasicState*> It;
      //sr->add(It(&*it),It());
      cache[sr].push_back(*it);
    }
    // else: ignore clusterless freak
  }

  for (Cache::const_iterator it = cache.begin(), en = cache.end(); it != en; ++it) {
    typedef net::StdConstIterator<net::BasicState*,std::vector<BasicState*> > It;
    it->first->add(It(it->second.begin()),It(it->second.end()));
  }
}
void ClusterSearcher::remove(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  typedef std::map<SearcherP,std::pair<StateCluster*,std::vector<BasicState*> > > Cache;
  Cache cache;

  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    if (cih->stateInfo(*it)) {
      StateCluster* const c = cih->stateInfo(*it)->location;
      if (c) {
        SearcherP& sr = internalSearchers[c];
        assert(sr);
        cache[sr].first = c;
        cache[sr].second.push_back(*it);
      }
      // else: ignore clusterless freak
      cih->releaseState(*it);
    }
  }

  for (Cache::const_iterator it = cache.begin(), en = cache.end(); it != en; ++it) {
    typedef net::StdConstIterator<net::BasicState*,std::vector<BasicState*> > It;
    it->first->remove(It(it->second.second.begin()),It(it->second.second.end()));
    if (it->first->empty()) {
      *strategy -= it->second.first;
      internalSearchers.erase(it->second.first);
    }
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
    remove(It(&state),It());
    add(It(&state),It());
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

