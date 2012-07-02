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
      static RefExpr buildImplication(RefExpr,RefExpr);
      static RefExpr buildConcat(RefExpr,RefExpr); // obeys endianness!
      template <typename Ex>
      static RefExpr build(RefExpr lhs, RefExpr rhs) {
        return Ex::create(lhs,rhs);
      }
      static RefExpr makeZeroBits(klee::Expr::Width);
      static RefExpr makeOneBits(klee::Expr::Width);
      static RefExpr makeFalse();
      static RefExpr makeTrue();
      static RefExpr assertFalse(RefExpr exp);
      static RefExpr assertTrue(RefExpr exp);


      struct ToExpr {
        RefExpr operator()(klee::Array const* array) const {
          return buildCompleteRead(array);
        }
        RefExpr operator()(RefExpr expr) const {
          return expr;
        }
      };

      template <typename BinaryOperator, typename UnaryOperator, typename InputIterator>
      static RefExpr foldl_map(BinaryOperator binOp, RefExpr start, UnaryOperator unOp, InputIterator begin, InputIterator end) {
        RefExpr expr = start;
        for (InputIterator it = begin; it != end; ++it)
          expr = binOp(expr,unOp(*it));
        return expr;
      }
      template <typename BinaryOperator, typename UnaryOperator, typename InputIterator>
      static RefExpr foldl1_map(BinaryOperator binOp, UnaryOperator unOp, InputIterator begin, InputIterator end) {
        InputIterator start = begin++;
        return foldl_map(binOp,*start,unOp,begin,end);
      }
      template <typename InputIterator>
      static RefExpr conjunction(InputIterator begin, InputIterator end) {
        return foldl_map(klee::AndExpr::alloc,ExprBuilder::makeTrue(),assertTrue,begin,end);
      }
      template <typename InputIterator>
      static RefExpr disjunction(InputIterator begin, InputIterator end) {
        return foldl_map(klee::OrExpr::alloc,ExprBuilder::makeFalse(),assertTrue,begin,end);
      }
      template <typename InputIterator>
      static RefExpr concat(InputIterator begin, InputIterator end) {
        return foldl1_map(buildConcat,ExprBuilder::ToExpr(),begin,end);
      }
  };
}
