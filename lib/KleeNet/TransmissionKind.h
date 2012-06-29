//===-- ConfigurationData.h -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace kleenet {
  struct TransmissionKind {
    enum Enum {
      tx,
      pull,
      merge
    };
  };
}
