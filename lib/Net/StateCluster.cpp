#include "StateCluster.h"

#include "net/StateMapper.h"
#include "MappingInformation.h"

#include "net/util/debug.h"

#define DD DEBUG<debug::clusters>

using namespace net;

Cluster const Cluster::FIRST_CLUSTER = 0;
Cluster const Cluster::INVALID_CLUSTER = (ClusterId)-1;

Cluster::Cluster() : id(INVALID_CLUSTER.id) {}
Cluster::Cluster(ClusterId id) : id(id) {}
Cluster Cluster::operator=(ClusterId _id) { return id = _id; }
Cluster Cluster::operator++() { return ++id; }
Cluster Cluster::operator++(int) { return id++; }
bool Cluster::operator==(Cluster const c2) const { return id == c2.id; }
bool Cluster::operator!=(Cluster const c2) const { return id != c2.id; }
bool Cluster::operator< (Cluster const c2) const { return id <  c2.id; }
bool Cluster::operator> (Cluster const c2) const { return id >  c2.id; }
bool Cluster::operator<=(Cluster const c2) const { return id <= c2.id; }
bool Cluster::operator>=(Cluster const c2) const { return id >= c2.id; }

namespace net {
  class ClusterAdministrator {
    friend class StateCluster;
    private:
      ClusterId nextClusterId;
      // This is used to recycle old cluster ids.
      std::deque<ClusterId> clusterIdGaps;
      ClusterAdministrator()
        : nextClusterId(Cluster::FIRST_CLUSTER.id) {
      }
  };
}

void StateClusterGate::depart(MappingInformation* mi) {
  StateCluster* const self = static_cast<StateCluster*>(this);
  self->_members.erase(mi);
  self->change();
}

void StateClusterGate::join(MappingInformation* mi) {
  StateCluster* const self = static_cast<StateCluster*>(this);
  self->_members.insert(mi);
  self->change();
}

Cluster StateCluster::next() const {
  Cluster c;
  if (admin->clusterIdGaps.empty()) {
    c = Cluster(admin->nextClusterId++);
  } else {
    c = admin->clusterIdGaps.front();
    admin->clusterIdGaps.pop_front();
  }
  return c;
}


StateCluster::StateCluster()
  : Observable<StateCluster>(this)
  , admin(new ClusterAdministrator())
  , members(_members)
  , _cluster(next())
  , cluster(_cluster) {
  DD::cout << "+StateCluster: " << _cluster.id << DD::endl;
}

StateCluster::StateCluster(StateCluster const& branchOf)
  : Observable<StateCluster>(this)
  , admin(branchOf.admin)
  , members(_members)
  , _cluster(next())
  , cluster(_cluster) {
  branchOf.assimilate(this);
  DD::cout << "+StateCluster: " << _cluster.id << DD::endl;
}

StateCluster::~StateCluster() {
  admin->clusterIdGaps.push_back(cluster.id);
  DD::cout << "~StateCluster: " << _cluster.id << DD::endl;
}

StateCluster* StateCluster::of(BasicState* state) {
  if (!state)
    return NULL;
  MappingInformation* mi = MappingInformation::retrieveDependant(state);
  return of(mi);
}

StateCluster* StateCluster::of(MappingInformation* mi) {
  if (!mi)
    return NULL;
  return mi->getCluster();
}
