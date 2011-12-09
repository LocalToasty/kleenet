#pragma once

#include "net/EventSearcher.h"

#include "net/TimeEvent.h"

#include <map>

namespace net {
  class PacketCacheBase;
  class CoojaInformationHandler; // pimpl

  class CoojaSearcher : public EventSearcher {
    private:
      PacketCacheBase* packetCache;
      CoojaInformationHandler& cih;
      typedef std::map<Time, TimeEvent> CalQueue;
      CalQueue calQueue;
      bool removeState(BasicState*,Node const* = NULL);
    public:
      CoojaSearcher(PacketCacheBase*);
      ~CoojaSearcher();
      bool supportsPhonyPackets() const;
      BasicState* selectState();
      bool empty() const;
      void add(ConstIteratable<BasicState*> const&, ConstIteratable<BasicState*> const&);
      void remove(ConstIteratable<BasicState*> const&, ConstIteratable<BasicState*> const&);
      void scheduleState(BasicState*, Time, EventKind);
      void yieldState(BasicState*);
      Time getStateTime(BasicState*) const;
  };
}

