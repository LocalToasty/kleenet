#pragma once

#include "StateMapperIntermediateBase.h"
#include "SdsGraph.h"

#include "MappingInformation.h"
#include "net/util/SafeList.h"
#include "util/SharedSafeList.h"
#include "util/NodeTable.h"

#include <ostream>

#include <set>
#include <map>
#include <list>

namespace net {
  /****************************************************************************
   * Super (Super DState State Mapper)                                        *
   ****************************************************************************/

  // forward declaration
  class VState;
  class DState;
  class SuperStateMapper;

  class SuperInformation : public MappingInformation {
    friend class SuperStateMapper;
    friend class StateMapperIntermediateBase<SuperInformation>;
    private:
      // speed up mapping. this member is only used in the mapping function
      unsigned input;
      // speed up findTargets
      mutable unsigned char wasFound;
    public:
      SuperStateMapper& mapper;
      // node of the sds graph
      SdsStateNode graphNode;
      // how many personalities do I have?
      typedef unsigned Multiplicity;
      Multiplicity multiplicity;
      util::SafeList<VState*> vstates;

      SuperInformation(SuperStateMapper&,SdsGraph&);
      SuperInformation(SuperInformation const&);
      ~SuperInformation();
      Node const& setNode(Node const& n);
  };

  /// A virtual state (VState) represents a state the cow(2) mapping would have
  /// created. As we virtualise this, VState has no similarities with
  /// the BasicState class.
  class VState {
    friend class DState;
    private:
      util::SafeListItem<VState*>* sli_si;
      SuperInformation* si;
      util::SafeListItem<VState*>* sli_ds;
      DState *ds;
    public:
      SdsEdge graphEdge;

      bool isTarget;

      VState(SuperInformation *s);
      ~VState();
      void moveTo(SuperInformation *s);
      SuperInformation *info();
      DState *dstate();
      BasicState *state();
    };

    class DStates {
    friend class DState;
    public:
      typedef util::SafeList<DState*> List;
    private:
      List _list;
    public:
      const List &list;
      DStates();
  };

  class DState {
    friend class SuperStateMapper;
    public:
      typedef util::LockableNodeTable<util::SharedSafeList<VState*> > VStates;
    private:
      VStates vstates;
      util::SafeListItem<DState*>* sli_mark; // allows fast lookups
      util::SafeListItem<DState*>* const sli_actives; // active DStates
      SdsDStateNode graphNode;
    public:
      class MapperInterface {
        friend class DState;
        bool allowResize;
        protected:
          MapperInterface() : allowResize(true) {}
      };
      SuperStateMapper &mapper;
      /// The heir member may be outdated, always be sure (e.g. by checking the
      /// mark) that it holds what you expect!
      DState *heir;
      DState(SuperStateMapper &ssm, NodeCount expectedNodeCount);
      DState(DState &ds);
      ~DState();
      /// note: The old vstate's dstate will be forced to abandon it!
      DState *adoptVState(VState *vs);
      static DState *autoAbandonVState(VState *vs);
      void abandonVState(VState *vs);
      NodeCount getNodeCount();
      /// use: for (util::SafeListIterator<BasicState*> it(dstate.look(node));
      ///          it.more(); it.next()) {it.get()->dostuff();}
      util::SharedSafeList<VState*>& look(Node node);
      bool areRivals(Node source);
      void setMark(util::SafeList<DState*> &marked);
      void resetMark(util::SafeList<DState*> &marked);
      bool isMarked();
  };

  class SuperStateMapper
    : public StateMapperIntermediateBase<SuperInformation>
    , public DState::MapperInterface {
    private:
      // Used to partly deactivate the branching mechanism, while the mapping
      // function is active.
      unsigned ignoreProperBranches;
      util::SafeList<DState*> marked;
      void resetMarks();
      bool iterateMarked(util::SafeListIterator<DState*> *sli);
      SdsGraph* myGraph;
    protected:
      virtual void _map(BasicState &state, Node dest);
      virtual void _phonyMap(std::set<BasicState*> const &state, Node dest);
      virtual void _remove(const std::set<BasicState*> &remstates);
      virtual void _findTargets(const BasicState &state, const Node dest) const;
    public:
      SdsGraph* graph() const;
      virtual DState* getRootDState() = 0;
      SuperStateMapper(net::StateMapperInitialiser const&, BasicState*, SdsGraph*);
      ~SuperStateMapper();
      bool doProperBranches() const;
      // All dstate objects that are currently administrated by this mapper.
      DStates activeDStates;
      // Setting the node count is terribly expensive
      //  =>  do it as soon as possible and never again
      // May SIGABRT, burn your house down or kill your dog if you
      // already have a dstate branch!
      virtual void setNodeCount(unsigned nodeCount);

      virtual unsigned countCurrentDistributedScenarios() const;
      virtual unsigned countTotalDistributedScenarios() const;

      void dumpInternals() const;
  };

  template <typename Graph> class SuperStateMapperWithClustering
    : public SuperStateMapper {
    private:
      // The root dstate is used for states arriving at a node.
      // The root is set up at creation and is unset when the first
      // communication occurs in the network, because by then, we assume
      // the network to be booted.
      DState* rootDState;
    protected:
      void _map(BasicState &state, Node dest);
      void _phonyMap(std::set<BasicState*> const &state, Node dest);
      void _remove(std::set<BasicState*> const& remstates);
    public:
      SuperStateMapperWithClustering(StateMapperInitialiser const& initialiser, BasicState* rootState);
      ~SuperStateMapperWithClustering();
      DState* getRootDState();
  };
  // Please see the cpp file. There we make sure that these types are actually
  // created by the compiler.
  typedef SuperStateMapperWithClustering<SdsDummyGraph> SuperStateMapperNoClustering;
  typedef SuperStateMapperWithClustering<SdsBfGraph> SuperStateMapperBfClustering;
  typedef SuperStateMapperWithClustering<SdsSmartGraph> SuperStateMapperSmartClustering;
}
