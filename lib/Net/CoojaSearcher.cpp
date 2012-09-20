#include "net/CoojaSearcher.h"

#include "SchedulingInformation.h"

#include "net/PacketCache.h"

#include <set>

#include "net/util/debug.h"

#define DD DEBUG<debug::searchers>

// TODO: check that we *actually* behave like cooja

namespace net {
  struct CoojaInformation : SchedulingInformation<CoojaInformation> {
    std::set<Time> scheduledTime;
    // danglingTimes are only used for copy construction of states
    // (to duplicate the scheduleTimes of the original state).
    // That is, these are times the state thinks it is scheduled,
    // but that it has not yet been to in our data structures.
    std::set<Time> danglingTimes;
    bool isDangling;
    Time scheduledBootTime;
    CoojaInformation()
      : SchedulingInformation<CoojaInformation>()
      , scheduledTime()
      , danglingTimes()
      , isDangling(true)
      , scheduledBootTime(0)
      {
    }
    CoojaInformation(CoojaInformation const& from)
      : SchedulingInformation<CoojaInformation>(from)
      , scheduledTime()
      , danglingTimes(from.scheduledTime)
      , isDangling(true)
      , scheduledBootTime(from.scheduledBootTime)
      {
    }
    bool isScheduled() {
      return !scheduledTime.empty();
    }
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
  bool const result = schInfo->isScheduled();
  if (result) {
    bool needCommit = false;
    CalQueue::iterator it = calQueue.find(*(schInfo->scheduledTime.begin()));
    if (it != calQueue.end()) {
      it->second.removeState(state);
      if (it->second.empty()) {
        needCommit = (it == calQueue.begin()) && packetCache;
        calQueue.erase(it);
      }
    }
    schInfo->scheduledTime.erase(schInfo->scheduledTime.begin());
    if (needCommit)
      packetCache->commitMappings();
  }
  return result;
}

void CoojaSearcher::operator+=(BasicState* state) {
  cih.equipState(state);
  CoojaInformation* const schInfo = cih.stateInfo(state);
  assert(schInfo->isDangling && "Cannot add already known states.");
  assert(!schInfo->isScheduled() && "Newly added state is already scheduled? How?");
  // if we are forked from another state, we have to mirror its schedule times, otherwise we will have to bootstrap
  schInfo->isDangling = false;
  if (schInfo->danglingTimes.empty()) {
    DD::cout << DD::endl << "New State: NO DANGLING EVENTS" << DD::endl;
    scheduleStateAt(state, schInfo->virtualTime, EK_Normal);
  } else {
    DD::cout << DD::endl << "New State: " << schInfo->danglingTimes.size() << " DANGLING EVENTS" << DD::endl;
    std::set<Time> d;
    d.swap(schInfo->danglingTimes);
    for (std::set<Time>::const_iterator tm = d.begin(), tmEnd = d.end(); tm != tmEnd; ++tm) {
      scheduleStateAt(state, *tm, EK_Normal);
    }
  }
}

void CoojaSearcher::operator-=(BasicState* state) {
  removeState(state);
  cih.stateInfo(state)->isDangling = true;
}

void CoojaSearcher::scheduleStateAt(BasicState* state, Time time, EventKind ekind) {
  CoojaInformation* schedInfo = cih.stateInfo(state);
  DD::cout << "Schedule request (type " << ekind << ") for state " << state << " with SI " << schedInfo << " at time " << time << " [" << (schedInfo->isDangling?"IS DANGLING":"not dangling") << "]" << DD::endl;
  DD::cout << "Queue Size before scheduling " << calQueue.size() << DD::endl;
  if (ekind == EK_Boot) {
    schedInfo->scheduledBootTime = time;
    while (removeState(state)){}
  }
  if (schedInfo->isDangling) {
    schedInfo->danglingTimes.insert(time);
  } else {
    if (schedInfo->isScheduled()) {
      // FIXME FIXME FIXME: Generate Error/Testcase if we schedule an event in the past (unless we havn't been booted yet)
      if (schedInfo->scheduledBootTime > time || schedInfo->virtualTime >= time) {
        // ignore wakeup request; XXX: Why are we doing this again?
        DD::cout << "  ignoring schedule request at " << time << "! Reason: boot-time " << schedInfo->scheduledBootTime << ", virtual-time " << schedInfo->virtualTime << DD::endl;
        return;
      }
      while (schedInfo->isScheduled() && time <= *(schedInfo->scheduledTime.begin())) {
        DD::cout << "  rescheduling!" << DD::endl;
        // remove the state from the time event in the future
        removeState(state);
      }
    }
    DD::cout << "Honouring schedule request for state " << state << " at time " << time << " (i.e. was not dropped)." << DD::endl;
    assert(time >= lowerBound());
    // set scheduled time
    schedInfo->scheduledTime.insert(time);
    // push the state into the fifo queue of the specified time event
    calQueue[time].pushBack(state);
    DD::cout << "Queue Size after scheduling " << calQueue.size() << DD::endl;
  }
}

BasicState* CoojaSearcher::selectState() {
  if (calQueue.empty()) {
    return NULL;
  }
  CalQueue::iterator head = calQueue.begin();
  BasicState* const headState = head->second.peakState();
  assert(cih.stateInfo(headState)->virtualTime <= head->first);
  if (cih.stateInfo(headState)->virtualTime != head->first) {
    DD::cout << "[" << cih.stateInfo(headState) << "] virtualTime := " << head->first << " was " << cih.stateInfo(headState)->virtualTime << DD::endl;
  }
  cih.stateInfo(headState)->virtualTime = head->first;
  updateLowerBound(head->first);
  {
    static BasicState* last = NULL;
    if (last != headState) {
      DD::cout << DD::endl << "Selecting State " << headState << " at time " << head->first << " (vt advanced to " << getStateTime(headState) << ")" << DD::endl;
      last = headState;
    }
  }
  return headState;
}

void CoojaSearcher::yieldState(BasicState* bs) {
  assert(!calQueue.empty() && "Yielding state although none is active!");
  assert(cih.stateInfo(bs)->isScheduled() && "Yielding an unscheduled state!");
  DD::cout << "Queue Size before yielding " << calQueue.size() << DD::endl;
  bool wasin = removeState(bs);
  DD::cout << "Queue Size after yielding " << calQueue.size() << DD::endl;
  assert(wasin);
  assert(calQueue.size() && "Our FES ran empty.");
}
