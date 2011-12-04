#pragma once

#include "util/SafeList.h"

#include <stdint.h>
#include <list>

namespace net {
  class SuperStateMapper;
  class SuperInformation;
  class DState;
  class SdsGraph;
  class SdsNode;
  class SdsEdge;
  class StateCluster;

  /*!
   * The point of GNI is to allow a dynamic, polymorphic equipping of the
   * Node class. */
  class GNI {
    public:
      virtual ~GNI();
  };
  /*!
   * The point of GEI is to allow a dynamic, polymorphic equipping of the
   * Edge class. */
  class GEI {
    public:
      virtual ~GEI();
  };

  /*!
   * Common base class for an Edge and a Node.
   */
  class SdsMember {
    protected:
      SdsMember(SdsGraph& g);
    public:
      SdsGraph& graph;
  };

  class SdsNode : public SdsMember {
    private:
      GNI* gni;
      util::SafeList<SdsEdge*> neighbours;
      util::SafeListItem<SdsNode*>* const sli_node;
    public:
      enum SdsNodeType {
        SNT_STATE_NODE,
        SNT_DSTATE_NODE
      } const isA;
      struct EdgeIf {
        friend class SdsEdge;
        private:
          util::SafeList<SdsEdge*>& neighbours;
        public:
          EdgeIf(util::SafeList<SdsEdge*>& n) : neighbours(n) {}
      } edgeif;
      void getNeighbourIterator(util::SafeListIterator<SdsEdge*>& i);
      GNI* const& info;
      virtual void moveToCluster(StateCluster* c) = 0;
      virtual StateCluster* getCluster() = 0;
      bool isIsolated() const;
      size_t neighbourCount() const;
    protected:
      SdsNode(SdsGraph& g, SdsNodeType type);
      SdsNode(SdsNode const& from);
      virtual ~SdsNode();
  };

  class SdsStateNode : public SdsNode {
    private:
    public:
      SuperInformation& si;
      SdsStateNode(SdsGraph&,SuperInformation&);
      virtual void moveToCluster(StateCluster* c);
      virtual StateCluster* getCluster();
      static SdsStateNode* getNode(SuperInformation* si);
  };

  class SdsDStateNode : public SdsNode {
    private:
      StateCluster* myCluster;
    public:
      DState& ds;
      SdsDStateNode(SdsGraph&,DState&);
      virtual void moveToCluster(StateCluster* c);
      virtual StateCluster* getCluster();
  };

  class SdsEdge : public SdsMember {
    private:
      GEI* gei;
    public:
      GEI* const& info;
    private:
      SdsStateNode* state;
      util::SafeListItem<SdsEdge*>* sli_state;
      SdsDStateNode* dstate;
      util::SafeListItem<SdsEdge*>* sli_dstate;
    protected:
      void remove();
      void add();
    public:
      SdsEdge(SdsStateNode*, SdsDStateNode*);
      SdsEdge(SdsGraph&);
      virtual ~SdsEdge();
      void setState(SdsStateNode* newState);
      SdsStateNode* getState() const;
      void setDState(SdsDStateNode* newDState);
      SdsDStateNode* getDState() const;
      SdsNode* traverseFrom(SdsNode* from);
  };

  class SdsGraph {
    friend class SdsNode;
    private:
      SuperStateMapper& mapper;
      util::SafeList<SdsNode*> nodes;
    protected:
      void getNodeIterator(util::SafeListIterator<SdsNode*>& i);
    public:
      SdsGraph(SuperStateMapper&);
      virtual ~SdsGraph();
      virtual void equipEdge(SdsEdge* edge, GEI*& gei) = 0;
      virtual void equipNode(SdsNode* node, GNI*& gni) = 0;
      virtual void removedEdge(SdsEdge*) = 0;
      virtual void addedEdge(SdsEdge*) = 0;
  };

  class SdsDummyGraph : public SdsGraph {
    public:
      SdsDummyGraph(SuperStateMapper&);
      virtual void equipEdge(SdsEdge* edge, GEI*& gei);
      virtual void equipNode(SdsNode* node, GNI*& gni);
      virtual void removedEdge(SdsEdge*);
      virtual void addedEdge(SdsEdge*);
  };

  class SdsBfGraph : public SdsGraph {
    public:
      typedef uint16_t visit_t;
    private:
      visit_t visit;
      struct NodeModifier {
        virtual void mod(SdsNode* n) {}
      };
      bool find(SdsNode* start, SdsNode* needle, NodeModifier* nm);
    public:
      SdsBfGraph(SuperStateMapper& m);
      virtual void equipEdge(SdsEdge* edge, GEI*& gei);
      virtual void equipNode(SdsNode* node, GNI*& gni);
      virtual void removedEdge(SdsEdge*);
      virtual void addedEdge(SdsEdge*);
  };
  // TODO!!!
  class SdsSmartGraph : public SdsDummyGraph /* XXX */ {
    public:
      SdsSmartGraph(SuperStateMapper& m) : SdsDummyGraph(m) {}
  };
}

