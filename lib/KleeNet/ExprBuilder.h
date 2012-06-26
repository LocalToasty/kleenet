//===-- ExprBuilder.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include "klee/Expr.h"

namespace klee {
  class Array;
}

namespace kleenet {
  struct ExprBuilder {
    public:
      typedef klee::ref<klee::Expr> RefExpr;
      static RefExpr buildRead8(klee::Array const* array, size_t offset);
      static RefExpr buildCompleteRead(klee::Array const* array);
      static RefExpr buildEquality(RefExpr,RefExpr);
      static RefExpr buildEquality(klee::Array const*,klee::Array const*);
      template <typename Ex>
      static RefExpr build(RefExpr lhs, RefExpr rhs) {
        return Ex::alloc(lhs,rhs);
      }
      static RefExpr makeZeroBits(klee::Expr::Width);
      static RefExpr makeOneBits(klee::Expr::Width);
      static RefExpr assertTrue(RefExpr exp);
      static RefExpr assertFalse(RefExpr exp);

      template <typename BinaryOperator, typename UnaryOperator, typename InputIterator>
      static RefExpr foldl_map(BinaryOperator binOp, RefExpr start, UnaryOperator unOp, InputIterator begin, InputIterator end) {
        RefExpr expr = start;
        for (InputIterator it = begin; it != end; ++it)
          expr = binOp(expr,unOp(*it));
        return expr;
      }
      template <typename InputIterator>
      static RefExpr conjunction(InputIterator begin, InputIterator end) {
        return foldl_map(klee::AndExpr::alloc,ExprBuilder::makeOneBits(klee::Expr::Bool),assertTrue,begin,end);
      }
      template <typename InputIterator>
      static RefExpr disjunction(InputIterator begin, InputIterator end) {
        return foldl_map(klee::OrExpr::alloc,ExprBuilder::makeZeroBits(klee::Expr::Bool),assertTrue,begin,end);
      }
  };
}
