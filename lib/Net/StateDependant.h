#pragma once

#include <cstddef>
#include <cassert>

// some small interfaces ..
#include "Clonable.h"

namespace net {
  class BasicState;
  class StateDependantDelayable;

  class StateDependantI : public Clonable {
    friend class BasicState;
  };
  //typedef Clonable StateDependantI;

  class RegisterChildDependant {
    private:
      size_t const id;
      size_t pending;
    public:
      RegisterChildDependant();
      void doRegister(StateDependantDelayable*, BasicState*);
      void unRegister(StateDependantDelayable*, BasicState*);
      // call this method when you are created but cannot supply a state, yet.
      void delayedConstruction(StateDependantDelayable*);
      StateDependantI* retrieve(BasicState const*) const;
  };

  class StateDependantDelayable : public StateDependantI {
    friend class RegisterChildDependant;
    private:
      bool pending;
    protected:
      StateDependantDelayable();
      // called by our children, implemented (optionally) by our grand*-children
      virtual void onStateBranch();
  };

  /* CRTP is for code reuse ... it should be called "lazyness-idiom" */
  template <typename Child> class StateDependant : public StateDependantDelayable {
    private:
      static RegisterChildDependant reg;
      BasicState* state;
      void registerState(BasicState* state) {
        reg.doRegister(this, state);
        onStateBranch();
      }
    protected:
      ClonerI const* cloner;
      void setCloner(ClonerI const* newCloner) {
        cloner = newCloner;
      }
      ClonerI const& getCloner() const {
        assert(cloner && "Dependant objects must know how to procreate by knowing a cloner object.");
        return *cloner;
      }
      // this is probably not how you want to create your dependants, as
      // you have to implement that ctor all the way down to the leafs
      StateDependant(BasicState* state) : cloner(NULL), state(state) {
        registerState(state);
      }
      // this is how you probably want to create the first dep
      StateDependant() : state(NULL), cloner(NULL) {
        reg.delayedConstruction(this);
      }
      // this is how you probably want to create all but the first dep
      StateDependant(StateDependant const& from) : state(NULL), cloner(from.cloner) {
        reg.delayedConstruction(this);
      }
      BasicState* getState() const {
        return state;
      }
      void setState(BasicState* _state) {
        assert(!state && "Cannot change state after construction.");
        state = _state;
        registerState(state);
      }
    public:
      static Child* retrieveDependant(BasicState const* _state) {
        return static_cast<Child*>(reg.retrieve(_state));
      }
      virtual ~StateDependant() {
        assert(state && "State must be set before destruction. Short-cut removal not supported.");
        reg.unRegister(this, state);
      }
  };
}

