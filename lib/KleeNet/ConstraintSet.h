//===-- ConstraintSet.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "TransmissionKind.h"

#include "net/util/SharedPtr.h"

#include "klee/Expr.h"

#include <vector>

namespace klee {
  class ExecutionState;
}

namespace kleenet {
  class ConfigurationData;
  class PerReceiverData;

  class ConstraintSet_impl;
  class ConstraintSetTransfer_impl;

  class ConstraintSetTransfer {
    friend class ConstraintSet_impl;
    private:
      struct IDel {
        void operator()(ConstraintSetTransfer_impl*) const;
      };
      typedef net::util::SharedPtr<ConstraintSetTransfer_impl,IDel> Pimpl;
      Pimpl pimpl;
      ConstraintSetTransfer(Pimpl const&);
    public:
      // Note that we use the default copy-ctor and default assignment, as we have sharing semantics.

      enum TxConstraintsTransmission {
        CLOSURE = 0,
        FORCEALL = 1,
        USERCHOICE /* only use in default parameters, do not allow the user to choose this (it wouldn't make sense) */
      };

      PerReceiverData& receiverData() const;
      std::vector<klee::ref<klee::Expr> > extractConstraints(TxConstraintsTransmission = USERCHOICE) const;
  };

  class ConstraintSet {
    private:
      struct IDel {
        void operator()(ConstraintSet_impl*) const;
      };
      net::util::SharedPtr<ConstraintSet_impl,IDel> pimpl;
    public:
      typedef std::vector<klee::ref<klee::Expr> >::const_iterator It;

      ConstraintSet(TransmissionKind::Enum, klee::ExecutionState&);
      ConstraintSet(TransmissionKind::Enum, ConfigurationData&);
      ConstraintSet(TransmissionKind::Enum, klee::ExecutionState&, It begin, It end);
      ConstraintSet(TransmissionKind::Enum, ConfigurationData&, It begin, It end);
      // Note that we use the default copy-ctor and default assignment, as we have sharing semantics.

      ConstraintSetTransfer extractFor(klee::ExecutionState&);
      ConstraintSetTransfer extractFor(ConfigurationData&);
  };
}
