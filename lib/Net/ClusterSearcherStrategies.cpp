#include "net/ClusterSearcherStrategies.h"

#include <assert.h>

using namespace net;

StateCluster* NullStrategy::selectCluster() {
  if (clusters.empty())
    return NULL;
  return *clusters.begin();
}
NullStrategy& NullStrategy::operator+=(StateCluster* c) {
  clusters.push_back(c);
  return *this;
}
NullStrategy& NullStrategy::operator-=(StateCluster* c) {
  clusters.remove(c);
  return *this;
}

FifoStrategy::FifoStrategy()
  : current(clusters.end()) {
}
FifoStrategy::FifoStrategy(FifoStrategy const& from)
  : clusters(from.clusters)
  , current(clusters.end()) {
}
StateCluster* FifoStrategy::selectCluster() {
  if (clusters.empty())
    return NULL;
  if (current == clusters.end())
    current = clusters.begin();
  return *current++;
}
FifoStrategy& FifoStrategy::operator+=(StateCluster* c) {
  clusters.insert(c);
  return *this;
}
FifoStrategy& FifoStrategy::operator-=(StateCluster* c) {
  std::set<StateCluster*>::iterator it = clusters.find(c);
  if (it != clusters.end()) {
    if (it == current)
      ++current;
    clusters.erase(it);
  }
  return *this;
}


StateCluster* RandomStrategy::selectCluster() {
  unsigned const k = prng(clusterLookUp.size());
  assert(k < clusterLookUp.size());
  return clusterLookUp[k];
}
RandomStrategy& RandomStrategy::operator+=(StateCluster* c) {
  clusters[c] = clusterLookUp.size();
  clusterLookUp.push_back(c);
  return *this;
}
RandomStrategy& RandomStrategy::operator-=(StateCluster* c) {
  std::map<StateCluster*,unsigned>::iterator it = clusters.find(c);
  if (it != clusters.end()) {
    StateCluster* const replace = clusterLookUp.back();
    unsigned const gap = it->second;
    clusters[replace] = gap;
    clusterLookUp[gap] = replace;
    clusters.erase(it);
    clusterLookUp.pop_back();
  }
  return *this;
}


MangleStrategy::MangleStrategy(Components const& components)
  : components(components)
  , current(components.end())
  , remaining(0) {
  assert(!components.empty());
}
MangleStrategy::MangleStrategy(MangleStrategy const& from)
  : components(from.components)
  , current(components.end())
  , remaining(0) {
}
StateCluster* MangleStrategy::selectCluster() {
  if (!remaining) {
    ++current;
    if (current == components.end()) {
      current = components.begin();
    }
    remaining = current->second;
    assert(remaining);
  }
  remaining--;
  return current->first->selectCluster();
}
MangleStrategy& MangleStrategy::operator+=(StateCluster* c) {
  for (Components::const_iterator it = components.begin(), en = components.end(); it != en; ++it) {
    *(it->first) += c;
  }
  return *this;
}
MangleStrategy& MangleStrategy::operator-=(StateCluster* c) {
  for (Components::const_iterator it = components.begin(), en = components.end(); it != en; ++it) {
    *(it->first) -= c;
  }
  return *this;
}



RepeatStrategy::RepeatStrategy(util::SharedPtr<SearcherStrategy> s, unsigned repeat)
  : s(s)
  , repeat(repeat)
  , current(0)
  , streak(0) {
  assert(this->s);
  assert(repeat);
}
RepeatStrategy::RepeatStrategy(RepeatStrategy const& from)
  : s(from.s)
  , repeat(from.repeat)
  , current(0)
  , streak(0) {
}
StateCluster* RepeatStrategy::selectCluster() {
  if (!streak--) {
    streak = repeat;
    current = s->selectCluster();
  }
  return current;
}
RepeatStrategy& RepeatStrategy::operator+=(StateCluster* c) {
  *s += c;
  return *this;
}
RepeatStrategy& RepeatStrategy::operator-=(StateCluster* c) {
  *s -= c;
  return *this;
}
