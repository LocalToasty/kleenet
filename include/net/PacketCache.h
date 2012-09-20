#pragma once

#include "net/Node.h"
#include "net/DataAtom.h"
#include "net/TransmitHandler.h"

#include "net/util/Functor.h"
#include "net/util/SharedPtr.h"

#include <map>
#include <set>
#include <vector>
#include <stdint.h>

namespace net {
  class StateMapper;
  struct node_t;
  class BasicState;

  template <typename PacketInfo> class PacketCache;

  class PacketCacheBase {
    template <typename PacketInfo> friend class PacketCache;
    private:
      StateMapper& stateMapper;
      std::vector<util::SharedPtr<util::DynamicFunctor<Node> > > commitHooks;
    protected:
      class StateTrie {
        private:
          typedef std::map<DataAtomHolder,StateTrie> Tree;
          typedef std::set<BasicState*> Content;
          Tree tree;
          Content content;
          unsigned depth;
        public:
          struct Functor {
            virtual void operator()(ExData const& exData, std::set<BasicState*> const& states) const = 0;
          };
        private:
          void unfoldWith(ExData::iterator it, unsigned remainingDepth, ExData const& exData, Functor const& func) const;
        public:
          StateTrie();
          unsigned insert(ExData::const_iterator begin, ExData::const_iterator end, BasicState* s);
          void call(Functor const& func) const;
          void clear();
          Tree::size_type size() const;
      };
      void cacheMapping(BasicState* sender, StateTrie& location, ExData const& data);
      struct Transmitter {
        virtual void operator()(BasicState* sender, BasicState* receiver, ExData const& data) const = 0;
      };
      void commitMappings(Node dest, StateTrie const& st, Transmitter const& transmitter);
    public:
      PacketCacheBase(StateMapper& mapper);
      virtual void commitMappings() = 0;
      void onCommitDo(util::SharedPtr<util::DynamicFunctor<Node> >);
  };

  /* NOTE: PacketInfo must be default convertible to Node. And that Node must be the destination! */
  template <typename PacketInfo> class PacketCache : public PacketCacheBase {
    public:
      typedef TransmitHandler<PacketInfo> TxHnd;
    private:
      TxHnd const& transmitHandler;
      typedef std::map<PacketInfo,StateTrie> Packets;
      Packets packets;
    public:
      PacketCache(StateMapper& mapper, TxHnd const& transmitHandler)
        : PacketCacheBase(mapper)
        , transmitHandler(transmitHandler) {
      }
      void cacheMapping(BasicState* sender, PacketInfo pi, ExData const& data) {
        PacketCacheBase::cacheMapping(sender,packets[pi],data);
      }
      void commitMappings() {
        struct PiTransmitter : Transmitter {
          private:
            TxHnd const& transmitHandler;
            PacketInfo const& pi;
          public:
            PiTransmitter(TxHnd const& transmitHandler, PacketInfo const& pi)
              : transmitHandler(transmitHandler), pi(pi) {
            }
            void operator()(BasicState* sender, BasicState* receiver, ExData const& data) const {
              transmitHandler.handleTransmission(pi, sender, receiver, data);
            }
        };
        Packets buffer; // recursion invalidates iterators. EVIL STUFF!
        buffer.swap(packets);
        for (typename Packets::iterator i = buffer.begin(), e = buffer.end(); i != e; ++i) {
          PacketCacheBase::commitMappings(static_cast<Node>(i->first),i->second,PiTransmitter(transmitHandler,i->first));
        }
      }
  };
}

