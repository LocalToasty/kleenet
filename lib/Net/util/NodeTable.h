#pragma once

#include "net/Node.h"

#include <cassert>
#include <vector>
#include <map>

namespace net {
  namespace util {

    template <class T> class NodeTable {
      private:
        typedef typename std::vector<T> N;
        N nodes;
      protected:
        virtual void onResize() {}
        virtual void onExplicitResize(NodeCount newCount) {
          onResize();
        }
        virtual void onAutoResize(Node intrude) {
          onResize();
        }
      public:
        typedef typename N::iterator iterator;
        NodeTable() : nodes(0) {}
        NodeTable(NodeCount nc) : nodes(nc) {}
        NodeTable(NodeTable const& nt) : nodes(nt.nodes) {}
        virtual ~NodeTable() {}
        void resize (NodeCount nc) {
          assert(nc >= nodes.size() && "Removing nodes not supported.");
          onExplicitResize(nc);
          nodes.resize(nc);
        }
        NodeCount size() const {
          return nodes.size();
        }
        iterator begin() {
          return nodes.begin();
        }
        iterator end() {
          return nodes.end();
        }
        T& operator[](Node const n) {
          assert(n.id >= Node::FIRST_NODE.id);
          const size_t id = (size_t)(n.id - Node::FIRST_NODE.id);
          if (id >= nodes.size()) {
            // autoresize nodes
            onAutoResize(n);
            nodes.resize(id + 1);
          }
          return nodes[id];
        }
    };

    template <class T> class LockableNodeTable : public NodeTable<T> {
      public:
        bool& allowResize;
      protected:
        virtual void onResize() {
          // prohibit node joins after lock
          assert(allowResize);
        }
      public:
        void lock() {
          allowResize = false;
        }
        bool isLocked() {
          return !allowResize;
        }
        LockableNodeTable(NodeCount nc, bool& allowResize) : NodeTable<T>(nc), allowResize(allowResize) {}
        LockableNodeTable(LockableNodeTable const& nt) : NodeTable<T>(nt), allowResize(nt.allowResize) {}
    };

    template <typename A, typename B, typename Result>
    Result innerProduct(const std::map<A, std::map<B, Result> > &m) {
      Result sum = 0;
      for (typename std::map<A, std::map<B, Result> >::const_iterator i = m.begin(),
              e = m.end(); i != e; ++i) {
        Result prod = 1;
        for (typename std::map<B, Result>::const_iterator j = i->second.begin(),
                f = i->second.end(); j != f; ++j) {
          prod *= j->second;
        }
        sum += prod;
      }
      return sum;
    }

  }

}

