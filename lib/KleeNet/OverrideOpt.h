#pragma once

#include "llvm/Support/CommandLine.h"

/* use like this

        OverrideChain()
          .overrideOpt(__OptionToOverride1__).withValue(__withValue1__).onlyIf(__overrideCondition1__).chain()
          .overrideOpt(__OptionToOverride2__).withValue(__withValue2__).onlyIf(__overrideCondition2__).chain()
          .overrideOpt(__OptionToOverride3__).withValue(__withValue3__).chain() <---- unconditional override
          .passSomeCRef(__someValue__)

  command chaining is fun
 */

namespace kleenet {
  template <typename,typename,typename>
  struct OverrideOptVoid {};

  template <typename Chain, typename ClOpt, template <typename WithChain, typename T, typename CO> class With = OverrideOptVoid>
  class OverrideOpt {
    template <typename,typename,template <typename,typename,typename> class OO>
    friend class OverrideOpt;
    private:
      OverrideOpt(); // not implemented
    protected:
      ClOpt& target;
      mutable bool onDuty;
      void commit() const {
        if (onDuty)
          onDuty = false;
      }
    public:
      OverrideOpt(ClOpt& target) : target(target), onDuty(true) {
      }
      template <typename OO>
      OverrideOpt(OO const& from) : target(from.target), onDuty(true) {
        from.onDuty = false;
      }
      ~OverrideOpt() {
        commit();
      }
      template <typename T>
      With<Chain,T,ClOpt> withValue(T value) const {
        return With<Chain,T,ClOpt>(*this,value);
      }
      Chain chain() const {
        commit();
        return Chain();
      }
  };

  template <typename Chain, typename T, typename ClOpt>
  class OverrideOptWith : public OverrideOpt<Chain,ClOpt> {
    template <typename,typename,template <typename,typename,typename> class OO>
    friend class OverrideOpt;
    private:
      OverrideOptWith(); // not implemented
      OverrideOptWith(OverrideOpt<Chain,ClOpt> const& from, T with) : OverrideOpt<Chain,ClOpt>(from), with(with), condition(true) {
      }
      OverrideOptWith(OverrideOptWith const& from, bool condition) : OverrideOpt<Chain,ClOpt>(from), with(from.with), condition(condition) {
      }
      OverrideOptWith(OverrideOptWith const& from) : OverrideOpt<Chain,ClOpt>(from), with(from.with), condition(from.condition) {
      }
    protected:
      T with;
      bool condition;
      void commit() const {
        if (this->onDuty)
          if (condition)
            this->target = with;
        OverrideOpt<Chain,ClOpt>::commit();
      }
    public:
      ~OverrideOptWith() {
        commit();
      }
      OverrideOptWith onlyIf(bool andCondition) {
        return OverrideOptWith(*this,condition && andCondition);
      }
      Chain chain() const {
        commit();
        return OverrideOpt<Chain,ClOpt>::chain();
      }
  };

  class OverrideChain {
    public:
      OverrideChain() {
      }
      OverrideChain chain() const {
        return *this;
      }
      template <typename ClOpt>
      static OverrideOpt<OverrideChain,ClOpt,OverrideOptWith> overrideOpt(ClOpt& target) {
        return OverrideOpt<OverrideChain,ClOpt,OverrideOptWith>(target);
      }
      template <typename Any>
      Any passSomeValue(Any any) {
        return any;
      }
      template <typename Any>
      Any const& passSomeCRef(Any const& any) {
        return any;
      }
  };

}
