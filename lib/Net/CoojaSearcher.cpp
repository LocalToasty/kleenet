#include "net/CoojaSearcher.h"

#include "SchedulingInformation.h"

#include "net/PacketCache.h"

// TODO: check that we *actually* behave like cooja

namespace net {
  struct CoojaInformation : SchedulingInformation<CoojaInformation> {
    bool isScheduled;
    Time scheduledBootTime;
    Time scheduledTime;
  };
  struct CoojaInformationHandler : SchedulingInformationHandler<CoojaInformation> {
    CoojaInformationHandler() : SchedulingInformationHandler<CoojaInformation>() {}
  };
}

using namespace net;

CoojaSearcher::CoojaSearcher(PacketCacheBase* packetCache)
  : packetCache(packetCache)
  , cih(*(new CoojaInformationHandler())) {
}

CoojaSearcher::~CoojaSearcher() {
  delete &cih;
}

Time CoojaSearcher::getStateTime(BasicState* state) const {
  CoojaInformation* const si(cih.stateInfo(state));
  if (si)
    return si->virtualTime;
  return Time(0); // TODO: Specify somewhere smarter!
}

bool CoojaSearcher::supportsPhonyPackets() const {
  return packetCache;
}

bool CoojaSearcher::empty() const {
  return calQueue.empty();
}

bool CoojaSearcher::removeState(BasicState* state) {
  CoojaInformation* schInfo = cih.stateInfo(state);
  bool result = schInfo->isScheduled;
  if (schInfo->isScheduled) {
    CalQueue::iterator it = calQueue.find(schInfo->scheduledTime);
    if (it != calQueue.end()) {
      it->second.removeState(state);
      if (it->second.empty()) {
        if (it == calQueue.begin() && packetCache) {
          packetCache->commitMappings();
        }
        calQueue.erase(it);
      }
    }
  }
  schInfo->isScheduled = false;
  return result;
}

void CoojaSearcher::add(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    cih.equipState(*it);
    CoojaInformation* schInfo = cih.stateInfo(*it);
    schInfo->scheduledTime = 0;
    assert(!schInfo->isScheduled && "Newly added state is already scheduled? How?");
    scheduleState(*it, schInfo->virtualTime, EK_Normal); // FIXME: Do we really have to pass Normal? Or do we need Boot?
  }
}

void CoojaSearcher::remove(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    removeState(*it);
  }
}

void CoojaSearcher::scheduleState(BasicState* state, Time time, EventKind ekind) {
  CoojaInformation* schedInfo = cih.stateInfo(state);
  assert(time >= lowerBound());
  if (ekind == EK_Boot)
    schedInfo->scheduledBootTime = time;
  if (schedInfo->isScheduled) {
    if (schedInfo->scheduledBootTime > time || schedInfo->virtualTime >= time) {
      // ignore wakeup request; XXX: Why are we doing this again?
      return;
    }
    if (time < schedInfo->scheduledTime) {
      // remove the state from the time event in the future
      removeState(state);
    }
  }
  schedInfo->isScheduled = true;
  // set scheduled time
  schedInfo->scheduledTime = time;
  // push the state into the fifo queue of the specified time event
  calQueue[time].pushBack(state);
}

BasicState* CoojaSearcher::selectState() {
  if (calQueue.empty()) {
    return NULL;
  }
  CalQueue::iterator head = calQueue.begin();
  BasicState* const headState = head->second.peakState();
  cih.stateInfo(headState)->virtualTime = head->first;
  updateLowerBound(head->first);
  return headState;
}

void CoojaSearcher::yieldState(BasicState* bs) {
  assert(!calQueue.empty() && "Yielding state although none is active!");
  bool wasin = removeState(bs);
  assert(wasin);
}
