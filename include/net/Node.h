#pragma once

namespace net {

  typedef int NodeId; // base type
  typedef unsigned int NodeCount;

  struct Node {
    NodeId id;
    Node();
    Node(NodeId);
    Node operator=(NodeId);
    Node operator++();
    Node operator++(int);
    bool operator==(const Node) const;
    bool operator!=(const Node) const;
    bool operator<(const Node) const;
    bool operator>(const Node) const;
    bool operator<=(const Node) const;
    bool operator>=(const Node) const;
    static const Node FIRST_NODE;
    static const Node INVALID_NODE;
  };
}

