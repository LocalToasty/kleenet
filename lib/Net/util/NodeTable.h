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
        NodeTable(NodeTable const& nt);// : nodes(nt.nodes) {}
      protected:
        virtual void onResize() {}
        virtual void onExplicitResize(NodeCount newCount) {
          onResize();
        }
        virtual void onAutoResize(Node intrude) {
          onResize();
        }
        void changeSize(NodeCount nc) {
          if (nc > nodes.size()) {
            nodes.reserve(nc);
            // This is necessary because our nested types might have specific copy semantics.
            // We have to get a fresh instance for each slot to avoid soft-copies.
            // Warning: Do not change this, unless you know exactly why it is here!
            for (NodeCount i = nodes.size(); i < nc; i++) {
              nodes.push_back(T());
            }
          }
        }
      public:
        typedef typename N::iterator iterator;
        NodeTable() : nodes(0) {}
        NodeTable(NodeCount nc) : nodes(0) {
          changeSize(nc);
        }
        virtual ~NodeTable() {}
        void resize(NodeCount nc) {
          assert(nc >= nodes.size() && "Removing nodes not supported.");
          onExplicitResize(nc);
          changeSize(nc);
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
            resize(id + 1);
          }
          return nodes[id];
        }
    };

    template <class T> class LockableNodeTable : public NodeTable<T> {
      private:
        LockableNodeTable(LockableNodeTable const& nt);// : NodeTable<T>(nt), allowResize(nt.allowResize) {}
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

