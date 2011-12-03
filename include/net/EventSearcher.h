#pragma once

#include "TimeSortedSearcher.h"

#include "Time.h"

namespace net {
  class EventSearcher : public TimeSortedSearcher {
    public:
      enum EventKind {
        EK_Boot,
        EK_Normal
      };
      virtual void scheduleState(BasicState*, Time, EventKind = EK_Normal) = 0;
      virtual void yieldState(BasicState*) = 0;
      EventSearcher* toEventSearcher() {
        return this;
      }
  };
}

