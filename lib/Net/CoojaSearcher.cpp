#include "net/CoojaSearcher.h"

#include "SchedulingInformation.h"

#include "net/PacketCache.h"

#include <set>
#include <iostream>

#include "debug.h"

// TODO: check that we *actually* behave like cooja

namespace net {
  struct CoojaInformation : SchedulingInformation<CoojaInformation> {
    std::set<Time> scheduledTime;
    // danglingTimes are only used for copy construction of states
    // (to duplicate the scheduleTimes of the original state).
    // That is, these are times the state thinks it is scheduled,
    // but that it has not yet been to in our data structures.
    std::set<Time> danglingTimes;
    Time scheduledBootTime;
    CoojaInformation() : SchedulingInformation<CoojaInformation>(), scheduledTime(), scheduledBootTime(0) {
    }
    CoojaInformation(CoojaInformation const& from) : SchedulingInformation<CoojaInformation>(from), scheduledTime(), danglingTimes(from.scheduledTime), scheduledBootTime(from.scheduledBootTime) {
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
    CalQueue::iterator it = calQueue.find(*(schInfo->scheduledTime.begin()));
    if (it != calQueue.end()) {
      it->second.removeState(state);
      if (it->second.empty()) {
        if (it == calQueue.begin() && packetCache) {
          packetCache->commitMappings();
        }
        calQueue.erase(it);
      }
    }
    schInfo->scheduledTime.erase(schInfo->scheduledTime.begin());
  }
  return result;
}

void CoojaSearcher::add(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    cih.equipState(*it);
    CoojaInformation* const schInfo = cih.stateInfo(*it);
    assert(!schInfo->isScheduled() && "Newly added state is already scheduled? How?");
    // if we are forked from another state, we have to mirror its schedule times, otherwise we will have too bootstrap
    if (schInfo->danglingTimes.empty()) {
      DDEBUG std::cerr << std::endl << "New State: NO DANGLING EVENTS" << std::endl;
      scheduleStateAt(*it, schInfo->virtualTime, EK_Normal);
    } else {
      DDEBUG std::cerr << std::endl << "New State: " << schInfo->danglingTimes.size() << " DANGLING EVENTS" << std::endl;
      for (std::set<Time>::const_iterator tm = schInfo->danglingTimes.begin(), tmEnd = schInfo->danglingTimes.end(); tm != tmEnd; ++tm) {
        scheduleStateAt(*it, *tm, EK_Normal);
      }
      schInfo->danglingTimes.clear();
    }
  }
}

void CoojaSearcher::remove(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    removeState(*it);
  }
}

void CoojaSearcher::scheduleStateAt(BasicState* state, Time time, EventKind ekind) {
  CoojaInformation* schedInfo = cih.stateInfo(state);
  DDEBUG std::cerr << "Schedule request (type " << ekind << ") for state " << state << " at time " << time << std::endl;
  DDEBUG std::cerr << "Queue Size before scheduling " << calQueue.size() << std::endl;
  if (ekind == EK_Boot) {
    schedInfo->scheduledBootTime = time;
    while (removeState(state));
    //schedInfo->scheduledTime.insert(0); // XXX do we still need this? if so, why?
    //calQueue[0].pushBack(state);
  }
  if (schedInfo->isScheduled()) {
    // FIXME FIXME FIXME: Generate Error/Testcase if we schedule an event in the past (unless we havn't been booted yet)
    if (schedInfo->scheduledBootTime > time || schedInfo->virtualTime >= time) {
    //if (schedInfo->scheduledBootTime > time || schedInfo->virtualTime > time) {
      // ignore wakeup request; XXX: Why are we doing this again?
      DDEBUG std::cerr << "  ignoring schedule request at " << time << "! Reason: boot-time " << schedInfo->scheduledBootTime << ", virtual-time " << schedInfo->virtualTime << std::endl;
      return;
    }
    while (schedInfo->isScheduled() && time <= *(schedInfo->scheduledTime.begin())) {
      DDEBUG std::cerr << "  rescheduling!" << std::endl;
      // remove the state from the time event in the future
      removeState(state);
    }
  }
  DDEBUG std::cerr << "Honouring schedule request for state " << state << " at time " << time << " (i.e. was not dropped)." << std::endl;
  assert(time >= lowerBound());
  // set scheduled time
  schedInfo->scheduledTime.insert(time);
  // push the state into the fifo queue of the specified time event
  calQueue[time].pushBack(state);
  DDEBUG std::cerr << "Queue Size after scheduling " << calQueue.size() << std::endl;
}

BasicState* CoojaSearcher::selectState() {
  if (calQueue.empty()) {
    return NULL;
  }
  CalQueue::iterator head = calQueue.begin();
  BasicState* const headState = head->second.peakState();
  assert(cih.stateInfo(headState)->virtualTime <= head->first);
  if (cih.stateInfo(headState)->virtualTime != head->first) {
    DDEBUG std::cerr << "[" << cih.stateInfo(headState) << "] virtualTime := " << head->first << " was " << cih.stateInfo(headState)->virtualTime << std::endl;
  }
  cih.stateInfo(headState)->virtualTime = head->first;
  updateLowerBound(head->first);
  DDEBUG {
    static BasicState* last = NULL;
    if (last != headState) {
      DDEBUG std::cerr << std::endl << "Selecting State " << headState << " at time " << head->first << " (vt advanced to " << getStateTime(headState) << ")" << std::endl;
      last = headState;
    }
  }
  return headState;
}

void CoojaSearcher::yieldState(BasicState* bs) {
  assert(!calQueue.empty() && "Yielding state although none is active!");
  assert(cih.stateInfo(bs)->isScheduled() && "Yielding an unscheduled state!");
  DDEBUG std::cerr << "Queue Size before yielding " << calQueue.size() << std::endl;
  bool wasin = removeState(bs);
  DDEBUG std::cerr << "Queue Size after yielding " << calQueue.size() << std::endl;
  assert(wasin);
  assert(calQueue.size() && "Our FES ran empty.");
}
