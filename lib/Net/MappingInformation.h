#pragma once

#include "StateDependant.h"

#include "net/Observer.h"
#include "net/Node.h"

namespace net {
  class StateMapper;
  class StateCluster;

  // derive me
  class MappingInformation : public Observable<MappingInformation>, public StateDependant<MappingInformation> {
    friend class StateMapper;
    private:
      // for our friends
      using StateDependant<MappingInformation>::setState;
    protected:
      MappingInformation();
      StateCluster* cluster;
    public:
      StateCluster* getCluster() const;
      void changeCluster(StateCluster* newCluster);
      using StateDependant<MappingInformation>::getState;
    private:
      Node _node; // do not mutate this, unless you are setNode!
    public:
      MappingInformation(MappingInformation const& from);
      virtual Node const& setNode(Node const& n);
      virtual Node const& getNode() const;
      virtual ~MappingInformation();
  };

}
