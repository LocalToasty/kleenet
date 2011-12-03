#pragma once

#include <set>

#include "net/Observer.h"

namespace net {

  /// CLUSTER
  typedef unsigned ClusterId; //base type
  typedef unsigned ClusterCount;

  struct Cluster {
    ClusterId id;
    Cluster();
    Cluster(ClusterId);
    Cluster operator=(ClusterId);
    Cluster operator++();
    Cluster operator++(int);
    bool operator==(Cluster const c2) const;
    bool operator!=(Cluster const c2) const;
    bool operator< (Cluster const c2) const;
    bool operator> (Cluster const c2) const;
    bool operator<=(Cluster const c2) const;
    bool operator>=(Cluster const c2) const;
    static Cluster const FIRST_CLUSTER;
    static Cluster const INVALID_CLUSTER;
  };


  class BasicState;
  class StateCluster;
  class MappingInformation;
  class StateMapper;

  class StateClusterGate {
    friend class MappingInformation;
    private:
      void depart(MappingInformation* mi);
      void join(MappingInformation* mi);
  };

  class StateCluster : public Observable<StateCluster>, public StateClusterGate {
    friend class StateClusterGate;
    public:
      typedef std::set<MappingInformation*> ClusterMembers;
    private:
      ClusterMembers _members;
    public:
      StateMapper& mapper;
      ClusterMembers const& members;
    private:
      Cluster _cluster;
      Cluster next() const;
    public:
      Cluster const& cluster;
      StateCluster(StateMapper& mapper);
      StateCluster(StateCluster const& branchOf);
      ~StateCluster();
      static StateCluster* of(BasicState*);
      static StateCluster* of(MappingInformation*);
  };
}

