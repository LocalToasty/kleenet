//===-- TransmitHandler.h ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <set>
#include <vector>

#include "net/util/Type.h"
#include "net/util/Containers.h"

namespace kleenet {
  namespace bg {

    template <typename Key, typename Value, bool continuous> struct Container;
    template <typename Key, typename Value> struct Container<Key,Value,false> {
      typedef std::map<Key,Value> Type;
      static void reserve(Type&,size_t const) {}
      static Value& insert(Type& c,Key const& k) {
        return c[k];
      }
      static Value const& find(Type const& c,Key const& k) {
        typename Type::const_iterator it = c.find(k);
        assert(it != c.end());
        return it->second;
      }
    };
    template <typename Key, typename Value> struct Container<Key,Value,true> {
      typedef std::vector<Value> Type;
      static void reserve(Type& c, size_t const sz) {
        c.reserve(sz);
      }
      static Value& insert(Type& c,Key const& k) {
        if (static_cast<typename Type::size_type>(k) >= c.size()) {
          c.resize(k+1);
        }
        return c[k];
      }
      static Value const& find(Type const& c,Key const& k) {
        assert(static_cast<typename Type::size_type>(k) < c.size());
        return c[k];
      }
    };

    template <typename N1, typename N2, bool N1_continuous = false, bool N2_continuous = false> struct Props {
      typedef N1 Node1;
      typedef N2 Node2;
      typedef Container<N1,std::set<N2>,N1_continuous> MetaContainer1;
      typedef Container<N2,std::set<N1>,N2_continuous> MetaContainer2;
      typedef typename MetaContainer1::Type Container1;
      typedef typename MetaContainer2::Type Container2;
    };

    template <typename Node, typename P> struct SelectContainer {
      typedef typename net::util::TypeSelection<
        Node,typename P::Node1,typename P::Container1,
        typename net::util::TypeSelection<Node,typename P::Node2,typename P::Container2,void>::Type
      >::Type Type;
    };

    template <typename P> class Graph {
      public:
        typedef typename P::Node1 Node1;
        typedef typename P::Node2 Node2;
        //typedef typename ConstRefUnlessPtr<Node1>::Type Node1Pass;
        //typedef typename ConstRefUnlessPtr<Node2>::Type Node2Pass;
        typedef typename P::MetaContainer1 MetaContainer1;
        typedef typename P::MetaContainer2 MetaContainer2;
        typedef typename P::Container1 Container1;
        typedef typename P::Container2 Container2;
      private:
        Container1 nodes1;
        Container2 nodes2;
        static MetaContainer1 const MetaContainer(Node1 const&) {
          return MetaContainer1();
        }
        static MetaContainer2 const MetaContainer(Node2 const&) {
          return MetaContainer2();
        }
        Container1& container(Node1 const&) {
          return nodes1;
        }
        Container2& container(Node2 const&) {
          return nodes2;
        }
        Container1 const& container(Node1 const&) const {
          return nodes1;
        }
        Container2 const& container(Node2 const&) const {
          return nodes2;
        }
        template <typename MetaContainer, typename OutputContainer, typename InputContainer>
        void addNodes(MetaContainer mc, OutputContainer& output, InputContainer const& input) {
          mc.reserve(output,output.size() + input.size());
          for (typename InputContainer::const_iterator it = input.begin(), en = input.end(); it != en; ++it) {
            mc.insert(output,*it);
          }
        }
      public:
        template <typename InputContainer>
        void addNodes(InputContainer const& c) {
          addNodes(
              MetaContainer(typename InputContainer::value_type())
            , container(typename InputContainer::value_type())
            , c
          );
        }
        template <typename From, typename To>
        void addDirectedEdge(From fromNode, To toNode) {
          MetaContainer(fromNode).insert(container(fromNode),fromNode).insert(toNode);
        }
        template <typename NodeA, typename NodeB>
        void addUndirectedEdge(NodeA a, NodeB b) {
          addDirectedEdge(a,b);
          addDirectedEdge(b,a);
        }
        template <typename Node>
        size_t getDegree(Node node) const {
          return MetaContainer(node).find(container(node),node).size();
        }
        template <typename Node>
        size_t countNodes() const {
          return container(Node()).size();
        }
        template <typename Node> struct Keys {
          typedef typename SelectContainer<Node,P>::Type NodeContainer;
          typedef typename net::util::ExtractContainerKeys<NodeContainer> Type;
        };
        template <typename Node>
        typename Keys<Node>::Type keys() const {
          return typename Keys<Node>::Type(container(Node()));
        }
        template <typename Node>
        typename Keys<Node>::NodeContainer __container__() const {
          return typename Keys<Node>::NodeContainer(container(Node()));
        }
    };
  }
}
