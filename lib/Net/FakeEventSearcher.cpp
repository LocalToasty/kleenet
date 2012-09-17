#include "FakeEventSearcher.h"

#include <assert.h>

using namespace net;

FakeEventSearcher::FakeEventSearcher(TimeSortedSearcher* trueSearcher, FakeEventHandler* fakeEventHandler)
  : trueSearcher(trueSearcher)
  , fakeEventHandler(fakeEventHandler) {
  assert(trueSearcher);
}
void FakeEventSearcher::scheduleStateAt(BasicState* state, Time time, EventKind ek) {
  if (fakeEventHandler)
    (*fakeEventHandler)(FakeEventHandler::FT_SCHEDULE);
}
void FakeEventSearcher::yieldState(BasicState* state) {
  if (fakeEventHandler)
    (*fakeEventHandler)(FakeEventHandler::FT_YIELD);
}

Time FakeEventSearcher::getStateTime(BasicState* state) const {
  return Time(0); // We do not have a time!
}


bool FakeEventSearcher::supportsPhonyPackets() const {
  return trueSearcher->supportsPhonyPackets();
}

bool FakeEventSearcher::empty() const {
  return trueSearcher->empty();
}

void FakeEventSearcher::operator+=(BasicState* state) {
  typedef net::SingletonIterator<net::BasicState*> It;
  trueSearcher->add(It(&state),It());
}

void FakeEventSearcher::operator-=(BasicState* state) {
  typedef net::SingletonIterator<net::BasicState*> It;
  trueSearcher->remove(It(&state),It());
}

BasicState* FakeEventSearcher::selectState() {
  return trueSearcher->selectState();
}
