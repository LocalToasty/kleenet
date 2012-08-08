#pragma once

#include "net/EventSearcher.h"

namespace net {
  struct FakeEventHandler {
    enum FakeType {
      FT_SCHEDULE,
      FT_YIELD
    };
    virtual void operator()(FakeType) = 0;
  };
  class FakeEventSearcher : public EventSearcher {
    private:
      TimeSortedSearcher* const trueSearcher;
      FakeEventHandler* const fakeEventHandler;
    public:
      FakeEventSearcher(TimeSortedSearcher*, FakeEventHandler* = 0);
      void scheduleStateAt(BasicState* state, Time time, EventKind);
      void yieldState(BasicState*);

      bool supportsPhonyPackets() const;
      BasicState* selectState();
      bool empty() const;
      void operator+=(BasicState*);
      void operator-=(BasicState*);
      Time getStateTime(BasicState*) const;
  };
}

