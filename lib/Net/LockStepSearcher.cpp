#include "net/LockStepSearcher.h"

#include "SchedulingInformation.h"

#include <vector>
#include <functional>
#include <algorithm>
#include <iterator>

namespace net {
  struct LockStepInformation : SchedulingInformation<LockStepInformation> {
    std::vector<BasicState*>::size_type slot;
    LockStepInformation() : slot(-1) {}
  };
  struct LockStepInformationHandler : SchedulingInformationHandler<LockStepInformation> {
    std::vector<BasicState*> states;
    std::vector<BasicState*>::iterator next;
    std::vector<BasicState*>::iterator end;
    std::vector<BasicState*>::size_type nullSlots;
    Time globalTime;
    LockStepInformationHandler()
      : SchedulingInformationHandler<LockStepInformation>()
      , states()
      , next(states.end())
      , end(states.end())
      , nullSlots(0)
      , globalTime(0) {
    }
  };
}

using namespace net;

LockStepSearcher::LockStepSearcher(PacketCacheBase* packetCache, Time stepIncrement)
  : packetCache(packetCache)
  , stepIncrement(stepIncrement)
  , lsih(*(new LockStepInformationHandler())) {
}
LockStepSearcher::~LockStepSearcher() {
  delete &lsih;
}

Time LockStepSearcher::getStateTime(BasicState* state) const {
  LockStepInformation* const si(lsih.stateInfo(state));
  if (si)
    return si->virtualTime;
  return Time(0); // TODO: Specify somewhere smarter!
}

bool LockStepSearcher::supportsPhonyPackets() const {
  return packetCache;
}

bool LockStepSearcher::empty() const {
  return lsih.states.empty();
}
void LockStepSearcher::add(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    lsih.equipState(*it);
    lsih.stateInfo(*it)->slot = lsih.states.size();
    lsih.states.push_back(*it);
  }
}
void LockStepSearcher::remove(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    if (lsih.stateInfo(*it)) {
      assert(lsih.stateInfo(*it)->slot < lsih.states.size());
      BasicState*& sl = lsih.states[lsih.stateInfo(*it)->slot];
      assert(sl);
      assert(lsih.stateInfo(*it) == lsih.stateInfo(sl));
      lsih.releaseState(sl);
      sl = NULL;
      lsih.nullSlots++;
    }
  }
}
BasicState* LockStepSearcher::selectState() {
  for (;lsih.next != lsih.end && !(*lsih.next); lsih.next++); // fast forward junk-entries
  if (lsih.next == lsih.end) {
    if (lsih.states.size()-lsih.nullSlots < lsih.states.capacity()/4) {
      std::vector<BasicState*> replace;
      replace.reserve(lsih.states.size()-lsih.nullSlots);
      std::remove_copy_if(lsih.states.begin(),lsih.states.end(),
                          std::back_inserter(replace),
                          std::bind2nd(std::equal_to<BasicState*>(),NULL));
      // Using swap is imperative to force vector to actually SHRINK!
      // So we do this whole mumbo jumbo even if there are no holes.
      lsih.states.swap(replace);
      lsih.nullSlots = 0;
    }
    lsih.next = lsih.states.begin();
    lsih.end = lsih.states.end();
    lsih.globalTime += stepIncrement;
  }
  BasicState* const selection = *lsih.next++;
  updateLowerBound((lsih.stateInfo(selection)->virtualTime) = lsih.globalTime);
  return selection;
}
