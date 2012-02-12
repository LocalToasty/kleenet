#include "DStateStateMapper.h"

#include "net/StateMapper.h"
#include "net/BasicState.h"

#include "StateCluster.h"

#include <cassert>

using namespace net;

template <typename Mapper,typename Info,typename T> DStateInformation<Mapper,Info,T>::DStateInformation(Mapper& m, DState* peers)
  : MappingInformation(), peers(peers), mapper(m) {
  changeCluster(peers->cluster);
}
template <typename Mapper,typename Info,typename T> DStateInformation<Mapper,Info,T>::DState::DState(NodeCount nc, bool& allowResize, StateMapper& mapper)
  : util::LockableNodeTable<T>(nc,allowResize), branchTo(this), cluster(new StateCluster()) {
}
template <typename Mapper,typename Info,typename T> DStateInformation<Mapper,Info,T>::DState::DState(DState const& from)
  : util::LockableNodeTable<T>(from.size(),from.allowResize), branchTo(this), cluster(new StateCluster(*from.cluster)) {
}
template <typename Mapper,typename Info,typename T> DStateInformation<Mapper,Info,T>::DState::~DState() {
  delete cluster;
}

template <typename Mapper,typename Info,typename T> Node const& DStateInformation<Mapper,Info,T>::setNode(Node const& n) {
  DState* newPeers = mapper.getRootDState();
  if (peers) {
    newPeers = peers;
    *peers -= this;
    peers = NULL;
  }
  Node const& result = MappingInformation::setNode(n);
  *newPeers += this;
  return result;
}

template <typename T> DStateMapper<T>::DStateMapper(StateMapperInitialiser const& initialiser, BasicState* rootState, DState* dstate, typename Info::Mapper* self)
  : StateMapperIntermediateBase<T>(initialiser,rootState,new T(*self,dstate))
  , allowResize(true)
  , root(dstate)
  , activeDStates(&root,(&root)+1) /*the only way to initialise a singleton set: fake an array with one element*/ {
}
template <typename T> DStateMapper<T>::~DStateMapper() {
}

template <typename T> void DStateMapper<T>::_remove(std::set<BasicState*> const& remstates) {
  if (!remstates.size())
    return; // done so quickly :)
  // Check that remstates has as many states as there are nodes
  // XXX this assertion may be wrong. not sure right now. CHECK!
  assert(remstates.size() == this->nodes().size());
  // Check that all are in the same DState.
  Peers* ds = this->stateInfo(*(remstates.begin()))->peers;
  assert(ds);
  bool performTests = false;
  assert((performTests = true));
  if (performTests) {
    // Check that they are all in the same Cluster.
    StateCluster* c = this->stateInfo(*remstates.begin())->getCluster();
    (void)c;
    for (std::set<BasicState*>::iterator si = remstates.begin(),
         se = remstates.end(); si != se; ++si) {
      assert(this->stateInfo(*si)->peers == ds);
      assert(this->stateInfo(*remstates.begin())->getCluster() == c);
    }
    // See that the DState contains exactly the right states
    // -- ALMOST perfect check - but cheap
    assert(ds->size() == this->nodes().size());
    // See that the Cluster contains exactly the right states
    // XXX XXX XXX assert(c->members.size() == remstates.size());
  }

  // Okay, we are convinced that noting too bad will happen
  // ... so go on with the real job.
  activeDStates.erase(ds);
  delete ds;
  // The actual states will be dealt with by StateMapper::remove.
}

template <typename T> typename DStateMapper<T>::DState* DStateMapper<T>::getRootDState() const {
  return root;
}

template <typename T> void DStateMapper<T>::setNodeCount(NodeCount nodes) {
  root->resize(nodes);
}

template <typename T> void DStateMapper<T>::movePeer(Info* peer, Peers* peers) {
  *(peer->peers) -= peer;
  *peers += peer;
}

template <typename Mapper,typename Info,typename T> typename DStateInformation<Mapper,Info,T>::DState& DStateInformation<Mapper,Info,T>::DState::operator+=(DStateInformation* info) {
  if (info->peers) {
    if (info->getNode() != Node::INVALID_NODE)
      info->peers->minus(static_cast<typename Mapper::Info*>(info));
    info->peers = NULL;
  }
  if (info->getNode() != Node::INVALID_NODE) {
    plus(static_cast<typename Mapper::Info*>(info));
    info->peers = this;
    info->changeCluster(cluster);
  //} else {
  //  info->changeCluster(NULL);
  }
  return *this;
}
template <typename Mapper,typename Info,typename T> typename DStateInformation<Mapper,Info,T>::DState& DStateInformation<Mapper,Info,T>::DState::operator-=(DStateInformation* info) {
  if (this == info->peers && info->getNode() != Node::INVALID_NODE) {
    minus(static_cast<typename Mapper::Info*>(info));
    info->peers = NULL;
  //  info->changeCluster(NULL); // it stays in the cluster, even if it's not in the DState; Weird, I give you this.
  }
  return *this;
}

//######################################################## Copy on Branch Mapper

CoBInformation::DState::DState(NodeCount nc, bool& allowResize, StateMapper& mapper)
  : DStateInformationBase::DState(nc,allowResize,mapper) {
}
CoBInformation::DState::~DState() {
  for (iterator it = begin(), en = end(); it != en; ++it) {
    *this -= *it;
  }
}

void CoBInformation::DState::plus(CoBInformation* info) {
  assert(info->getNode() != Node::INVALID_NODE);
  (*this)[info->getNode()] = info;
}
void CoBInformation::DState::minus(CoBInformation* info) {
  assert(this == info->peers && info->getNode() != Node::INVALID_NODE);
  (*this)[info->getNode()] = NULL;
}

CoBInformation::CoBInformation(CoBStateMapper& m, DState* ds)
  : DStateInformationBase(m, ds) {
  *peers += this;
}

CoBInformation::CoBInformation(CoBInformation const& from)
  : DStateInformationBase(from) {
  if (getNode() != Node::INVALID_NODE) {
    assert(from.getNode() == this->getNode());
    //peers->lock();   we do not have to lock the dscenario => allowing the user more freedom
    if (peers->branchTo == peers) {
      // We caused the branch! We have to force fork our peers now. They will not enter this scope!
      assert(peers == from.peers && "There is a problem in the CoBInformation COPY-CTOR.");
      DState* ds = new DState(*static_cast<DState*>(peers));
      peers->branchTo = ds;
      // note: the new Cluster is implicitly created by the DState!
      mapper.activeDStates.insert(ds);
      mapper.dscenarios++;
      for (DState::iterator it = peers->begin(), end = peers->end(); it != end; ++it)
        if ((*it)->getNode() != this->getNode())
          mapper.fork((*it)->getState());
      peers = peers->branchTo;
      from.peers->branchTo = from.peers;
    }
    peers = peers->branchTo;
    (*peers)[getNode()] = this;
    changeCluster(peers->cluster);
  }
}

CoBStateMapper::CoBStateMapper(net::StateMapperInitialiser const& initialiser, BasicState* rootState)
  : DStateMapper<CoBInformation>(initialiser,rootState,new DState(0,allowResize,*this),this)
  , dscenarios(1) {
}

void CoBStateMapper::_findTargets(BasicState const& state, Node const dest) const {
  Info const* info = stateInfo(state);
  assert(info);
  this->foundTarget((*(info->peers))[dest]->getState());
}

unsigned CoBStateMapper::countTotalDistributedScenarios() const {
  return dscenarios;
}

void CoBStateMapper::_map(BasicState& state, Node dest) {
  // We don't have to map, because in CoB,
  // conflicts are resolved when they arrive.
}

void CoBStateMapper::_phonyMap(std::set<BasicState*> const& senders, Node dest) {
  // We don't have to map, because in CoB,
  // conflicts are resolved when they arrive.
}

//################################################### Copy on Write Mapper 1 & 2

CoWInformation::DState::DState(NodeCount nc, bool& allowResize, StateMapper& mapper)
  : DStateInformationBase::DState(nc,allowResize,mapper) {
}

void CoWInformation::DState::plus(CoWInformation* info) {
  assert(info->getNode() != Node::INVALID_NODE);
  assert(info->sli);
  info->sli = (*this)[info->getNode()].put(info);
}
void CoWInformation::DState::minus(CoWInformation* info) {
  assert(this == info->peers && info->getNode() != Node::INVALID_NODE);
  (*this)[info->getNode()].drop(info->sli);
  info->sli = NULL;
}

CoWInformation::DState::~DState() {
  // Our SafeList is designed for maximal safety and performance, so if you
  // destroy a non-empty list, it will complain (via assertion failure).
  // Therefore we have to boilerplaterise our way through the -= calls.
  for (iterator it = begin(), en = end(); it != en; ++it) {
    std::vector<CoWInformation*> buf(it->size(),NULL);
    std::vector<CoWInformation*>::iterator end = buf.begin();
    for (util::SafeListIterator<CoWInformation*> sit(*it); sit.more(); sit.next()) {
      *end++ = sit.get();
    }
    while (end != buf.begin()) {
      *this -= *--end;
    }
  }
}

CoWInformation::CoWInformation(CoWStateMapper& m, DState* dstate)
  : DStateInformationBase(m, dstate), sli(NULL) {
  *peers += this;
}

CoWInformation::CoWInformation(CoWInformation const& from)
  : DStateInformationBase(from), sli(NULL) {
  *peers += this;
}

CoWStateMapper::CoWStateMapper(net::StateMapperInitialiser const& initialiser, BasicState* rootState)
  : DStateMapper<CoWInformation>(initialiser,rootState,new DState(0,allowResize,*this),this) {
}

void CoWStateMapper::_map(BasicState& state, Node dest) {
  Info* const info = stateInfo(state);
  assert(info);
  Node const nd = info->getNode();
  Info::Peers& peers = *(info->peers);
  bool rivalled = false;

  for (util::SafeListIterator<Info*> it(peers[nd]); it.more(); it.next()) {
    if (it.get() != info) {
      rivalled = true;
      if (!_continueRivalSearch(it.get()))
        break;
    }
  }

  if (rivalled) {
    _postProcessRivals(info);
    // XXX This is hardly efficient: we iterate over all neighbours three times,
    // although one time would suffice. It is safer though, because we use
    // locked lists. The iterator locks the list, so we have to:
    //  1. cache all, 2. handle all, 3. delete all (cached)
    // Obviously step 1 and 3 are only needed due to the locking mechanism.
    std::vector<Info*> nbuf;
    nbuf.reserve(peers.size());
    // 1.
    for (DState::iterator n = peers.begin(), e = peers.end(); n != e; ++n) {
      for (util::SafeListIterator<Info*> it(*n); it.more(); it.next()) {
        if (it.get()->getNode() == nd) {
          // Leave INNER loop ... we entered a loop we should never have!
          // First element skips.
          break;
        }
        nbuf.push_back(it.get());
      }
    }
    // 2.
    for (std::vector<Info*>::const_iterator it(nbuf.begin()), end(nbuf.end()); it != end; ++it) {
      _handleRivalledNeighbour(*it);
    }
    // 3.
    // nbuf goes out of scope
  }
}

void CoWStateMapper::_phonyMap(std::set<BasicState*> const& senders, Node dest) {
  typedef std::map<Info::Peers*,std::vector<BasicState*> > Chunks;
  Chunks chunks;
  // The semantic of `chunks`: All states to a given dstate that are sening the packet.
  for (std::set<BasicState*>::const_iterator i = senders.begin(), e = senders.end(); i != e; ++i) {
    chunks[stateInfo(*i)->peers].push_back(*i);
  }
  // The chunks indirection allows us to handle each dstate independently.
  for (Chunks::const_iterator i = chunks.begin(), e = chunks.end(); i != e; ++i) {
    std::vector<BasicState*>::const_iterator si = i->second.begin(), se = i->second.end();
    BasicState* const pivotES = *si;
    CoWInformation* const pivotMI = stateInfo(pivotES);
    Info::Peers& commonDS = *(i->first);
    assert(commonDS[pivotMI->getNode()].size() >= i->second.size() && "Inconsistent DState information.");
    if (commonDS[pivotMI->getNode()].size() > i->second.size()) {
      // okay, not ALL states are sending, so we have to map ...
      _phonyMap(++si,se,pivotMI,dest);
    }
  }
}

void CoWStateMapper::_findTargets(BasicState const& state, Node const dest) const {
  for (util::SafeListIterator<Info*> it((*(stateInfo(state)->peers))[dest]); it.more(); it.next()) {
    this->foundTarget(it.get()->getState());
  }
}

unsigned CoWStateMapper::countCurrentDistributedScenarios() const {
  std::map<Peers*, std::map<unsigned, unsigned> > network;
  for (std::set<Peers*>::const_iterator dsit = activeDStates.begin(), dsend = activeDStates.end(); dsit != dsend; ++dsit) {
    std::map<unsigned, unsigned>& dsn = network[*dsit];
    unsigned n = 0;
    for (DState::iterator i = (*dsit)->begin(), end = (*dsit)->end(); i != end; ++i) {
      dsn[n++] = i->size();
    }
  }
  return util::innerProduct(network);
}

unsigned CoWStateMapper::countTotalDistributedScenarios() const {
  return countCurrentDistributedScenarios() + truncatedDScenarios();
}

/*
 *  Copy on Write 1
 */
CoW1StateMapper::CoW1StateMapper(net::StateMapperInitialiser const& initialiser, BasicState* rootState)
  : CoWStateMapper(initialiser,rootState)
  , rivals() {
}

void CoW1StateMapper::_map(BasicState& state, Node dest) {
  rivals.clear();
  CoWStateMapper::_map(state, dest);
  rivals.clear();
}
bool CoW1StateMapper::_continueRivalSearch(Info *rival) {
  rivals.push_back(rival);
  return true;
}
void CoW1StateMapper::_postProcessRivals(Info *sender) {
  for (std::vector<Info*>::const_iterator it = rivals.begin(), e = rivals.end(); it != e; ++it) {
    DState* const ds = new DState(*static_cast<DState*>((*it)->peers));
    movePeer(*it, ds);
    activeDStates.insert(ds);
  }
}
void CoW1StateMapper::_handleRivalledNeighbour(Info* nb) {
  for (std::vector<Info*>::const_iterator it(rivals.begin()), e(rivals.end()); it != e; ++it) {
    movePeer(stateInfo(this->fork(nb->getState())), (*it)->peers);
  }
}

void CoW1StateMapper::_phonyMap(std::vector<BasicState*>::const_iterator brothers, std::vector<BasicState*>::const_iterator brothersEnd, CoWInformation* pivotMI, Node dest) {
  // cow1 guarantees that the rivals, too, will be unrivalled after mapping
  _map(*(pivotMI->getState()),dest);
}

/*
 *  Static Copy on Write 2
 */
CoW2StateMapper::CoW2StateMapper(net::StateMapperInitialiser const& initialiser, BasicState* rootState)
  : CoWStateMapper(initialiser,rootState)
  , snd(NULL) {
}

bool CoW2StateMapper::_continueRivalSearch(Info *rival) {
  return false; // We found one, that's enough.
}
void CoW2StateMapper::_postProcessRivals(Info *sender) {
  DState* const ds = new DState(*static_cast<DState*>(sender->peers));
  movePeer(snd = sender, ds);
  activeDStates.insert(ds);
}
void CoW2StateMapper::_handleRivalledNeighbour(Info *nb) {
  movePeer(stateInfo(this->fork(nb->getState())), snd->peers);
}

void CoW2StateMapper::_phonyMap(std::vector<BasicState*>::const_iterator brothers, std::vector<BasicState*>::const_iterator brothersEnd, CoWInformation* pivotMI, Node dest) {
  _map(*(pivotMI->getState()),dest);
  // Just sneak all other "sending" states over to the new dstate.
  for (;brothers != brothersEnd; ++brothers) {
    movePeer(stateInfo(*brothers), pivotMI->peers);
  }
}
