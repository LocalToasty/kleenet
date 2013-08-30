#include "SdsGraph.h"

#include "SuperStateMapper.h"
#include "StateCluster.h"

#include <cassert>

#include <deque>

#include "net/util/debug.h"

#define DD DEBUG<debug::clusters>

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
        DD::cout << "moving node " << static_cast<BfN*>(n->info)->id << " to cluster " << newCluster->cluster.id << DD::endl;
        n->moveToCluster(newCluster);
      }
    }
  };

  DD::cout << "removing Edge (" << static_cast<BfE*>(e->info)->id << ")" << DD::endl;

  ClCount cc;
  if (!find(e->getState(),e->getDState(),&cc)) {
    StateCluster* const cluster = e->getState()->getCluster();
    if (DD::enable) {
      ClCount cc2;
      bool f = find(e->getDState(),e->getState(),&cc2);
      DD::cout << "\tcould not find dstate (s: " << cc.cnt << ", ds: " << cc2.cnt << ")" << DD::endl;
      if (f)
        DD::cout << "BUT THE REVERSE PATH EXISTS!" << DD::endl;
    }
    SdsNode* change = e->getDState();
    SdsNode* keep = e->getState();
    if (cc.cnt < cluster->members.size() - cc.cnt) {
      SdsNode* t = change;
      change = keep;
      keep = t;
    }
    DD::cout << "initialising splitter ..." << DD::endl;
    ClSplit splitter(new StateCluster(*cluster));
    find(change,NULL,&splitter);
  }

  DD::cout << "DONE removing Edge" << DD::endl;
}

namespace net {
  void dumpDSC(SdsDStateNode* ds, SdsNode* s) {
    DD::cout << "mirroring cluster of " << static_cast<BfN*>(s->info)->id << " by dstate " << static_cast<BfN*>(ds->info)->id << DD::endl;
  }
}

void SdsBfGraph::addedEdge(SdsEdge* e) {
  DD::cout << DD::endl
    << "adding Edge (" << static_cast<BfE*>(e->info)->id << ")" << DD::endl;
  DD::cout << "Edge info: s("
    << static_cast<BfN*>(e->getState()->info)->id
    << ") <-> ds("
    << static_cast<BfN*>(e->getDState()->info)->id
    << ")" << DD::endl;
  StateCluster* keep = e->getState()->getCluster();
  StateCluster* trash = e->getDState()->getCluster();
  DD::cout << "state-sc:   " << keep->cluster.id << " with " << keep->members.size() << " members" << DD::endl;
  DD::cout << "dstate-sc:  " << trash->cluster.id << " with " << trash->members.size() << " members" << DD::endl;
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
    DD::cout << "DONE adding Edge (short return)" << DD::endl;
    return;
  }
  if (keep->members.size() < trash->members.size()) {
    StateCluster* t = keep;
    keep = trash;
    trash = t;
  }
  StateCluster::ClusterMembers moveStates(trash->members);
  DD::cout << "trash-sc has " << trash->members.size() << " members" << DD::endl;
  for (StateCluster::ClusterMembers::iterator i = moveStates.begin(), e = moveStates.end();
       i != e; ++i) {
    (*i)->changeCluster(keep);
     DD::cout << "-1" << DD::endl;
    SdsStateNode* n = SdsStateNode::getNode(static_cast<SuperInformation*>(*i));
    util::SafeListIterator<SdsEdge*> neighbourIterator;
    n->getNeighbourIterator(neighbourIterator);
    for (; neighbourIterator.more(); neighbourIterator.next()) {
      SdsNode* dsn = neighbourIterator.get()->traverseFrom(n);
      assert(dsn && dsn->isA == SdsNode::SNT_DSTATE_NODE);
      static_cast<SdsDStateNode*>(dsn)->moveToCluster(keep);
       DD::cout << "((-1))" << DD::endl;
    }
  }
  DD::cout << "trash-sc has " << trash->members.size() << " members (after removal)" << DD::endl;
  assert(trash->members.size() == 0);
  delete trash;
  DD::cout << "DONE adding Edge" << DD::endl;
}
