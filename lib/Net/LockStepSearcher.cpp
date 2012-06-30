#include "net/LockStepSearcher.h"

#include "SchedulingInformation.h"

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
    LockStepInformation() : slot(-1), blocked(false) {}
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
    void fastForwardJunk() {
      for (;next != end && !*next; ++next); // fast forward junk-entries
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
    void consolidate() {
      fastForwardJunk();
      if (next == end) {
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
void LockStepSearcher::add(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    lsih.equipState(*it);
    lsih.stateInfo(*it)->slot = lsih.states.size();
    if (lsih.stateInfo(*it)->blocked)
      lsih.blockedStates++;
    LockStepInformationHandler::States::difference_type const current = lsih.next - lsih.states.begin();
    lsih.states.push_back(*it); // potentially invalidates iterators!
    lsih.next = lsih.states.begin() + current;
    lsih.end = lsih.states.end();
  }
}
void LockStepSearcher::remove(ConstIteratable<BasicState*> const& begin, ConstIteratable<BasicState*> const& end) {
  for (ConstIteratorHolder<BasicState*> it = begin; it != end; ++it) {
    if (lsih.stateInfo(*it)) {
      assert(lsih.stateInfo(*it)->slot < lsih.states.size());
      lsih.unblock(lsih.stateInfo(*it));
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
  if (lsih.getGovernedStates() == lsih.blockedStates) {
    lsih.unblockAll();
  }
  BasicState* selection;
  do {
    lsih.consolidate();
    selection = *lsih.next++;
  } while (lsih.stateInfo(selection)->blocked);
  updateLowerBound((lsih.stateInfo(selection)->virtualTime) = lsih.globalTime);
  return selection;
}
