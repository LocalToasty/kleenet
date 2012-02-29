#include "net/TimeEvent.h"

#include "MappingInformation.h"

#include "StateDependant.h"
#include "net/Node.h"

#include <assert.h>

#include "debug.h"

using namespace net;

class TimeEventNodeSlot : public StateDependant<TimeEventNodeSlot> {
  friend class Cloner<TimeEventNodeSlot>;
  private:
    Node node;
    size_t events;
    TimeEventNodeSlot(BasicState* bs)
      : StateDependant<TimeEventNodeSlot>(bs), node(Node::INVALID_NODE), events(0) {
      setCloner(&Cloner<TimeEventNodeSlot>::getCloner());
    }
    TimeEventNodeSlot(TimeEventNodeSlot const& from) // called by Cloner
      : StateDependant<TimeEventNodeSlot>(from), node(Node::INVALID_NODE), events(0) {
    }
  public:
    enum EventChange {
      EC_less = -1,
      EC_none =  0,
      EC_more = +1
    };
    static TimeEventNodeSlot* getSlot(BasicState* state) {
      TimeEventNodeSlot* const ns = retrieveDependant(state);
      return ns?ns:(new TimeEventNodeSlot(state));
      // We will not delete this ourself, because it is shared by all TimeEvents.
      // It will be automatically released by BasicState::~BasicState.
    }
    Node getNode(EventChange ec = EC_none) { // not const !
      if (!events) {
        MappingInformation* const mi = MappingInformation::retrieveDependant(getState());
        node = mi?(mi->getNode()):(Node::INVALID_NODE);
      }
      events += ec;
      return node;
    }
};


TimeEvent::TimeEvent() {
}

TimeEvent::~TimeEvent() {
}

BasicState* TimeEvent::peakState() {
  Table::iterator it = scheduledNodes.begin();
  assert(it != scheduledNodes.end() && "no nodes are scheduled, the map is empty");
  assert(!(*it).second.empty() && "no node's state(s) are scheduled, the list is empty");
  return (*it).second.front();
}

void TimeEvent::popState() {
  Table::iterator it = scheduledNodes.begin();
  assert(it != scheduledNodes.end() && "no nodes are scheduled, the map is empty");
  assert(!(*it).second.empty() && "no node's state(s) are scheduled, the list is empty");
  (*it).second.pop_front();
  if ((*it).second.empty())
    scheduledNodes.erase(it);
}

void TimeEvent::pushBack(BasicState* es) {
  Node const node = TimeEventNodeSlot::getSlot(es)->getNode(TimeEventNodeSlot::EC_more);
  DDEBUG std::cerr << "Pushing state " << es << " on node " << node.id << "\t (total states here: " << scheduledNodes[node].size() << ")" << std::endl;
  scheduledNodes[node].push_back(es);
  DDEBUG std::cerr << "        After " << es << "         " << node.id << "\t (total states here: " << scheduledNodes[node].size() << ") .. " << scheduledNodes.size() << " nodes known in total" << std::endl;
}

void TimeEvent::removeState(BasicState* es) {
  Node const node = TimeEventNodeSlot::getSlot(es)->getNode(TimeEventNodeSlot::EC_less);
  DDEBUG std::cerr << "Removing BS " << es << " from Node " << node.id << "\t (total states here: " << scheduledNodes[node].size() << ")" << std::endl;
  assert(scheduledNodes[node].size() && "the state is not scheduled, the node entry does not exist"); // TODO refactor me
  scheduledNodes[node].remove(es); // TODO refactor me
  DDEBUG std::cerr << "      After " << es << "           " << node.id << "\t (total states here: " << scheduledNodes[node].size() << ")";
  if (scheduledNodes[node].empty())
    scheduledNodes.erase(node);
  DDEBUG std::cerr << " .. " << scheduledNodes.size() << " nodes known in total" << std::endl;
}

bool TimeEvent::empty() const {
  return scheduledNodes.empty();
}

bool TimeEvent::isNodeScheduled(Node node) {
  return scheduledNodes.find(node) != scheduledNodes.end();
}
