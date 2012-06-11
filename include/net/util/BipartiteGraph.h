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
#include <deque>

#include "net/util/Type.h"
#include "net/util/Containers.h"

namespace kleenet {
  namespace bg {

    using net::util::TypeSelection;
    using net::util::ExtractContainerKeys;
    using net::util::extractContainerKeys;
    using net::util::LoopConstIterator;
    using net::util::Functor;
    using net::util::DictionaryType;

    template <typename N1, typename N2, DictionaryType N1_is = net::util::UncontinuousDictionary, DictionaryType N2_is = net::util::UncontinuousDictionary> struct Props {
      typedef N1 Node1;
      typedef N2 Node2;
      typedef net::util::Dictionary<N1,std::set<size_t>,N1_is> Dictionary1;
      typedef net::util::Dictionary<N2,std::set<size_t>,N2_is> Dictionary2;
    };

    template <typename Node, typename P> struct SelectDictionary {
      typedef typename TypeSelection<
        Node,typename P::Node1,typename P::Dictionary1,
        typename TypeSelection<Node,typename P::Node2,typename P::Dictionary2,void>::Type
      >::Type Type;
      typedef typename TypeSelection<
        Node,typename P::Node1,typename P::Dictionary2,
        typename TypeSelection<Node,typename P::Node2,typename P::Dictionary1,void>::Type
      >::Type Reverse;
    };
    template <typename Node, typename P> struct SwapNode {
      typedef typename TypeSelection<
        /*if*/Node, /*equals*/typename P::Node1, /*then*/typename P::Node2, /*else*/typename P::Node1
      >::Type Type;
    };

    template <typename P> class Graph {
      public:
        typedef typename P::Node1 Node1;
        typedef typename P::Node2 Node2;
        typedef typename P::Dictionary1 Dictionary1;
        typedef typename P::Dictionary2 Dictionary2;
      private:
        Dictionary1 nodes1;
        Dictionary2 nodes2;
        Dictionary1& dictionaryOf(Node1 const&) {
          return nodes1;
        }
        Dictionary2& dictionaryOf(Node2 const&) {
          return nodes2;
        }
        Dictionary1 const& dictionaryOf(Node1 const&) const {
          return nodes1;
        }
        Dictionary2 const& dictionaryOf(Node2 const&) const {
          return nodes2;
        }
        template <typename OutputDictionary, typename InputContainer>
        void addNodes(OutputDictionary& output, InputContainer const& input) {
          output.reserve(output.size() + input.size());
          for (typename InputContainer::const_iterator it = input.begin(), en = input.end(); it != en; ++it) {
            output[*it]; // merely bumping it
          }
        }
      public:
        template <typename InputContainer>
        void addNodes(InputContainer const& c) {
          addNodes(dictionaryOf(typename InputContainer::value_type()), c);
        }
        template <typename From, typename To>
        void addDirectedEdge(From fromNode, To toNode) {
          dictionaryOf(fromNode)[fromNode].insert(dictionaryOf(toNode).getIndex(toNode));
        }
        template <typename NodeA, typename NodeB>
        void addUndirectedEdge(NodeA a, NodeB b) {
          addDirectedEdge(a,b);
          addDirectedEdge(b,a);
        }
        template <typename Node>
        size_t getDegree(Node node) const {
          return dictionaryOf(node).find(node).size();
        }
        template <typename Node>
        size_t countNodes() const {
          return dictionaryOf(Node()).size();
        }

        /* debug
        template <typename Node> struct NodeCollection {
          typedef typename SelectDictionary<Node,P>::Type NodeDictionary;
          typedef typename net::util::ExtractContainerKeys<NodeDictionary> Type;
        };
        typedef typename NodeCollection<Node1>::Type NodeCollection1;
        typedef typename NodeCollection<Node2>::Type NodeCollection2;
        template <typename Node>
        typename NodeCollection<Node>::Type nodeCollection() const {
          return extractContainerKeys(dictionaryOf(Node()));
        }
        eof debug */

        //template <typename Node>
        //std::set<typename SwapNode<Node,P>::Type> traverse(Node root) const { // rvo (we have to copy anyhow)
        //  return dictionaryOf(root).find(root);
        //}
        //template <typename InputContainer>
        //std::set<typename SwapNode<typename InputContainer::value_type,P>::Type> traverse(InputContainer const& input) const { // rvo (we have to copy anyway)
        //  std::set<typename SwapNode<typename InputContainer::value_type,P>::Type> result;
        //  for (LoopConstIterator<std::set<typename SwapNode<typename InputContainer::value_type,P>::Type> > it(input); it.more(); it.next()) {
        //    result.insert(
        //      dictionaryOf(*it).find(*it).begin()
        //    , dictionaryOf(*it).find(*it).end()
        //    );
        //  }
        //  return result;
        //}
        //template <typename InputContainer>
        //std::set<typename InputContainer::value_type> traverse2(InputContainer const& input) const { // rvo (we have to copy anyhow)
        //  return traverse(traverse(input));
        //}

        template <typename Dictionary, typename Func>
        struct SearchContext {
          // TODO: it is probably for the best if we replace this Queue with our SafeList (just for speed)
          typedef std::deque<typename Dictionary::size_type> Queue;
          // we're using this as a temporary to a function, so we have to pass it as const&
          mutable Dictionary const& dictionary;
          mutable std::vector<bool> visited;
          mutable Queue queue;
          mutable Func onVisit;
          SearchContext(Dictionary const& dictionary, Func onVisit)
            : dictionary(dictionary)
            , visited(dictionary.size(),false)
            , queue()
            , onVisit(onVisit) {
          }
          template <typename InputIterator>
          SearchContext const& setQueueWithIndices(InputIterator begin, InputIterator end) const {
            queue.swap(Queue(begin,end));
          }
          struct ExtractKey {
            Dictionary const& dictionary;
            ExtractKey(Dictionary const& dictionary) : dictionary(dictionary) {}
            typename Dictionary::key_type operator()(typename Dictionary::index_type i) {
              return dictionary.getKey(i);
            }
          };
          template <typename InputIterator>
          SearchContext const& setQueueWithKeys(InputIterator begin, InputIterator end) const {
            typedef net::util::AdHocIteratorTransformation<InputIterator,ExtractKey,typename Dictionary::key_type> Tx;
            setQueueWithIndices(Tx(begin,ExtractKey(dictionary)),
                                Tx(end,ExtractKey(dictionary)));
          }
        };

        template <typename Dictionary, typename Func>
        SearchContext<Dictionary,Func> searchContext(Dictionary const& dictionary, Func onVisit) {
          return SearchContext<Dictionary,Func>(dictionary,onVisit);
        }
        template <typename Dictionary>
        SearchContext<Dictionary,Functor<> > searchContext(Dictionary const& dictionary) {
          return SearchContext<Dictionary,Functor<> >(dictionary,Functor<>());
        }

        template <typename SC1, typename SC2>
        void search(SC1 const& sc1, SC2 const& sc2) const {
          bool newNodes = false;
          for (LoopConstIterator<typename SC1::Queue> it(sc1.queue); it.more(); it.next())
            if (!sc1.visited[*it]) {
              sc1.visited[*it] = true;
              sc1.onVisit(*it);
              for (LoopConstIterator<typename SC1::Dictionary::value_type> edges(sc1.dictionary[*it]); edges.more(); edges.next())
                if (!sc2.visited[*edges])// redundant test, but may save us from spamming the queue
                  sc2.queue.push_back(*edges);
            }
          sc1.queue.clear();
          search(sc2,sc1);
        }
        template <typename Dictionary>
        struct CollectVisits {
          Dictionary const& dictionary;
          std::vector<typename Dictionary::key_type> visited;
          void operator()(typename Dictionary::index_type index) {
            visited.push_back(dictionary.getKey(index));
          }
          CollectVisits(Dictionary& dictionary)
            : dictionary(dictionary)
            , visited() {
            visited.reserve(dictionary.size());
          }
        };
      public:
        template <typename InputContainer, typename OutputContainer>
        void search(InputContainer const& start, OutputContainer& result) const { // rvo (we have to copy anyhow)
          if (start.empty())
            return start;
          typedef typename InputContainer::value_type Node;
          typedef typename OutputContainer::value_type OtherNode;
          CollectVisits<typename SelectDictionary<OtherNode,P>::Type> cv;
          search(
            searchContext(containerOf(Node())).setQueueWithKeys(start.begin(),start.end())
          , searchContext(containerOf(OtherNode()),cv)
          );
          result.swap(OutputContainer(cv.visited.begin(),cv.visited.end()));
        }
    };
  }
}
