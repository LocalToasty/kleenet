#include "net/PacketCache.h"

#include "net/StateMapper.h"
#include "net/BasicState.h"
#include "MappingInformation.h"
#include "StateDependant.h"

#include "net/util/debug.h"

using namespace net;

typedef DEBUG<debug::mapping> DD;

namespace {
  struct PacketCacheInformation : net::StateDependant<PacketCacheInformation> {
    using StateDependant<PacketCacheInformation>::setState;
    using StateDependant<PacketCacheInformation>::setCloner;
    std::set<net::PacketCacheBase::StateLink*> backLinks;
    PacketCacheInformation() : StateDependant<PacketCacheInformation>() {}
    PacketCacheInformation(PacketCacheInformation const& from) : StateDependant<PacketCacheInformation>(from) {
    }
    ~PacketCacheInformation() {
      {
        // loop would silently invalidate set iterators!
        std::vector<net::PacketCacheBase::StateLink*> const links(backLinks.begin(),backLinks.end());
        for (std::vector<net::PacketCacheBase::StateLink*>::const_iterator it = links.begin(); it != links.end(); ++it) {
          assert(*it && "Null StateLink");
          assert((*it)->container && "Null StateLink container");
          (*it)->container->erase(**it);
        }
      }
      assert(backLinks.empty());
    }
  };
}

PacketCacheBase::StateLink& PacketCacheBase::StateLink::operator=(BasicState* s) {
  if (state) {
    if (PacketCacheInformation* const pci = PacketCacheInformation::retrieveDependant(state)) {
      pci->backLinks.erase(this);
      // No need to delete pci, maybe it's needed again.
      // If the state dies it will take care of all StateDependants.
    }
  }
  state = s;
  if (state) {
    PacketCacheInformation* pci = PacketCacheInformation::retrieveDependant(state);
    if (!pci) {
      pci = new PacketCacheInformation();
      pci->setState(state);
      pci->setCloner(&NullCloner<PacketCacheInformation>::getCloner());
    }
    pci->backLinks.insert(this);
  }
  return *this;
}


PacketCacheBase::StateTrie::StateTrie()
  : depth(0) {
}

PacketCacheBase::StateTrie::Tree::size_type PacketCacheBase::StateTrie::size() const {
  return tree.size();
}

void PacketCacheBase::onCommitDo(util::SharedPtr<util::DynamicFunctor<Node> > f) {
  commitHooks.push_back(f);
}

unsigned PacketCacheBase::StateTrie::insert(ExData::const_iterator begin, ExData::const_iterator end, BasicState* s) {
  unsigned d;
  if (begin == end) {
    content.insert(s).first->container = &content;
    d = 0;
  } else {
    ExData::const_iterator next = begin;
    ++next;
    assert(util::SharedPtr<DataAtom>(*begin) && "Null ptr in packet string when writing.");
    d = 1+tree[*begin].insert(next,end,s); // implicit StateTrie construction!
  }
  if (d > depth)
    depth = d;
  return depth;
}

void PacketCacheBase::StateTrie::unfoldWith(ExData::iterator it, unsigned remainingDepth, ExData const& exData, Functor const& func) const {
  if (!content.empty()) {
    /* Note: To ensure that we will never invoke the functor with an illformed data-string,
     * we check that we consumed exactly 'depth' many atoms. This will result in a
     * recursion tree that has leafs only at one depth. */
    assert(!remainingDepth && "Ill-formed data-string due to partial tree.");
    func(exData,content);
  }
  if (!tree.empty()) {
    assert(it != exData.end());
    ExData::iterator next = it;
    ++next;
    for (Tree::const_iterator i = tree.begin(), e = tree.end(); i != e; ++i) {
      *it = i->first;
      assert(util::SharedPtr<DataAtom>(*it) && "Null ptr in packet string when reading.");
      i->second.unfoldWith(next,remainingDepth-1,exData,func);
    }
  }
}

void PacketCacheBase::StateTrie::call(Functor const& func) const {
  ExData exData(depth, DataAtomHolder(util::SharedPtr<DataAtom>()));
  unfoldWith(exData.begin(),depth,exData,func);
}


void PacketCacheBase::StateTrie::clear() {
  tree.clear();
  content.clear();
}



PacketCacheBase::PacketCacheBase(StateMapper& stateMapper)
  : stateMapper(stateMapper) {
}

void PacketCacheBase::commitMappings(Node dest, StateTrie const& st, Transmitter const& transmitter) {
  struct Tx : StateTrie::Functor {
    private:
      StateMapper& stateMapper;
      Node dest;
      Transmitter const& transmitter; // second level functor :)
    public:
      Tx(StateMapper& stateMapper, Node dest, Transmitter const& transmitter)
        : stateMapper(stateMapper)
        , dest(dest)
        , transmitter(transmitter) {
      }
      void operator()(ExData const& exData, StateTrie::Content const& states) const {
        std::set<BasicState*> const senders(states.begin(),states.end());
        stateMapper.map(senders, dest);
        for (std::set<BasicState*>::const_iterator sender = senders.begin(), end = senders.end(); sender != end; ++sender) {
          stateMapper.findTargets(*sender, dest);
          std::vector<BasicState*> targets(stateMapper.begin(),stateMapper.end());
          stateMapper.invalidate();
          for (std::vector<BasicState*>::iterator recv = targets.begin(), recvEnd = targets.end(); recv != recvEnd; ++recv) {
            transmitter(*sender,*recv,exData);
          }
          (*sender)->incCompletedTransmissions();
        }
      }
  };
  st.call(Tx(stateMapper,dest,transmitter));
  std::vector<util::SharedPtr<util::DynamicFunctor<Node> > > temp;
  temp.swap(commitHooks);
  for (std::vector<util::SharedPtr<util::DynamicFunctor<Node> > >::iterator it = temp.begin(), end = temp.end(); it != end; ++it) {
    (**it)(dest);
  }
}

void PacketCacheBase::cacheMapping(BasicState* s, StateTrie& location, ExData const& data) {
  assert(!data.empty() && "Transmitting empty packets unsupported!");
  location.insert(data.begin(), data.end(), s);
}

void PacketCacheBase::removeState(BasicState* s) {
  if (s) {
    if (PacketCacheInformation* const pci = PacketCacheInformation::retrieveDependant(s)) {
      delete pci;
    }
  }
}
