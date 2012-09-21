#include "net/LockStepSearcher.h"

#include "SchedulingInformation.h"

#include "net/PacketCache.h"

#include <vector>
#include <functional>
#include <algorithm>
#include <iterator>

#include "net/util/debug.h"

#define DD net::DEBUG<net::debug::searchers>

namespace net {
  struct LockStepInformation : SchedulingInformation<LockStepInformation> {
    std::vector<BasicState*>::size_type slot;
    bool blocked;
    bool skipOnce;
    LockStepInformation() : slot(-1), blocked(false), skipOnce(false) {}
  };
  struct LockStepInformationHandler : SchedulingInformationHandler<LockStepInformation> {
    typedef std::vector<BasicState*> States;
    States states;
    States::iterator next;
    States::iterator end;
    States::size_type nullSlots;
    States::size_type blockedStates;
    Time globalTime;
    Time stepIncrement;
    LockStepInformationHandler(Time stepIncrement)
      : SchedulingInformationHandler<LockStepInformation>()
      , states()
      , next(states.end())
      , end(states.end())
      , nullSlots(0)
      , blockedStates(0)
      , globalTime(0)
      , stepIncrement(stepIncrement) {
    }
    States::size_type getGovernedStates() const {
      assert(states.size() >= nullSlots);
      return states.size()-nullSlots;
    }
    template <typename T>
    inline static T testAndSet(T& var, T const value) {
      T const prev = var;
      var = value;
      return prev;
    }
    void fastForwardJunk() {
      while (next != end && (!*next || testAndSet(stateInfo(*next)->skipOnce,false)))
        ++next; // fast forward junk-entries
    }
    void block(LockStepInformation* const lsi) {
      if (lsi && !lsi->blocked) {
        blockedStates++;
        lsi->blocked = true;
      }
    }
    void unblock(LockStepInformation* const lsi) {
      if (lsi && lsi->blocked) {
        blockedStates--;
        lsi->blocked = false;
      }
    }
    void unblockAll() {
      if (blockedStates)
        for (States::iterator it = states.begin(), en = states.end(); it != en; ++it) {
          if (*it)
            stateInfo(*it)->blocked = false;
        }
      blockedStates = 0;
    }
    bool consolidate() {
      fastForwardJunk();
      bool const result = next == end;
      if (result) {
        if (getGovernedStates() < states.capacity()/4) {
          std::vector<BasicState*> replace(states.size()-nullSlots,NULL);
          std::vector<BasicState*>::size_type index = 0;
          for (std::vector<BasicState*>::const_iterator it = states.begin(), en = states.end(); it != en; ++it)
            if (*it)
              replace[(stateInfo(*it)->slot = index++)] = *it;
          assert(replace.size() == index);
          // Using swap is imperative to force vector to actually SHRINK!
          // So we do this whole mumbo jumbo even if there are no holes.
          states.swap(replace);
          nullSlots = 0;
        }
        next = states.begin();
        end = states.end();
        fastForwardJunk();
        assert(next != end);
        globalTime += stepIncrement;
      }
      return result;
    }
  };
}

using namespace net;

LockStepSearcher::LockStepSearcher(PacketCacheBase* packetCache, Time stepIncrement)
  : packetCache(packetCache)
  , stepIncrement(stepIncrement)
  , lsih(*(new LockStepInformationHandler(stepIncrement))) {
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

void LockStepSearcher::barrier(BasicState* state) {
  lsih.block(lsih.stateInfo(state));
}

bool LockStepSearcher::empty() const {
  return lsih.getGovernedStates();
}
void LockStepSearcher::operator+=(BasicState* state) {
  lsih.equipState(state);
  typedef std::vector<BasicState*>::size_type Slot;
  Slot& slot = lsih.stateInfo(state)->slot;
  typedef LockStepInformationHandler::States::difference_type DiffType;
  DiffType const current = lsih.next - lsih.states.begin();
  // note "slot" is now still the slot of the parent state
  lsih.stateInfo(state)->skipOnce = (slot != static_cast<Slot>(-1)) && (static_cast<DiffType>(slot) < current);
  slot = lsih.states.size();
  if (lsih.stateInfo(state)->blocked)
    lsih.blockedStates++;
  lsih.states.push_back(state); // potentially invalidates iterators!
  lsih.next = lsih.states.begin() + current;
  lsih.end = lsih.states.end();
}
void LockStepSearcher::operator-=(BasicState* state) {
  if (lsih.stateInfo(state)) {
    assert(lsih.stateInfo(state)->slot < lsih.states.size());
    lsih.unblock(lsih.stateInfo(state));
    BasicState*& sl = lsih.states[lsih.stateInfo(state)->slot];
    assert(sl);
    assert(lsih.stateInfo(state) == lsih.stateInfo(sl));
    lsih.releaseState(sl);
    sl = NULL;
    lsih.nullSlots++;
  }
}
BasicState* LockStepSearcher::selectState() {
  if (lsih.getGovernedStates() == lsih.blockedStates) {
    lsih.unblockAll();
  }
  BasicState* selection;
  do {
    while (lsih.consolidate()) {
      if (packetCache)
        packetCache->commitMappings();
    }
    selection = *lsih.next++;
  } while (lsih.stateInfo(selection)->blocked);
  updateLowerBound((lsih.stateInfo(selection)->virtualTime) = lsih.globalTime);
  return selection;
}
