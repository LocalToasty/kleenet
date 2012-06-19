//===-- NameMangling.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <map>
#include <set>

#include "net/Node.h"

namespace klee {
  class Array;
}

namespace kleenet {
  class StateDistSymbols;

  struct NameMangler {
    virtual ~NameMangler() {}
    virtual klee::Array const* operator()(klee::Array const* array) const = 0;
    virtual klee::Array const* isReflexive(klee::Array const* array) const {
      return array; // nope
    }
  };

  struct NameManglerHolder {
    NameMangler& mangler; // heap allocated!
    static NameMangler& constructMangler(size_t const currentTx, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest);
    NameManglerHolder(size_t const currentTx, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest)
      : mangler(constructMangler(currentTx,distSymbolsSrc,distSymbolsDest)) {}
    ~NameManglerHolder() {
      delete &mangler;
    }
  };

  class LazySymbolTranslator { // cheap construction
    public:
      typedef std::set<klee::Array const*> Symbols;
      typedef std::map<klee::Array const*,klee::Array const*> TxMap;
    private:
      NameMangler& mangle;
    protected:
      TxMap txMap;
      Symbols* const preImageSymbols;
    public:
      LazySymbolTranslator(NameMangler& mangle, Symbols* const preImageSymbols = NULL)
        : mangle(mangle)
        , preImageSymbols(preImageSymbols) {
      }
      klee::Array const* operator()(klee::Array const* array);
      TxMap const& symbolTable() const {
        return txMap;
      }
  };

}
