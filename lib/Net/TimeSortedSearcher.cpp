#include "net/TimeSortedSearcher.h"

#include "SchedulingInformation.h"

#include <assert.h>

using namespace net;

TimeSortedSearcher::TimeSortedSearcher()
  : lastLowerBound(0) {
}

void TimeSortedSearcher::updateLowerBound(Time newLB) {
  assert(newLB >= lastLowerBound);
  lastLowerBound = newLB;
}

Time TimeSortedSearcher::lowerBound() const {
  return lastLowerBound;
}
