#include "net/Node.h"

using namespace net;

const Node Node::FIRST_NODE = 1; // TODO: change to 0
const Node Node::INVALID_NODE = -1;

Node Node::operator++() {
  return ++id;
}

Node Node::operator++(int) {
  return id++;
}

bool Node::operator==(const Node n2) const {
  return id == n2.id;
}

bool Node::operator!=(const Node n2) const {
  return !(*this == n2);
}

bool Node::operator<(const Node n2) const {
  return id < n2.id;
}

bool Node::operator>(const Node n2) const {
  return id > n2.id;
}

bool Node::operator<=(const Node n2) const {
  return id <= n2.id;
}

bool Node::operator>=(const Node n2) const {
  return id >= n2.id;
}
