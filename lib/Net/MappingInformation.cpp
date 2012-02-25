#include <assert.h>

#include "net/BasicState.h"

#include "MappingInformation.h"
#include "StateCluster.h"

using namespace net;

MappingInformation::MappingInformation()
  : Observable<MappingInformation>(this)
  , StateDependant<MappingInformation>()
  , cluster(NULL)
  , _node(Node::INVALID_NODE) {
  std::cout << "[" << this << "] MappingInformation()" << std::endl;
}
MappingInformation::MappingInformation(MappingInformation const& from)
  : Observable<MappingInformation>(this)
  , StateDependant<MappingInformation>(from)
  , cluster(from.cluster)
  , _node(from._node) {
  from.assimilate(this);
  std::cout << "[" << this << "] MappingInformation(MappingInformation const&) // node = " << _node.id << std::endl;
}
MappingInformation::~MappingInformation() {
  if (cluster) {
    cluster->depart(this);
    // We do not call change() because we are being deleted.
    // Note that the default destructor of Observable, from which
    // we inherit, will automatically inform our observers.
  }
}

StateCluster* MappingInformation::getCluster() const {
  return cluster;
}

void MappingInformation::changeCluster(StateCluster* newCluster) {
  if (cluster != newCluster) {
    if (cluster)
      cluster->depart(this);
    cluster = newCluster;
    if (cluster)
      cluster->join(this);
    change();
  }
}

Node const& MappingInformation::setNode(Node const& n) {
  assert(n >= Node::FIRST_NODE);
  std::cout << "[" << this << "] changing node " << _node.id << " -> " << n.id << std::endl;
  _node = n;
  this->change();
  return _node;
}

Node const& MappingInformation::getNode() const {
  return _node;
}
