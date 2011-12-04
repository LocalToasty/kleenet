#pragma once

#include "net/Node.h"

#include "MappingInformation.h"
#include "util/SafeList.h"
#include "util/NodeTable.h"

#include "StateMapperIntermediateBase.h"

#include <ostream>
#include <set>
#include <map>
#include <vector>

namespace net {
  class BasicState;

  /// Information template base class for dstate aware mappers.
  template <typename Mapper_,typename Info,typename T> class DStateInformation : public MappingInformation {
    private:
      DStateInformation(); // not implemented
    public:
      typedef DStateInformation DStateInformationBase;
      struct DState : public util::LockableNodeTable<T> {
        DState* branchTo;
        StateCluster* const cluster;
        DState(NodeCount nc, bool& allowResize, StateMapper& mapper);
        DState(DState const& from);
        DState& operator+=(DStateInformation*);
        DState& operator-=(DStateInformation*);
        virtual void plus(Info*) = 0;
        virtual void minus(Info*) = 0;
        virtual ~DState();
      };
      typedef T TableEntry;
      typedef DState Peers;
      typedef Mapper_ Mapper;
      Peers* peers;
      Mapper& mapper;
      DStateInformation(Mapper&, DState*);
      Node const& setNode(Node const& n);
  };

  // Mapper template base class for dstate aware mappers.
  template <class T> class DStateMapper
    : public StateMapperIntermediateBase<T> {
  public:
    typedef T Info;
    typedef typename Info::DState DState;
    typedef typename Info::Peers Peers;
    typedef typename Info::TableEntry TableEntry;
  protected:
    bool allowResize;
  private:
    typedef typename DState::iterator DStateIterator;
    DState *root;
  public:
    DState* getRootDState() const;
  protected:
    std::set<Peers*> activeDStates;
    void movePeer(Info* peer, Peers *peers);

    virtual void _remove(const std::set<BasicState*> &remstates);
  public:
    DStateMapper(StateMapperInitialiser const&, BasicState*, DState*, typename Info::Mapper* self);
    virtual ~DStateMapper();
    virtual void setNodeCount(NodeCount nodes);
  };

  /****************************************************************************
   * Copy on Branch (Brute force)                                             *
   ****************************************************************************/
  class CoBInformation;
  class CoBStateMapper;
  class CoBInformation : public DStateInformation<CoBStateMapper,CoBInformation,CoBInformation*> {
    friend class StateMapperIntermediateBase<CoBInformation>;
    public:
      struct DState : public DStateInformationBase::DState {
        DState(NodeCount nc, bool& allowResize, StateMapper& mapper);
        void plus(CoBInformation*);
        void minus(CoBInformation*);
        ~DState();
      };
      CoBInformation(CoBStateMapper&, DState*);
      CoBInformation(CoBInformation const&);
  };

  /* final */
  class CoBStateMapper : public DStateMapper<CoBInformation> {
    friend class CoBInformation;
    private:
      unsigned dscenarios;
    protected:
      void _map(BasicState& state, Node dest);
      void _phonyMap(std::set<BasicState*> const &senders, Node dest);
    public:
      CoBStateMapper(net::StateMapperInitialiser const&, BasicState*);
      void _findTargets(BasicState const &state, const Node dest) const;
      unsigned countTotalDistributedScenarios() const; // O(1)
  };

  /****************************************************************************
   * Copy on Write 1 & 2 (Naive)                                              *
   ****************************************************************************/
  class CoWInformation;
  class CoWStateMapper;
  class CoWInformation : public DStateInformation<CoWStateMapper,CoWInformation,util::SafeList<CoWInformation*> > {
    friend class StateMapperIntermediateBase<CoWInformation>;
    public:
      struct DState : public DStateInformationBase::DState {
        DState(NodeCount nc, bool& allowResize, StateMapper& mapper);
        void plus(CoWInformation*);
        void minus(CoWInformation*);
        ~DState();
      };
      util::SafeListItem<CoWInformation*>* sli;
      CoWInformation(CoWStateMapper&, DState*);
      CoWInformation(CoWInformation const&);
  };

  // Implement one mapper ... get one for free ... well, almost.
  /// Copy on WRITE base class that is used by customising protected methods.
  class CoWStateMapper : public DStateMapper<CoWInformation> {
  protected:
    virtual bool _continueRivalSearch(Info *rival) = 0;
    virtual void _postProcessRivals(Info *sender) = 0;
    virtual void _phonyMap(std::vector<BasicState*>::const_iterator brothers, std::vector<BasicState*>::const_iterator brothersEnd, CoWInformation* pivotMI, Node dest) = 0;
    virtual void _handleRivalledNeighbour(Info *nb) = 0;
    void _phonyMap(std::set<BasicState*> const& senders, Node dest);
    virtual void _map(BasicState& state, Node dest);

    virtual void _findTargets(BasicState const& state, Node const dest) const;
    virtual unsigned countCurrentDistributedScenarios() const;
  public:
    virtual unsigned countTotalDistributedScenarios() const;
    CoWStateMapper(net::StateMapperInitialiser const&, BasicState*);
  };


  /* final */
  class CoW1StateMapper : public CoWStateMapper {
    private:
      std::vector<Info*> rivals;
    protected:
      void _map(BasicState& state, Node dest);
      bool _continueRivalSearch(Info *rival);
      void _postProcessRivals(Info *sender);
      using CoWStateMapper::_phonyMap;
      void _phonyMap(std::vector<BasicState*>::const_iterator brothers, std::vector<BasicState*>::const_iterator brothersEnd, CoWInformation* pivotMI, Node dest);
      void _handleRivalledNeighbour(Info *nb);
    public:
      CoW1StateMapper(net::StateMapperInitialiser const&, BasicState*);
  };

  /* final */
  class CoW2StateMapper : public CoWStateMapper {
    private:
      Info* snd;
    protected:
      bool _continueRivalSearch(Info *rival);
      void _postProcessRivals(Info *sender);
      using CoWStateMapper::_phonyMap;
      void _phonyMap(std::vector<BasicState*>::const_iterator brothers, std::vector<BasicState*>::const_iterator brothersEnd, CoWInformation* pivotMI, Node dest);
      void _handleRivalledNeighbour(Info *nb);
    public:
      CoW2StateMapper(net::StateMapperInitialiser const&, BasicState*);
  };

}

