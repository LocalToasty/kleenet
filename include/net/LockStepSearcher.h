#pragma once

#include "net/TimeSortedSearcher.h"

namespace net {
  class LockStepInformationHandler; // pimpl
  class PacketCacheBase;

  class LockStepSearcher : public TimeSortedSearcher {
    private:
      PacketCacheBase* packetCache;
      Time const stepIncrement;
      LockStepInformationHandler& lsih;
    public:
      LockStepSearcher(PacketCacheBase*, Time stepIncrement = 1);
      ~LockStepSearcher();
      bool supportsPhonyPackets() const;
      BasicState* selectState();
      bool empty() const;
      void add(ConstIteratable<BasicState*> const&, ConstIteratable<BasicState*> const&);
      void remove(ConstIteratable<BasicState*> const&, ConstIteratable<BasicState*> const&);
      Time getStateTime(BasicState*) const;
  };
}

