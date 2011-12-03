#pragma once

#include <assert.h>

#include "net/StateMapper.h"

#include "Clonable.h"

namespace net {

  /****************************************************************************
   * StateMapperIntermediateBase - Base Class to inherit from                 *
   ****************************************************************************/
  /// This template class is used to glue a concrete derived state mapper class
  /// to a concrete mapping information class. By inheriting form
  /// StateMapperIntermediateBase<X> we can automatically glue the class to the
  /// mapping information X.
  /// This class is abstract, do not instantiate.
  template <class MI> class StateMapperIntermediateBase : public StateMapper {
    // MI is the actual type, usually not the MappingInformation base class.
    protected:
      StateMapperIntermediateBase(StateMapperInitialiser const& initialiser,
                                  BasicState* rootState,
                                  MI* defaultState)
        : StateMapper(initialiser,rootState,defaultState) {
        defaultState->setCloner(&Cloner<MI>::getCloner());
      }

      /// Get the actual mapping information object of the passed state as its
      /// actual type (which we know from this class on).
      MI* stateInfo(BasicState const& state) const {
        return stateInfo(&state);
      }
      /// Pointer variant of stateInfo(&).
      MI* stateInfo(BasicState const* state) const {
        // We cannot afford dynamic_cast. So we have to settle for static_cast.
        MI* info = static_cast<MI*>(MI::retrieveDependant(state));
        assert(info);
        return info;
      }
  };

}

