#pragma once

/* Note: This information is supposed to be included by code files for local types, only.
 * If you are including this in a header file, something is probably wrong.
 */

#include <list>
#include <map>

#include "StateDependant.h"
#include "StateCluster.h"
#include "net/Time.h"

namespace net {
  class BasicState;
  template <typename SI> struct SchedulingInformationHandler;

  template <typename SI> struct SchedulingInformation : public StateDependant<SI> {
    friend class SchedulingInformationHandler<SI>;
    private:
      using StateDependant<SI>::setState;
      using StateDependant<SI>::setCloner;
    public:
      Time virtualTime;

      SchedulingInformation() : virtualTime(0) {}
      ~SchedulingInformation() {}

  };
  template <typename SI> struct SchedulingInformationHandler {
    protected:
      SchedulingInformationHandler() {}
    public:
      void equipState(BasicState* state) const {
        if (SI::retrieveDependant(state) == NULL) {
          SI* const si = new SI();
          si->setState(state);
          si->setCloner(&Cloner<SchedulingInformation<SI> >::getCloner());
        }
        // else: already equipped => do nothing
      }
      void releaseState(BasicState* state) const {
        if (SI::retrieveDependant(state)) {
          delete SI::retrieveDependant(state);
        }
      }
      SI* stateInfo(BasicState* state) const {
        return SI::retrieveDependant(state);
      }
  };
}

