#include "net/TimeEvent.h"

#include "MappingInformation.h"

#include <assert.h>

using namespace net;

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
  scheduledNodes[MappingInformation::retrieveDependant(es)->getNode()].push_back(es);
}

void TimeEvent::removeState(BasicState* es) {
  Node const node = MappingInformation::retrieveDependant(es)->getNode();
  removeStateOnNode(es,node);
}

void TimeEvent::removeStateOnNode(BasicState* es, Node const node) {
  for (Table::const_iterator it = scheduledNodes.begin(), en = scheduledNodes.end(); it != en; ++it)
    std::cout << "Available node for TimeEvent: " << it->first.id << std::endl;
  std::cout << "Looking for node " << node.id << std::endl;
  assert(scheduledNodes.count(node) && "the state is not scheduled, the node entry does not exist"); // TODO refactor me
  scheduledNodes[node].remove(es); // TODO refactor me
  if (scheduledNodes[node].empty())
    scheduledNodes.erase(node);
}

bool TimeEvent::empty() const {
  return scheduledNodes.empty();
}

bool TimeEvent::isNodeScheduled(Node node) {
  return scheduledNodes.count(node);
}
