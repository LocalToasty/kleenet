#include "SdsGraph.h"

#include "SuperStateMapper.h"

#include <cassert>

using namespace net;

GNI::~GNI() {}
GEI::~GEI() {}

SdsMember::SdsMember(SdsGraph& g)
  : graph(g) {
}

SdsNode::SdsNode(SdsGraph& g, SdsNodeType type)
  : SdsMember(g), gni(NULL), sli_node(g.nodes.put(this))
  , isA(type)
  , edgeif(neighbours), info(gni) {
  graph.equipNode(this,gni);
}
SdsNode::SdsNode(SdsNode const& from)
  : SdsMember(from.graph), gni(NULL), sli_node(graph.nodes.put(this))
  , isA(from.isA)
  , edgeif(neighbours), info(gni) {
  graph.equipNode(this,gni);
}

SdsNode::~SdsNode() {
  graph.nodes.dropOwn(sli_node);
  if (gni) {
    delete gni;
    gni = NULL;
  }
}

void SdsNode::getNeighbourIterator(util::SafeListIterator<SdsEdge*>& i) {
  i.reassign(neighbours);
}

size_t SdsNode::neighbourCount() const {
  return neighbours.size();
}

bool SdsNode::isIsolated() const {
  return neighbours.size() == 0;
}

SdsStateNode::SdsStateNode(SdsGraph& g, SuperInformation& si)
  : SdsNode(g, SNT_STATE_NODE), si(si) {
}

SdsStateNode* SdsStateNode::getNode(SuperInformation* si) {
  if (!si)
    return NULL;
  return &(si->graphNode);
}

void SdsStateNode::moveToCluster(StateCluster* c) {
  si.changeCluster(c);
}

StateCluster* SdsStateNode::getCluster() {
  return si.getCluster();
}

SdsDStateNode::SdsDStateNode(SdsGraph& g, DState& ds)
  : SdsNode(g, SNT_DSTATE_NODE)
  , myCluster(NULL), ds(ds) {
}

void SdsDStateNode::moveToCluster(StateCluster* c) {
  myCluster = c;
}

StateCluster* SdsDStateNode::getCluster() {
  // If we do not yet belong to a Cluster, we will
  // mirror one now!
  // This is a bit tricky, sorry.
  if (!myCluster) {
    util::SafeListIterator<SdsEdge*> i;
    getNeighbourIterator(i);
    assert(i.more() && "DState has no state.");
    SdsNode* n = i.get()->traverseFrom(this);
    assert(n && n->isA == SNT_STATE_NODE);
    void dumpDSC(SdsDStateNode* ds, SdsNode* s);
    dumpDSC(this,n);
    myCluster = n->getCluster();
  }
  return myCluster;
}

SdsEdge::SdsEdge(SdsStateNode* sn, SdsDStateNode* dn)
  : SdsMember(sn->graph)
  , gei(NULL), info(gei)
  , state(NULL), sli_state(NULL)
  , dstate(NULL), sli_dstate(NULL) {
  graph.equipEdge(this,gei);
}

SdsEdge::SdsEdge(SdsGraph& graph)
  : SdsMember(graph)
  , gei(NULL), info(gei)
  , state(NULL), sli_state(NULL)
  , dstate(NULL), sli_dstate(NULL) {
  graph.equipEdge(this,gei);
}

SdsEdge::~SdsEdge() {
  if (gei) {
    delete gei;
    gei = NULL;
  }
}

#include <iostream>

void SdsEdge::setState(SdsStateNode* newState) {
  if (state != newState) {
    SuperInformation* a = NULL;
    SuperInformation* b = NULL;
    if (state)
      a = &(state->si);
    if (newState)
      b = &(newState->si);
    //std::cout << "[edge " << this << "] setState: " << a << " -> " << b << std::endl;
    remove();
    state = newState;
    add();
  }
}

SdsStateNode* SdsEdge::getState() const {
  return state;
}

void SdsEdge::setDState(SdsDStateNode* newDState) {
  if (dstate != newDState) {
    DState* a = NULL;
    DState* b = NULL;
    if (dstate)
      a = &(dstate->ds);
    if (newDState)
      b = &(newDState->ds);
    //std::cout << "[edge " << this << "] setDState: " << a << " -> " << b << std::endl;
    remove();
    dstate = newDState;
    add();
  }
}

SdsDStateNode* SdsEdge::getDState() const {
  return dstate;
}

void SdsEdge::remove() {
  if (state && dstate) {
    assert(sli_state && sli_dstate);
    state->edgeif.neighbours.dropOwn(sli_state);
    sli_state = NULL;
    dstate->edgeif.neighbours.dropOwn(sli_dstate);
    sli_dstate = NULL;
    graph.removedEdge(this);
  }
}

void SdsEdge::add() {
  if (state && dstate) {
    assert((!sli_state) && (!sli_dstate));
    sli_state = state->edgeif.neighbours.put(this);
    sli_dstate = dstate->edgeif.neighbours.put(this);
    graph.addedEdge(this);
  }
}

SdsNode* SdsEdge::traverseFrom(SdsNode* from) {
  if (from == state)
    return dstate;
  if (from == dstate)
    return state;
  return NULL;
}



SdsGraph::SdsGraph(SuperStateMapper& mapper)
  : mapper(mapper) {
}
SdsGraph::~SdsGraph() {
  // From hell's heart, I stab at thee. For hate's sake, I spit my last breath at thee.
  assert(!nodes.size() && "Deleting SdsGraph now is dangerous as I still have nodes. There's something wrong in the state-mapper, I think.");
}

void SdsGraph::getNodeIterator(util::SafeListIterator<SdsNode*>& i) {
  i.reassign(nodes);
}

SdsDummyGraph::SdsDummyGraph(SuperStateMapper& mapper)
  : SdsGraph(mapper) {
}

void SdsDummyGraph::equipEdge(SdsEdge* edge, GEI*& gei) {
  gei = NULL;
}

void SdsDummyGraph::equipNode(SdsNode* node, GNI*& gni) {
  gni = NULL;
}

void SdsDummyGraph::removedEdge(SdsEdge* e) {
}

void SdsDummyGraph::addedEdge(SdsEdge* e) {
}


