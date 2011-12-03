#include "StateCluster.h"

#include "net/StateMapper.h"
#include "MappingInformation.h"

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
  if (mapper.clusterIdGaps.empty()) {
    c = Cluster(mapper.nextClusterId++);
  } else {
    c = mapper.clusterIdGaps.front();
    mapper.clusterIdGaps.pop_front();
  }
  return c;
}


StateCluster::StateCluster(StateMapper& mapper)
  : Observable<StateCluster>(this)
  , mapper(mapper)
  , members(_members)
  , _cluster(next())
  , cluster(_cluster) {
}

StateCluster::StateCluster(StateCluster const& branchOf)
  : Observable<StateCluster>(this)
  , mapper(branchOf.mapper)
  , members(_members)
  , _cluster(next())
  , cluster(_cluster) {
  branchOf.assimilate(this);
}

StateCluster::~StateCluster() {
  mapper.clusterIdGaps.push_back(cluster.id);
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
