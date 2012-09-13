#include "net/StateMapper.h"

#include "net/Observer.h"
#include "net/BasicState.h"

#include "StateCluster.h"
#include "DStateStateMapper.h"
#include "SuperStateMapper.h"

#include <cassert>
#include <vector>
#include <stdint.h>

#include "net/util/debug.h"

typedef net::DEBUG<net::debug::mapping> DD;

namespace net {

  class SmStateLog {
    private:
      SmStateLogger& logger;
    public:
      SmStateLog(SmStateLogger&);
      virtual ~SmStateLog();
      virtual void tell(BasicState*) = 0;
  };

  class SmStateLogger {
    friend class SmStateLog;
    private:
      std::set<SmStateLog*> subscribers;
    public:
      void newState(BasicState* es) {
        for (std::set<SmStateLog*>::iterator i(subscribers.begin()), e(subscribers.end()); i != e; ++i) {
        (*i)->tell(es);
      }
    }
  };

  template <typename Container> class SmStateBufferLog : public SmStateLog {
    private:
      Container& log;
    public:
      SmStateBufferLog(SmStateLogger& logger, Container& log)
        : SmStateLog(logger), log(log) {
      }
      void tell(BasicState* s) {
        log.push_back(s);
      }
  };

  class NodeChangeObserver : public AutoObserver<MappingInformation> {
    private:
      StateMapper::Nodes& nodes;
    public:
      NodeChangeObserver(StateMapper::Nodes& nodes, MappingInformation* mi)
        : nodes(nodes) {
        mi->add(this);
      }
      void notify(Observable<MappingInformation>* observable) {
        nodes.insert(observable->observed->getNode());
      }
  };

}

using namespace net;

SmStateLog::SmStateLog(SmStateLogger& logger)
  : logger(logger) {
  logger.subscribers.insert(this);
}

SmStateLog::~SmStateLog() {
  logger.subscribers.erase(this);
}

StateMapper::StateMapper(StateMapperInitialiser const& initialiser, BasicState* rootState, MappingInformation* mi)
        : StateMapperInitialiser(initialiser)
        , validTargets(false)
        , stateLogger(new SmStateLogger())
        , _nodes()
        , _truncatedDScenarios(0)
        , nco(new NodeChangeObserver(_nodes, mi)) {
  mi->setState(rootState);
  assert(mi->getCluster() && "Cluster has not yet been set!");
}

void StateMapper::foundTarget(BasicState const* const ts) const {
  // Note that targets is mutable.
  targets.push_back(const_cast<BasicState*>(ts));
}

StateMapper::Targets const& StateMapper::allFoundTargets() const {
  return targets;
}

std::set<Node> const& StateMapper::nodes() const {
  return _nodes;
}

StateMapper::~StateMapper() {
}

Node StateMapper::getStateNode(BasicState const* state) {
  MappingInformation* const mi = MappingInformation::retrieveDependant(state);
  if (!mi)
    return Node(); // invalid node
  return mi->getNode();
}
void StateMapper::setStateNode(BasicState const* state, Node const& n) {
  MappingInformation* const mi = MappingInformation::retrieveDependant(state);
  if (mi)
    mi->setNode(n);
}

/// Fork the passed state but do nothing else.
BasicState* StateMapper::fork(BasicState* state) {
  BasicState* ns = state->forceFork();
  assert(ns != state);

  stateLogger->newState(ns);

  return ns;
}

StateMapperInitialiser::StateMapperInitialiser(bool phonyPackets)
  : phonyPackets(phonyPackets) {
}

namespace { // avoid clashes
  struct SMBuilderInterface {
    virtual StateMapper* operator()(StateMapperInitialiser const& init, BasicState* rootState) const = 0;
  };
  template <StateMappingType t, typename SM> struct SMBuilder : SMBuilderInterface {
    SMBuilder(std::map<StateMappingType, SMBuilderInterface*>& trans) {
      trans[t] = this;
    }
    StateMapper* operator()(StateMapperInitialiser const& init, BasicState* rootState) const {
      return new SM(init,rootState);
    }
  };
}

/// Create the (derived) StateMapper (this method is a static factory method).
StateMapper* StateMapper::create(StateMappingType mt,
                                 bool usePhonyPackets,
                                 BasicState* rootState) {
  std::map<StateMappingType, SMBuilderInterface*> trans;
  SMBuilder<SM_COPY_ON_BRANCH,CoBStateMapper> smb1(trans);
  SMBuilder<SM_COPY_ON_WRITE,CoW1StateMapper> smb2(trans);
  SMBuilder<SM_COPY_ON_WRITE2,CoW2StateMapper> smb3(trans);
  SMBuilder<SM_SUPER_DSTATE,SuperStateMapperNoClustering> smb4(trans);
  SMBuilder<SM_SUPER_DSTATE_WITH_BF_CLUS,SuperStateMapperBfClustering> smb5(trans);
  SMBuilder<SM_SUPER_DSTATE_WITH_SMART_CLUS,SuperStateMapperSmartClustering> smb6(trans);
  StateMapperInitialiser const initialiser(usePhonyPackets);
  assert(trans[mt] && "Invalid state mapping algorithm selected!");
  return (*trans[mt])(initialiser,rootState);
}

bool StateMapper::checkMappingAdmissible(BasicState const* es, Node n) const {
  assert((!validTargets)
    && "Cannot map if there are still valid targets. Invalidate first.");
  assert(stateInfo(es)
    && "State to map has no valid mapping information.");
  assert(nodes().find(stateInfo(es)->getNode()) != nodes().end()
    && "Cannot map from a non-existant node.");
  if (DD::enable && nodes().find(n) == nodes().end()) {
    DD::cout << " ! WARNING ! Trying to map to node " << n.id << " while the valid nodes are:" << DD::endl;
    for (Nodes::const_iterator it = nodes().begin(), en = nodes().end(); it != en; ++it) {
      DD::cout << "  * " << it->id << DD::endl;
    }
  }
  assert(nodes().find(n) != nodes().end()
    && "Cannot map to a non-existant node.");
  // no local delivery => call custom code
  return stateInfo(es)->getNode() != n;
}

void StateMapper::map(BasicState *state, Node dest) {
  if (checkMappingAdmissible(state,dest)) {
    _map(*state, dest);
  }
}

void StateMapper::map(BasicState &state, Node dest) {
  map(&state,dest);
}

void StateMapper::map(std::set<BasicState*> const& states, Node dest) {
  std::set<BasicState*> validStates;
  for (std::set<BasicState*>::const_iterator i = states.begin(), e = states.end(); i != e; ++i) {
    if (checkMappingAdmissible(*i,dest)) {
      validStates.insert(validStates.end(),*i);
    }
  }
  if (phonyPackets) {
    if (!validStates.empty())
      _phonyMap(validStates, dest);
  } else {
    for (std::set<BasicState*>::const_iterator i = validStates.begin(), e = validStates.end(); i != e; ++i) {
      _map(**i,dest);
    }
  }
}


bool StateMapper::paranoidExplosionsActive() const {
  bool active = false;
  assert((active = true));
  return active && PARANOID_EXPLOSIONS;
}

void StateMapper::explode(BasicState* state) {
  explode(state, NULL);
}

void StateMapper::explode(BasicState* state,
                          std::vector<BasicState*>* siblings) {
  explode(state, _nodes, _nodes, siblings);
}

void StateMapper::explode(BasicState* state,
                          std::set<Node> const& cleanWithRespectTo,
                          std::set<Node> const& nukeNodes,
                          std::vector<BasicState*>* siblings) {
  assert((!validTargets) &&
    "Cannot explode dstate if there are still valid targets."
    " Invalidate first.");
  assert(state && "Cannot explode dstate of NULL.");
  assert(stateInfo(state) && "Exploding state without Mapping Information.");
  if (stateInfo(state)->getNode() == Node::INVALID_NODE)
    return;
  // In this log we will collect all states we have to explode. Especially, it
  // will be passed to the fork method which will append all newly created
  // states, so that we do not miss new siblings. Note that 'state' will never
  // be added to the log.
  std::vector<BasicState*> log;
  SmStateBufferLog<std::vector<BasicState*> > logwrap(*stateLogger,log);
  Node const nd = stateInfo(state)->getNode();
  // Now, execute a "cleaning sequence", to get rid of all rivals (we do not
  // want to explode these). We have to map to all nodes to be sure that 'state'
  // has no rivals regarding any node.
  for (std::set<Node>::const_iterator i = cleanWithRespectTo.begin(), e = cleanWithRespectTo.end();
          i != e; ++i) {
    map(*state, *i);
  }
  // Build lookup table for nukeNodes ...
  // NOTE: I have considered std::bitset but that has constant size.
  // NOTE: I have also considered implementing a bitset, but frankly, I do not care about factor 8.
  Node min = Node::FIRST_NODE;
  Node max = Node::FIRST_NODE;
  for (std::set<Node>::const_iterator i = nukeNodes.begin(), e = nukeNodes.end();
          i != e; ++i) {
    min = (min<*i)?min:*i;
    max = (max>*i)?max:*i;
  }
  std::vector<uint_fast8_t> nlt(1 + max.id - min.id,0);
  for (std::set<Node>::const_iterator i = nukeNodes.begin(), e = nukeNodes.end();
          i != e; ++i) {
    nlt[i->id - min.id] = 1;
  }
  log.reserve(log.size()+nukeNodes.size());
  // States on different nodes; Find all reachable states and put them in the log.
  for (std::set<Node>::const_iterator i = nukeNodes.begin(), e = nukeNodes.end();
          i != e; ++i) {
    // Cut down unnecessary mapping later (do not add 'state' over and over).
    if (*i != nd) {
      findTargets(*state, *i);
      log.insert(log.end(), begin(), end());
      invalidate();
    }
  }
  // We need this set only if PARANOID_EXPLOSIONS is active.
  std::set<BasicState*> check;
  while (!log.empty()) {
    BasicState* const s = log.back();
    if (paranoidExplosionsActive())
      check.insert(s);
    log.pop_back();
    if (siblings && stateInfo(s)->getNode() == nd) {
      // Everything branched now will be a sibling ("in the precise
      // configuration as") 'state'. Note that 'state' itself was never added to
      // the log.
      siblings->push_back(s);
    }
    if (nlt[stateInfo(s)->getNode().id - min.id]) {
      if (paranoidExplosionsActive())
        check.insert(s);
      for (std::set<Node>::const_iterator it = cleanWithRespectTo.begin(), en = cleanWithRespectTo.end();
              it != en; ++it) {
        // New states will be put in the 'log' list by fork, because map will call
        // the fork method and the fork method logs new states in stateLog, which
        // now points to log.
        map(*s, *it);
      }
    }
  }
  if (paranoidExplosionsActive()) {
    /*********** only for verification **/
    for (std::set<BasicState*>::iterator i = check.begin(), e = check.end();
            i != e; ++i) {
      for (std::set<Node>::const_iterator n = nukeNodes.begin(), ne = nukeNodes.end();
              n != ne; ++n) {
        findTargets(**i, *n);
        iterator j = begin();
        assert(j != end());
        assert(++j == end());
        invalidate();
      }
    }
  }
  // Now the statelog will go out of scope, removing itself from the logger.
}

void StateMapper::remove(BasicState *state) {
  bool excessiveTests = false;
  (void)excessiveTests;
  assert((excessiveTests = true) && state && "Unknown state");
  if (!MappingInformation::retrieveDependant(state))
    return;
  if (stateInfo(state)->getNode() == Node::INVALID_NODE) {
    delete stateInfo(state);
    assert(!MappingInformation::retrieveDependant(state) && "StateDependant didn't clean up correctly.");
    return;
  }
  std::set<BasicState*> states;
  states.insert(state);
  // Fetch cluster of 'state' and make sure there's only one target per node.
  for (std::set<Node>::iterator ni = _nodes.begin(), ne = _nodes.end();
          ni != ne; ++ni) {
    size_t const count = findTargets(*state, *ni);
    assert(count >= 1 &&
      "State has no peer on at least one node: Did you already remove this dscenario?.");
    assert(count <= 1 &&
      "State was not exploded before removal: State has ambigous peers.");
    states.insert(begin(), end());
    invalidate();
  }
  // Check if 'state' has rivals.
  // This test is incomplete, as the target states could have rivals,
  // but that would be too expensive to check, so we don't.
  if (excessiveTests) {
    for (std::set<BasicState*>::iterator si = states.begin(), se = states.end();
        si != se; ++si) {
      assert(stateInfo(*si)->getNode() != Node::INVALID_NODE &&
          "State has no associated node.");
      size_t count = findTargets(**si, stateInfo(state)->getNode());
      assert(count == 1 &&
          "State was not exploded before removal: Target has ambigous peers.");
      assert(*(begin()) == state &&
          "Mapper inconsistency: Visibility is not symmetric; This is bad! Seriously!");
      invalidate();
    }
  }
  if (DD::enable) for (std::set<BasicState*>::const_iterator it = states.begin(), en = states.end(); it != en; ++it) {
    DD::cout << "! This state: " << *it << " is now removed from the mapper!" << DD::endl;
  }
  // Tell the mapping algorithm to throw out this dscenario.
  _remove(states);
  // Do the general cleanup, removing MappingInformation objects.
  for (std::set<BasicState*>::iterator si = states.begin(), se = states.end();
          si != se; ++si) {
    delete stateInfo(*si);
    assert(!MappingInformation::retrieveDependant(*si) && "StateDependant didn't clean up correctly.");
  }
  ++_truncatedDScenarios;
}

size_t StateMapper::findTargets(BasicState const* state, Node const dest) const {
  return findTargets(*state,dest);
}

size_t StateMapper::findTargets(BasicState const& state, Node const dest) const {
  assert((!validTargets) &&
    "Cannot find targets if there are still valid targets."
    "Invalidate first.");
  assert(stateInfo(state) &&
    "Cannot find targets for a state without mapping information.");
  assert(nodes().find(dest) != nodes().end() &&
    "Cannot findTargets of a non-existant destination node.");
  assert(nodes().find(stateInfo(state)->getNode()) != nodes().end() &&
    "Cannot findTargets on a non-existant source node.");
  // local delivery
  if (stateInfo(state)->getNode() == dest) {
    foundTarget(&state);
  } else {
    _findTargets(state, dest);
  }
  b = targets.begin();
  e = targets.end();
  assert((b != e) && "No targets found.");
  validTargets = true;
  return targets.size();
}

unsigned StateMapper::truncatedDScenarios() const {
  return _truncatedDScenarios;
}

StateMapper::iterator &StateMapper::begin() {
  assert(validTargets);
  return b;
}

StateMapper::iterator &StateMapper::end() {
  assert(validTargets);
  return e;
}

void StateMapper::invalidate() {
  validTargets = false;
  targets.clear();
}

void StateMapper::setNodeCount(unsigned nodeCount) {
}

bool StateMapper::terminateCluster(BasicState& state, TerminateStateHandler const& terminate) {
  static unsigned depth = 0;
  depth++;
  MappingInformation* const mi = MappingInformation::retrieveDependant(&state);
  typedef net::DEBUG<net::debug::term> DD;
  DD::cout << "[StateMapper::terminateCluster] (" << depth << ") Terminating Cluster (SM) on pivot state " << &state << " with MI: " << mi << ".";
  if (StateCluster* const sc = mi?(mi->getCluster()):0)
    DD::cout << " Cluster information: id=" << (sc->cluster.id) << ", size=" << (sc->members.size());
  DD::cout << DD::endl;
  std::vector<BasicState*> targets;
  std::vector<BasicState*> siblings;

  bool const knownState = mi && (mi->getNode() != Node::INVALID_NODE);

  if (knownState) {
    explode(&state, &siblings);
    DD::cout << "[StateMapper::terminateCluster] (" << depth << ")   explosion yielded " << siblings.size() << " siblings" << DD::endl;
    // NOTE: updateStates() is NOT required, because we don't examine any of the engine's data structures
    // anyway (let alone have to inform the searcher)
    // also, everything is faster if the states can be immediately dispatched!

    // grab target states
    for (Nodes::iterator it = nodes().begin(), ite = nodes().end(); it != ite; ++it) {
      // skip state's node
      if (*it != mi->getNode()) {
        targets.reserve(targets.size() + findTargets(state,*it));
        targets.insert(targets.end(),begin(),end());
        invalidate();
      }
    }
    if (nodes().size()) {
      // this should fail if the dscenario has not been exploded yet
      assert((targets.size() == nodes().size()-1) &&
        "incorrect number of targets");
    }
  }

  // remove dscenario from the mapper
  remove(&state);
  // finally, terminate all involved states (state + targets)
  DD::cout << "[StateMapper::terminateCluster] (" << depth << ")     now invoking the callers functor to indicate that we're done with BasicState " << (&state) << " and its " << targets.size() << " targets" << DD::endl;
  terminate(state,targets);
  // terminate siblings' dscenarios recursively if any
  for (std::vector<BasicState*>::iterator it = siblings.begin(), ie = siblings.end(); it != ie; ++it) {
    if (*it != &state)
      if (terminateCluster(**it, terminate))
        DD::cout << "[StateMapper::terminateCluster] (" << depth << ")     ... nope, we're not. We're ignoring it as it was nested." << DD::endl;
  }
  if (knownState)
    DD::cout << "[StateMapper::terminateCluster] (" << depth << ")     counting this state" << DD::endl;
  DD::cout << "[StateMapper::terminateCluster] (" << depth << ") ." << DD::endl << DD::endl;
  depth--;
  return knownState;
}
