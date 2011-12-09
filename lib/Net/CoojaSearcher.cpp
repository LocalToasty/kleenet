#include "net/CoojaSearcher.h"

#include "SchedulingInformation.h"

#include "net/PacketCache.h"

#include <set>

// TODO: check that we *actually* behave like cooja

namespace net {
  struct CoojaInformation : SchedulingInformation<CoojaInformation> {
    std::set<Time> scheduledTime;
    Time scheduledBootTime;
    CoojaInformation() : SchedulingInformation<CoojaInformation>(), scheduledTime(), scheduledBootTime(0) {
    }
    CoojaInformation(CoojaInformation const& from) : SchedulingInformation<CoojaInformation>(from), scheduledTime(), scheduledBootTime(0) {
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

bool CoojaSearcher::removeState(BasicState* state, Node const* node) {
  CoojaInformation* schInfo = cih.stateInfo(state);
  bool const result = schInfo->isScheduled();
  if (result) {
    CalQueue::iterator it = calQueue.find(*(schInfo->scheduledTime.begin()));
    if (it != calQueue.end()) {
      if (node) {
        it->second.removeStateOnNode(state,*node);
      } else {
        it->second.removeState(state);
      }
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
  //for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
  //  std::cout << "Got new BS: " << *it << std::endl;
  //}
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    cih.equipState(*it);
    CoojaInformation* schInfo = cih.stateInfo(*it);
    assert(!schInfo->isScheduled() && "Newly added state is already scheduled? How?");
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
  std::cout << "Schedule request for node " << state << " at time " << time << std::endl;
  std::cout << "Queue Size before scheduling " << calQueue.size() << std::endl;
  if (ekind == EK_Boot) {
    schedInfo->scheduledBootTime = time;
    while (removeState(state,&Node::INVALID_NODE));
  }
  if (schedInfo->isScheduled()) {
    if (schedInfo->scheduledBootTime > time || schedInfo->virtualTime >= time) {
      // ignore wakeup request; XXX: Why are we doing this again?
      std::cout << "  ignoring schedule request!" << std::endl;
      return;
    }
    while (schedInfo->isScheduled() && time <= *(schedInfo->scheduledTime.begin())) {
      std::cout << "  rescheduling!" << std::endl;
      // remove the state from the time event in the future
      removeState(state);
    }
  }
  // set scheduled time
  schedInfo->scheduledTime.insert(time);
  // push the state into the fifo queue of the specified time event
  calQueue[time].pushBack(state);
  std::cout << "Queue Size after scheduling " << calQueue.size() << std::endl;
}

BasicState* CoojaSearcher::selectState() {
  if (calQueue.empty()) {
    return NULL;
  }
  CalQueue::iterator head = calQueue.begin();
  BasicState* const headState = head->second.peakState();
  cih.stateInfo(headState)->virtualTime = head->first;
  updateLowerBound(head->first);
  /*XXX*/static BasicState* last = NULL;
  /*XXX*/if (last != headState) {
  /*XXX*/  std::cout << "Selecting State " << headState << std::endl;
  /*XXX*/  last = headState;
  /*XXX*/}
  return headState;
}

void CoojaSearcher::yieldState(BasicState* bs) {
  assert(!calQueue.empty() && "Yielding state although none is active!");
  assert(cih.stateInfo(bs)->isScheduled() && "Yielding an unscheduled state!");
  std::cout << "Queue Size before yielding " << calQueue.size() << std::endl;
  bool wasin = removeState(bs);
  std::cout << "Queue Size after yielding " << calQueue.size() << std::endl;
  assert(wasin);
}
