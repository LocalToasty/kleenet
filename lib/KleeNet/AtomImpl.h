#pragma once

#include <stdint.h>

#include "net/DataAtom.h"

#include "klee/util/Ref.h"

namespace klee {
  class Expr;
}

namespace kleenet {

  class ConcreteAtom : public net::DataAtomT<ConcreteAtom> {
    private:
      typedef uint8_t Data;
      Data data;
    public:
      explicit ConcreteAtom(Data);
      virtual bool operator==(net::DataAtom const&) const;
      virtual bool operator<(net::DataAtom const&) const;
      operator klee::ref<klee::Expr>() const;
  };

  class SymbolicAtom : public net::DataAtomT<SymbolicAtom> {
    private:
      klee::ref<klee::Expr> expr;
    public:
      explicit SymbolicAtom(klee::ref<klee::Expr>);
      virtual bool operator==(net::DataAtom const&) const;
      virtual bool operator<(net::DataAtom const&) const;
      operator klee::ref<klee::Expr>() const;
  };


  klee::ref<klee::Expr> dataAtomToExpr(net::DataAtomHolder const& atomHolder);
}

