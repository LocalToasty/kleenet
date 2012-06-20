//===-- kexPPrinter.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <sstream>
#include <ostream>

namespace klee {
  class ConstraintManager;
  template <typename> class ref;
  class Expr;
}

namespace kleenet {
  namespace kexpp { // Pretty printing that has to do with Klee EXpressions
    template <typename T>
    struct ppHelper {
      static char buf[1024]; // have fun linking if `T != void`
    };
    template <>
    struct ppHelper<klee::ConstraintManager> {
      static void pprint(std::ostream& str, klee::ConstraintManager const& obj);
    };
    template <>
    struct ppHelper<klee::ref<klee::Expr> > {
      static void pprint(std::ostream& str, klee::ref<klee::Expr> const& obj);
    };
    template <typename DD, typename T>
    struct pp {
      static void pprint(T const& obj, char const* prefix) {
        if (DD::enable) {
          std::stringstream str;
          ppHelper<T>::pprint(str,obj);
          char const dummy = 0;
          char const* p = &dummy;
          while (str.getline(ppHelper<void>::buf,sizeof ppHelper<void>::buf / sizeof *ppHelper<void>::buf)) {
            DD::cout << p << ppHelper<void>::buf << DD::endl;
            p = prefix;
          }
        }
      }
    };
  }

  template <typename DD, typename T>
  void pprint(DD, T const& t, char const* prefix = 0) {
    char dummy = 0;
    kexpp::pp<DD,T>::pprint(t,prefix?prefix:&dummy);
  }
}
