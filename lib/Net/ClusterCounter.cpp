#include "net/ClusterCounter.h"

#include "StateCluster.h"
#include "MappingInformation.h"

namespace net {
  struct ClusterCounter_impl : Observer<StateCluster> {
    virtual void notify(Observable<StateCluster>* observable) {
    }
    virtual void notifyNew(Observable<StateCluster>* observable, Observable<StateCluster> const*) {
      clusters.insert(observable->observed->cluster.id);
      observable->add(this);
      parent.change();
    }
    virtual void notifyDie(Observable<StateCluster> const* observable) {
      clusters.erase(observable->observed->cluster.id);
      parent.change();
    }
    std::set<unsigned> clusters;
    ClusterCounter& parent;
    explicit ClusterCounter_impl(ClusterCounter& parent)
      : parent(parent)
      {
    }
  };
}

using namespace net;

ClusterCounter::ClusterCounter(BasicState* rootState)
  : Observable<ClusterCounter>(this)
  , pimpl(new ClusterCounter_impl(*this))
  , clusters(pimpl->clusters)
  {
  StateCluster* sc = MappingInformation::retrieveDependant(rootState)->getCluster();
  assert(sc && "New state doesn't have cluster.");
  sc->add(pimpl);
  pimpl->notifyNew(sc,NULL);
}

ClusterCounter::~ClusterCounter() {
  assert(clusters.empty());
  delete pimpl;
}
