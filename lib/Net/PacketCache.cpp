#include "net/PacketCache.h"

#include "net/StateMapper.h"
#include "MappingInformation.h"

using namespace net;


PacketCacheBase::StateTrie::StateTrie()
  : depth(0) {
}

unsigned PacketCacheBase::StateTrie::insert(ExData::const_iterator begin, ExData::const_iterator end, BasicState* s) {
  unsigned d;
  if (begin == end) {
    content.insert(s);
    d = 0;
  } else {
    ExData::const_iterator next = begin;
    ++next;
    d = 1+tree[*begin].insert(next,end,s); // implicit StateTrie construction!
  }
  if (d > depth)
    depth = d;
  return depth;
}

void PacketCacheBase::StateTrie::call(ExData::iterator it, ExData const& exData, Functor const& func) const {
  if (!content.empty())
    func(exData,content);
  if (!tree.empty()) {
    assert(it != exData.end());
    ExData::iterator next = it;
    ++next;
    for (Tree::const_iterator i = tree.begin(), e = tree.end(); i != e; ++i) {
      *it = i->first;
      call(next,exData,func);
    }
  }
}

void PacketCacheBase::StateTrie::call(Functor const& func) const {
  ExData exData(depth, DataAtomHolder(util::SharedPtr<DataAtom>()));
  call(exData.begin(),exData,func);
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
      void operator()(ExData const& exData, std::set<BasicState*> const& states) const {
        stateMapper.map(states, dest);
        for (std::set<BasicState*>::const_iterator sender = states.begin(), end = states.end(); sender != end; ++sender) {
          stateMapper.findTargets(*sender, dest);
          for (StateMapper::iterator recv = stateMapper.begin(), recvEnd = stateMapper.end(); recv != recvEnd; ++recv) {
            transmitter(*sender,*recv,exData);
            //transmitHandler.handleTransmission(pi, *sender, *recv, exData);
          }
          stateMapper.invalidate();
        }
      }
  };
  st.call(Tx(stateMapper,dest,transmitter));
}

void PacketCacheBase::cacheMapping(BasicState* s, StateTrie& location, ExData const& data) {
  assert(!data.empty() && "Transmitting empty packets unsupported!");
  location.insert(data.begin(), data.end(), s);
}
