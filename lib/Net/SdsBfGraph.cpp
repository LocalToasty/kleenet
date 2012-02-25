#include "SdsGraph.h"

#include "SuperStateMapper.h"
#include "StateCluster.h"

#include <cassert>

#include <iostream>
#include <deque>

#define ENABLE_DEBUG 0

#define ddebug if (ENABLE_DEBUG)

using namespace net;

class BfN : public GNI {
  private:
    static uint32_t next;
  public:
    SdsBfGraph::visit_t visit;
    uint32_t const id;
    BfN() : visit(0), id(next++) {}
};

class BfE : public GEI {
  private:
    static uint32_t next;
  public:
    BfE() : id(next++) {}
    uint32_t const id;
};

uint32_t BfE::next = 0;
uint32_t BfN::next = 0;

SdsBfGraph::SdsBfGraph(SuperStateMapper& m)
  : SdsGraph(m), visit(0) {
}

void SdsBfGraph::equipEdge(SdsEdge* edge, GEI*& gei) {
  gei = new BfE();
}

void SdsBfGraph::equipNode(SdsNode* node, GNI*& gni) {
  gni = new BfN();
}

bool SdsBfGraph::find(SdsNode* start, SdsNode* needle, NodeModifier* nm) {
  NodeModifier _nm;
  if (!nm)
    nm = &_nm;
  visit++;
  if (!visit) {
    // freshness ...
    for (util::SafeListIterator<SdsNode*> i, getNodeIterator(i); i.more(); i.next()) {
      static_cast<BfN*>(i.get()->info)->visit = 0;
    }
    visit++;
  }
  std::deque<SdsNode*> buffer;
  buffer.push_back(start);
  while (!buffer.empty()) {
    SdsNode* const curr(buffer.front());
    if (curr == needle)
      return true;
    buffer.pop_front();
    visit_t& v = static_cast<BfN*>(curr->info)->visit;
    assert(v <= visit && "Node from the future! Doc Brown?");
    if (v != visit) {
      nm->mod(curr);
      // we did not see this, yet
      util::SafeListIterator<SdsEdge*> i;
      for (curr->getNeighbourIterator(i); i.more(); i.next()) {
        SdsNode* n = i.get()->traverseFrom(curr);
        assert(n);
        switch (n->isA) {
          case SdsNode::SNT_STATE_NODE:
            buffer.push_back(n);
            break;
          case SdsNode::SNT_DSTATE_NODE:
            // we favour dstates
            buffer.push_front(n);
            break;
        }
      }
    }
    v = visit;
  }
  return false;
}

void SdsBfGraph::removedEdge(SdsEdge* e) {
  struct ClCount : public SdsBfGraph::NodeModifier {
    uintptr_t cnt;
    void mod(SdsNode* n) {
      cnt++;
    }
    ClCount() : cnt(0) {}
  };

  struct ClSplit : public SdsBfGraph::NodeModifier {
    StateCluster* const newCluster;
    ClSplit(StateCluster* newCluster)
      : newCluster(newCluster) {
      }
    void mod(SdsNode* n) {
      if (n) {
        ddebug std::cout << "moving node " << static_cast<BfN*>(n->info)->id << " to cluster " << newCluster->cluster.id << std::endl;
        n->moveToCluster(newCluster);
      }
    }
  };

  ddebug std::cout << "removing Edge (" << static_cast<BfE*>(e->info)->id << ")" << std::endl;

  ClCount cc;
  if (!find(e->getState(),e->getDState(),&cc)) {
    StateCluster* const cluster = e->getState()->getCluster();
    ddebug {//debug
      ClCount cc2;
      bool f = find(e->getDState(),e->getState(),&cc2);
      ddebug std::cout << "\tcould not find dstate (s: " << cc.cnt << ", ds: " << cc2.cnt << ")" << std::endl;
      if (f)
        ddebug std::cout << "BUT THE REVERSE PATH EXISTS!" << std::endl;
    }
    SdsNode* change = e->getDState();
    SdsNode* keep = e->getState();
    if (cc.cnt < cluster->members.size() - cc.cnt) {
      SdsNode* t = change;
      change = keep;
      keep = t;
    }
    ddebug std::cout << "initialising splitter ..." << std::endl;
    ClSplit splitter(new StateCluster(*cluster));
    find(change,NULL,&splitter);
  }

  ddebug std::cout << "DONE removing Edge" << std::endl;
}

namespace net {
  void dumpDSC(SdsDStateNode* ds, SdsNode* s) {
    ddebug std::cout << "mirroring cluster of " << static_cast<BfN*>(s->info)->id << " by dstate " << static_cast<BfN*>(ds->info)->id << std::endl;
  }
}

void SdsBfGraph::addedEdge(SdsEdge* e) {
  ddebug std::cout << std::endl
    << "adding Edge (" << static_cast<BfE*>(e->info)->id << ")" << std::endl;
  ddebug std::cout << "Edge info: s("
    << static_cast<BfN*>(e->getState()->info)->id
    << ") <-> ds("
    << static_cast<BfN*>(e->getDState()->info)->id
    << ")" << std::endl;
  StateCluster* keep = e->getState()->getCluster();
  StateCluster* trash = e->getDState()->getCluster();
  ddebug std::cout << "state-sc:   " << keep->cluster.id << " with " << keep->members.size() << " members" << std::endl;
  ddebug std::cout << "dstate-sc:  " << trash->cluster.id << " with " << trash->members.size() << " members" << std::endl;
  /* An isolated state is always "convinced" to join our cluster,
     even if the state has already a (different) cluster.
     The rationale for this, is that isolated states are always freshly forked
     (otherwise they would belong to at least ONE dstate).
     So they are simply moved to the dstate's cluster.
  */
  if (e->getState()->neighbourCount() == 1) {
    keep = trash;
    e->getState()->moveToCluster(keep);
  }
  assert(keep && trash);
  if (keep == trash) {
    ddebug std::cout << "DONE adding Edge (short return)" << std::endl;
    return;
  }
  if (keep->members.size() < trash->members.size()) {
    StateCluster* t = keep;
    keep = trash;
    trash = t;
  }
  StateCluster::ClusterMembers moveStates(trash->members);
  ddebug std::cout << "trash-sc has " << trash->members.size() << " members" << std::endl;
  for (StateCluster::ClusterMembers::iterator i = moveStates.begin(), e = moveStates.end();
       i != e; ++i) {
    (*i)->changeCluster(keep);
    ddebug std::cout << "-1" << std::endl;
    SdsStateNode* n = SdsStateNode::getNode(static_cast<SuperInformation*>(*i));
    util::SafeListIterator<SdsEdge*> i;
    n->getNeighbourIterator(i);
    for (; i.more(); i.next()) {
      SdsNode* dsn = i.get()->traverseFrom(n);
      assert(dsn && dsn->isA == SdsNode::SNT_DSTATE_NODE);
      static_cast<SdsDStateNode*>(dsn)->moveToCluster(keep);
      ddebug std::cout << "((-1))" << std::endl;
    }
  }
  ddebug std::cout << "trash-sc has " << trash->members.size() << " members (after removal)" << std::endl;
  assert(trash->members.size() == 0);
  delete trash;
  ddebug std::cout << "DONE adding Edge" << std::endl;
}
