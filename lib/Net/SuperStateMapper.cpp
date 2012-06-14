#include "SuperStateMapper.h"

#include "net/util/SafeList.h"
#include "util/SharedSafeList.h"
#include "StateCluster.h"

#include <cassert>
#include <utility>

#include "net/util/debug.h"

#define DD DEBUG<debug::mapping>

using namespace net;

/// Super DState Mapper

SuperInformation::SuperInformation(SuperStateMapper& m, SdsGraph& graph)
  : MappingInformation()
  , input(0), wasFound(0)
  , mapper(m)
  , graphNode(graph, *this)
  , multiplicity(0), vstates() {
  graphNode.moveToCluster(new StateCluster());
}

SuperInformation::SuperInformation(SuperInformation const& from)
  : MappingInformation(from)
  , input(0), wasFound(0)
  , mapper(from.mapper)
  , graphNode(*mapper.graph(), *this)
  , multiplicity(0), vstates() {
  if (mapper.doProperBranches() && getNode() != Node::INVALID_NODE) {
    Multiplicity vstateCount = 0;
    // just put the copy in the same dstates as 'this'
    for (util::SafeListIterator<VState*> vs(from.vstates); vs.more(); vs.next()) {
      vstateCount++;
      if (vs.get()->dstate()->adoptVState(new VState(this))) {
        assert(0 && "new VState already had a DState.");
      }
    }
    assert(vstateCount);
    assert(from.multiplicity == multiplicity);
    assert(vstateCount == multiplicity);
  }
}

SuperInformation::~SuperInformation() {
}

VState::VState(SuperInformation *s)
  : sli_si(NULL), si(NULL), sli_ds(NULL), ds(NULL),
    graphEdge(s->graphNode.graph), isTarget(false) {
  //DD::cout << "Creating VState " << this << " (on SI " << s << ", on BasicState " << s->getState() << ")" << DD::endl; // XXX
  moveTo(s);
}
VState::~VState() {
  //DD::cout << "Destroying VState " << this << DD::endl; // XXX
}

void VState::moveTo(SuperInformation *s) {
  if (si) {
    assert(s->getNode() == si->getNode() && "Cannot move to a different node.");
    assert(sli_si && "Inconsistent link.");
    si->multiplicity--;
    assert(!si->vstates.isLocked());
    si->vstates.drop(sli_si);
  }
  assert(s && "Cannot move to NULL.");
  si = s;
  sli_si = si->vstates.put(this);
  si->multiplicity++;
  graphEdge.setState(&(s->graphNode));
}
SuperInformation *VState::info() {
  return si;
}
DState *VState::dstate() {
  return ds;
}
BasicState *VState::state() {
  return si->getState();
}

DStates::DStates() : list(_list) {
}

DState::DState(SuperStateMapper& ssm, NodeCount expectedNodeCount)
  : vstates(expectedNodeCount, ssm.allowResize), sli_mark(NULL)
  , sli_actives(ssm.activeDStates._list.put(this))
  , graphNode(*ssm.graph(),*this), mapper(ssm), heir(NULL) {
  assert(!vstates.isLocked());
}
DState::DState(DState& ds)
  : vstates(ds.vstates.size(), ds.mapper.allowResize), sli_mark(NULL)
  , sli_actives(ds.mapper.activeDStates._list.put(this))
  , graphNode(*ds.mapper.graph(),*this), mapper(ds.mapper), heir(NULL) {
  ds.heir = this;
  vstates.lock();
}
DState::~DState() {
  //DD::cout << "DState dying" << DD::endl;
  mapper.activeDStates._list.drop(sli_actives);
  //DD::cout << "DState dead" << DD::endl;
}

DState *DState::adoptVState(VState *vs) {
  assert(vs);
  DState *old = autoAbandonVState(vs);
  // we are friends with VState ...
  //DD::cout << "[" << this << "] size[n:" << vs->info()->getNode().id << "] before adopting vstate: " << vstates[vs->info()->getNode()].size() << DD::endl;
  vs->sli_ds = vstates[vs->info()->getNode()].put(vs);
  //DD::cout << "[" << this << "] size[n:" << vs->info()->getNode().id << "] after adopting vstate: " << vstates[vs->info()->getNode()].size() << DD::endl;
  vs->ds = this;
  vs->graphEdge.setDState(&(this->graphNode));
  return old;
}
/*static*/ DState *DState::autoAbandonVState(VState *vs) {
  DState *old = NULL;
  if (vs && (old = vs->ds)) {
    vs->ds->abandonVState(vs);
  }
  return old;
}
void DState::abandonVState(VState *vs) {
  assert(vs && vs->sli_ds && vs->ds == this);
  assert(!vstates[vs->info()->getNode()].isLocked());
  //DD::cout << "size[n:" << vs->info()->getNode().id << "] before abandoning vstate: " << vstates[vs->info()->getNode()].size() << DD::endl;
  vstates[vs->info()->getNode()].drop(vs->sli_ds);
  //DD::cout << "size[n:" << vs->info()->getNode().id << "] after abandoning vstate: " << vstates[vs->info()->getNode()].size() << DD::endl;
  vs->ds = NULL;
  vs->sli_ds = NULL;
  vs->graphEdge.setDState(NULL);
}
NodeCount DState::getNodeCount() {
  return vstates.size();
}
// use: for (util::SafeListIterator<BasicState*> it(dstate.look(node)); it.more();
//          it.next()) {it.get()->dostuff();}
util::SharedSafeList<VState*> &DState::look(Node node) {
  return vstates[node];
}
bool DState::areRivals(Node source) {
  // since there can be no dstates with no states in a node ...
  util::SafeListIterator<VState*> it(vstates[source]);
  return !(it.singletonOrEmpty());
}
void DState::setMark(util::SafeList<DState*> &marked) {
  sli_mark = marked.put(this);
}
void DState::resetMark(util::SafeList<DState*> &marked) {
  if (sli_mark) {
    marked.drop(sli_mark);
    sli_mark = NULL;
  }
}
bool DState::isMarked() {
  return sli_mark;
}

bool SuperStateMapper::doProperBranches() const {
  return !ignoreProperBranches;
}

void SuperStateMapper::resetMarks() {
  for (util::SafeListIterator<DState*> it(marked); it.more(); it.next()) {
    it.get()->sli_mark = NULL;
  }
  marked.dropAll();
}
bool SuperStateMapper::iterateMarked(util::SafeListIterator<DState*> *sli) {
  if (!sli)
    return false;
  sli->reassign(marked);
  return true;
}


SuperStateMapper::SuperStateMapper(net::StateMapperInitialiser const& initialiser, BasicState* rootState, SdsGraph* myGraph)
  : StateMapperIntermediateBase<SuperInformation>(initialiser,rootState,new SuperInformation(*this,*myGraph))
  , ignoreProperBranches(0)
  , myGraph(myGraph) {
}

SuperStateMapper::~SuperStateMapper() {
  assert(!activeDStates.list.size() && "There are still active dstates.");
  delete myGraph;
}

void SuperStateMapper::setNodeCount(NodeCount nodeCount) {
  assert(getRootDState() && "Cannot set the network size after transmissions.");
  assert(activeDStates.list.size() >= 1
    && "Cannot set the network size after every dstate is gone. "
       "This is probably the termination phase but suggestions can only "
        "be made in the boot phase.");
  assert(activeDStates.list.size() <= 1
    && "Cannot set the network size after dstate branches.");
  util::SafeListIterator<DState*> dss(activeDStates.list);
  assert(dss.more() && "util::SafeList size is inconsistent.");
  assert(dss.get() == getRootDState()
    && "Cannot set the network size after the root dstate is gone.");
  getRootDState()->vstates.resize(nodeCount);
}

Node const& SuperInformation::setNode(Node const& n) {
  assert(mapper.getRootDState() && "Cannot change node affiliation after transmissions.");
  DStates& activeDStates(mapper.activeDStates);
  assert(activeDStates.list.size() >= 1
    && "Cannot change node affiliation after every dstate is gone. "
       "This is probably the termination phase but nodes can only "
       "be changed in the boot phase.");
  assert(activeDStates.list.size() <= 1
    && "Cannot change node affiliation after dstate branches.");
  util::SafeListIterator<DState*> dss(activeDStates.list);
  assert(dss.more() && "util::SafeList size is inconsistent. Something went REALLY wrong!");
  assert(dss.get() == mapper.getRootDState()
    && "Cannot change node affiliation after the root dstate is gone (it is simply not yet implemented!).");
  assert(n != Node::INVALID_NODE && "Cannot move state to an invalid node.");
  assert(!this->vstates.size() && "State has already virtual states (that could be the case if you set the node id of this state twice).");
  // Am I paranoid?
  Node const& result = MappingInformation::setNode(n);
  mapper.getRootDState()->adoptVState(new VState(this));
  return result;
}

void SuperStateMapper::_remove(const std::set<BasicState*> &remstates) {
  DD::cout << "Removing " << remstates.size() << " states from Super Mapper" << DD::endl;
  //for (std::set<BasicState*>::iterator i = remstates.begin(),
  //        e = remstates.end(); i != e; ++i) {
  //  DD::cout << "  - " << *i << DD::endl;
  //}
  if (!remstates.size())
    return;
  std::set<DState*> ds;
  for (std::set<BasicState*>::const_iterator i = remstates.begin(),
          e = remstates.end(); i != e; ++i) {
    SuperInformation *si = stateInfo(*i);
    assert(si);
    {
      util::SafeListIterator<VState*> vs(si->vstates);
      assert(vs.more());
      {
        VState * const v(vs.get());
        ds.insert(v->dstate());
        v->dstate()->abandonVState(v);
        delete v;
      }
      vs.next();
      assert(!vs.more()
        && "State was not exploded but you are trying to remove it.");
    }
    si->vstates.dropAll();
  }
  assert(ds.size() == 1 && "Ambiguous dstate.");
  // If we remove at least one dscenario, the rootDState is meaningless.
  delete *(ds.begin());
}

unsigned SuperStateMapper::countCurrentDistributedScenarios() const {
  std::map<DState*, std::map<unsigned, unsigned> > network;
  for (util::SafeListIterator<DState*> ds(activeDStates.list); ds.more(); ds.next()) {
    unsigned n = 0;
    DState::VStates& vs(ds.get()->vstates);
    for (DState::VStates::iterator i = vs.begin(), e = vs.end();
            i != e; ++i) {
      network[ds.get()][n++] = i->size();
    }
  }
  return util::innerProduct<DState*, unsigned, unsigned>(network);
}

unsigned SuperStateMapper::countTotalDistributedScenarios() const {
  return countCurrentDistributedScenarios() + truncatedDScenarios();
}

void SuperStateMapper::_findTargets(const BasicState &state,
                                    const Node dest) const {
  SuperInformation *si = const_cast<SuperInformation*>(stateInfo(state));
  assert(si);
  for (util::SafeListIterator<VState*> senders(si->vstates); senders.more();
          senders.next()) {
    for (util::SafeListIterator<VState*> vtargets(senders.get()->dstate()->look(dest));
            vtargets.more(); vtargets.next()) {
      SuperInformation *ti = vtargets.get()->info();
      if (!ti->wasFound) {
        foundTarget(vtargets.get()->info()->getState());
        ti->wasFound = 1;
      }
    }
  }
  for (Targets::const_iterator it = allFoundTargets().begin(),
          en = allFoundTargets().end(); it != en; ++it) {
    this->stateInfo(*it)->wasFound = 0;
  }
}

SdsGraph* SuperStateMapper::graph() const {
  return myGraph;
}


namespace {
  template <typename T> class SimpleLock {
    private:
      T& lock;
    public:
      SimpleLock(T& lock) : lock(lock) {
        lock++;
      }
      ~SimpleLock() {
        assert(lock > 0);
        lock--;
      }
  };
}

void SuperStateMapper::_map(BasicState &es, Node dest) {
  //DD::cout << DD::endl << DD::endl << "############################################# START MAP" << DD::endl;
  // How to read this function:
  //   There are several places where we will (logically) need a map, but
  //   instead of using map<class1*,class2*> you will often see an attribute of
  //   class2 in class1. This is not very clean, but a map<> lookup takes
  //   logarithmic time while dereferencing is considered to take constant time.
  //   When "state" is written in double quotes ("), its associated Information
  //   object is meant.

  SuperInformation *si = stateInfo(es);
  assert(si);
  assert(si->multiplicity);
  // Deactivate the automatic branching mechanism for the time of this function
  SimpleLock<unsigned> properBranchesLock(ignoreProperBranches);
  const Node send = si->getNode();
  assert(send != Node::INVALID_NODE);
  // Note that 'targets' is a "set" of vstates, where superTargets are "states".
  util::SafeList<VState*> targets;
  // Yes, this is a redundant list, but avoiding redundancy would be even more
  // expensive. This means that superTargets may contain states more than once.
  util::SafeList<SuperInformation*> superTargets;
  // There should be no marks set, but it doesn't hurt to make sure, considering
  // the marks were originally intended to be general purpose. Now we will use
  // the marks to mark dstates that have rivals.
  resetMarks();
  // Find all targets and if necessary already branch dstates in case of direct
  // rivals
  {
    util::SafeListIterator<VState*> senders(si->vstates);
    assert(!senders.empty());
    // The entire for loop is in O(|virtual_targets|).
    for (; senders.more(); senders.next()) {
      DState *ds = senders.get()->dstate();
      assert(ds);
      // Check if we have rivals in this dstate, and if so duplicate the dstate
      // object and fill that naked dstate with the sender's vstate (that has
      // rivals).
      if (ds->areRivals(send)) { // O(1)
        // Rivalled dstate ...
        ds->setMark(marked);
        // This looks like a memory leak, but the new dstate is saved as
        // ds->heir, also we move the conflicted (rivalled) vstate to the new
        // dstate.
        (new DState(*ds))->adoptVState(senders.get());
        // NOTE: The heir will never be reset. Always check the mark to make
        // sure the heir is up to date.
        // This technique is a bit messy, but it does the trick quite well.
      }
      // Now, find and mark all target vstates:
      // Note that there is no way that we can find a target vstate twice,
      // because (both target and sender) vstates are in exactly one dstate.
      // But we may find a target "state" twice so for every "state" we count
      // the input (targets) we found to compare it with its multiplicity
      //   -> this allows us to find super-rivals.
      for (util::SafeListIterator<VState*> it(ds->look(dest)); it.more(); it.next()) {
        assert(it.get());
        it.get()->isTarget = true;
        // We need this 'target' list mainly for cleaning up (isTarget <- false)
        targets.put(it.get());
        // The 'input' will be reset afterwards, so every time we invoke a new
        // mapping 'input' will be 0 in all SuperInformation objects again.
        // It tells us how many vstates are sending this "state" something
        // e.g. the sender state and target may share two dstates, then input
        // would be 2. Alternatively, you can think of it as the number of
        // virtual packets that are being sent right now.
        it.get()->info()->input++;
        superTargets.put(it.get()->info());
      }
    }
  }
  // We have already branched all conflicted dstates, also, we have the
  // information to recognise target states with super-rivals.
  // But we sill have to determine which states(!) we have to branch, and where
  // to put their virtual states (vstates).
  for (util::SafeListIterator<SuperInformation*> it(superTargets); it.more(); it.next()) {
    // This filter looks ugly, but the superTargets list contains each target
    // as often as it has vstates. So we are still in O(|virtual_targets|).
    // It works, because each state must be processed once, and its input is set
    // to 0, after it is processed.
    if (it.get()->input) {
      // Okay, we found a new "state" that we have to analyse.
      SuperInformation *st = it.get();
      // 'br' indicates if a real state-branch must be done.
      bool br = st->input < st->multiplicity; // <--- super-rivals
      for (util::SafeListIterator<VState*> vs(st->vstates); vs.more() && !br; vs.next()) {
        // A mark implies rivals (i.e. conflict).
        // {!br} => {phi == phi | br}
        br = vs.get()->dstate()->isMarked();
      }
      // 'ns' holds the "receiver" state.
      BasicState *ns = st->getState();
      if (br) {
        ////////  STATE FORK  //////////     o     //
        ns = this->fork(ns);          //    / \    //
        ////////////////////////////////   o   o   //
        SuperInformation *ni = stateInfo(ns);
        util::SafeList<VState*> migrate;
        assert(ni);
        for (util::SafeListIterator<VState*> vs(st->vstates); vs.more(); vs.next()) {
          if (vs.get()->isTarget) {
            DState *ods = vs.get()->dstate();
            if (ods->isMarked()) {
              // If the old dstate was recently branched we have to get a new
              // vstate for the new state and the new dstate (the old ones keep
              // their relation).
              ods->heir->adoptVState(new VState(ni));
            } else {
              // The dstate wasn't branched, so just migrate there (reason:
              // super-rivals).
              migrate.put(vs.get());
            }
          }
        }
        for (util::SafeListIterator<VState*> vs(migrate); vs.more(); vs.next()) {
          vs.get()->moveTo(ni);
        }
        migrate.dropAll();
      }
      it.get()->input = 0;
    }
  }
  // Now we finished the branching and have the target-node and source-node
  // sorted out. All that's left is to put other nodes "states" into the new
  // dstates (if any).
  {
    util::SafeListIterator<DState*> ds;
    // We have to do it like that because the dstate doesn't want to expose its
    // internal util::SafeList<> (we could do nasty stuff with it).
    for (iterateMarked(&ds); ds.more(); ds.next()) {
      Node i = Node::FIRST_NODE;
      const NodeCount nCnt = ds.get()->getNodeCount() + i.id;
      for (; (NodeCount)(i.id) < nCnt; i++) {
        if (i == dest || i == send)
          continue;
        for (util::SafeListIterator<VState*> vs(ds.get()->look(i)); vs.more(); vs.next()) {
          assert(ds.get() != ds.get()->heir);
          ds.get()->heir->adoptVState(new VState(vs.get()->info()));
        }
      }
    }
  }

  // Cleanup ...
  for (util::SafeListIterator<VState*> it(targets); it.more(); it.next()) {
    it.get()->isTarget = false;
  }
  resetMarks();
  targets.dropAll();
  superTargets.dropAll();
}

void SuperStateMapper::_phonyMap(std::set<BasicState*> const &states, Node dest) {
  //DD::cout << "phonyMap" << DD::endl;
  assert(states.size() && "Empty mapping request?");
  Node const origin = stateInfo(*states.begin())->getNode();
  resetMarks();
  // These are individual virtual packets.
  // !IMPORTANT! The key-value (first) is the RECEIVER, the second value
  // i.e. the set is the set of senders!
  // NOTE: The contents of the set are never actually used, only the set's size.
  // Nevertheless, you cannot simply replace the set with an unsigned, as
  // we actually use the set property as we are getting tons of duplicates.
  typedef std::map<VState*,std::set<VState*> > VPackets;
  VPackets vpackets;
  // This is the number of virtual states that will receive _a_ packet.
  // The key value is the MI they belong to.
  typedef std::map<SuperInformation*,unsigned> VTargets;
  VTargets vtargets;
  // Pretty straight forward: All vstates that transmit.
  typedef std::set<VState*> VSenders;
  VSenders vsenders;
  // Collect all virtual packets (these could be a LOT) XXX refactor me please XXX
  // NOTE: The problem here is that the inflation could be quite large.
  // This is necessary to use this bucket-algorithm. To avoid it, the whole thing
  // whould probably grow in code-size considerably, I think.
  for (std::set<BasicState*>::const_iterator i = states.begin(), e = states.end(); i != e; ++i) {
    for (util::SafeListIterator<VState*> senders(stateInfo(*i)->vstates); senders.more(); senders.next()) {
      vsenders.insert(senders.get());
      for (util::SafeListIterator<VState*> receivers(senders.get()->dstate()->look(dest)); receivers.more(); receivers.next()) {
        vpackets[receivers.get()].insert(senders.get());
      }
    }
  }

  //DD::cout << "found a total of " << vpackets.size() << " vpackets" << DD::endl;

  std::vector<VState*> allvt;
  allvt.reserve(vpackets.size());

  // See which vstates actually need forking
  for (VPackets::const_iterator i = vpackets.begin(), e = vpackets.end(); i != e; ++i) {
    VState* target = i->first;
    DState* const ds = target->dstate();
    size_t const total = ds->look(origin).size();
    size_t const sending = i->second.size();
    assert(sending);
    assert(sending <= total);
    //DD::cout << "total states: " << total << "; sending: " << sending << DD::endl;
    if (sending < total) {
      if (!ds->isMarked()) {
        new DState(*ds); // is automatically stored in ds->heir
        ds->setMark(marked);
      }
      target = new VState(target->info());
      ds->heir->adoptVState(target);
    } else {
      //DD::cout << "IGNORING PHONY PACKET!" << DD::endl;
    }
    // Note that it is not possible to add any vstate
    // twice as the vstate is the key of the data structure
    // which is iterated over! Thus an unsigned does the trick!
    vtargets[target->info()]++;
    assert(!target->isTarget);
    target->isTarget = true;
    allvt.push_back(target);
  }

  {
    // Senders and Bystanders (for hard-copy dstates)
    util::SafeListIterator<DState*> dsit;
    for (iterateMarked(&dsit); dsit.more(); dsit.next())
      for (Nodes::const_iterator n = this->nodes().begin(), ne = this->nodes().end(); n != ne; ++n)
        if (*n != dest) {
          std::vector<std::pair<DState*,VState*> > cache;
          util::SharedSafeList<VState*>& slist(dsit.get()->look(*n));
          cache.reserve(slist.size());
          for (util::SafeListIterator<VState*> vs(slist); vs.more(); vs.next())
            cache.push_back(std::make_pair(dsit.get()->heir,(*n == origin)?vs.get():new VState(vs.get()->info())));
          for (std::vector<std::pair<DState*,VState*> >::const_iterator avs = cache.begin(), avse = cache.end(); avs != avse; ++avs)
            avs->first->adoptVState(avs->second);
        }
  }

  std::map<BasicState*,BasicState*> clones;
  SimpleLock<unsigned> properBranchesLock(ignoreProperBranches);

  {
    typedef std::vector<std::pair<VState*,SuperInformation*> > Joblist;
    Joblist jobs;
    // fork the actual states
    for (VTargets::const_iterator i = vtargets.begin(), e = vtargets.end(); i != e; ++i) {
      SuperInformation* const oldInfo = i->first;
      size_t const total = i->first->vstates.size();
      assert(i->first->vstates.size() == i->first->multiplicity);
      size_t const receiving = i->second;
      size_t receivingCheck = 0;
      assert(total >= receiving && "Inconsistent dstate");
      if (total > receiving) {
        // There are receiving vstates and non-receiving vstates!
        BasicState*& clone = clones[oldInfo->getState()];
        if (!clone) {
          //////////////  STATE FORK  ////////////////     o     //
          clone = this->fork(oldInfo->getState());  //    / \    //
          ////////////////////////////////////////////   o   o   //
        }
        assert(clone && "NULL state");
        assert(!stateInfo(clone)->multiplicity);
        for (util::SafeListIterator<VState*> vs(oldInfo->vstates); vs.more(); vs.next()) {
          if (vs.get()->isTarget) {
            jobs.push_back(std::make_pair(vs.get(),stateInfo(clone)));
            receivingCheck++;
          }
        }
        assert(receivingCheck == receiving);
      }
    }
    for (Joblist::const_iterator i = jobs.begin(), e = jobs.end(); i != e; ++i) {
      //DD::cout << i->first << " (" << i->first->info() << "[" << i->first->info()->multiplicity << "]" << " ~> " << i->second << "[" << i->second->multiplicity << "])" << DD::endl;
      assert(i->first->info()->multiplicity > 1);
      i->first->moveTo(i->second);
    }
  }
  for (std::vector<VState*>::const_iterator i = allvt.begin(), e = allvt.end(); i != e; ++i) {
    (*i)->isTarget = false;
  }
  resetMarks();
}


template <typename Graph> void SuperStateMapperWithClustering<Graph>::_map(BasicState &state, Node dest) {
  // Prelimiary work:
  //  We are transmitting, so the complete network must be booted.
  //  Hence we remove the rootDState.
  rootDState = NULL;
  SuperStateMapper::_map(state,dest);
}

template <typename Graph> void SuperStateMapperWithClustering<Graph>::_phonyMap(std::set<BasicState*> const &state, Node dest) {
  rootDState = NULL;
  SuperStateMapper::_phonyMap(state,dest);
}

template <typename Graph> void SuperStateMapperWithClustering<Graph>::_remove(std::set<BasicState*> const& remstates) {
  SuperStateMapper::_remove(remstates);
  rootDState = NULL;
}

template <typename Graph> SuperStateMapperWithClustering<Graph>::SuperStateMapperWithClustering(StateMapperInitialiser const& initialiser, BasicState* rootState)
  : SuperStateMapper(initialiser,rootState,new Graph(*this))
  , rootDState(new DState(*this, 0)) {
}
template <typename Graph> SuperStateMapperWithClustering<Graph>::~SuperStateMapperWithClustering() {
  if (rootDState) {
    assert(activeDStates.list.size() && "Root dstate exists but is not active.");
    delete rootDState;
    rootDState = NULL;
  }
}

template <typename Graph> DState* SuperStateMapperWithClustering<Graph>::getRootDState() {
  return rootDState;
}

// force the compiler to create all types (they're small, so never mind)
template class SuperStateMapperWithClustering<SdsDummyGraph>;
template class SuperStateMapperWithClustering<SdsBfGraph>;
template class SuperStateMapperWithClustering<SdsSmartGraph>;
