#include "ExprBuilder.h"

#include "klee/Expr.h"
#include "klee_headers/Context.h"

#include <vector>

namespace {
  static size_t beginEndianRange(size_t begin, size_t end) {
    assert(begin <= end);
    return klee::Context::get().isLittleEndian() ? begin : (end - 1); // underflow is NOT a problem
  }
  static size_t endEndianRange(size_t begin, size_t end) {
    assert(begin <= end);
    return klee::Context::get().isLittleEndian() ? end : (begin - 1); // underflow is NOT a problem
  }
  static size_t incEndianRange() {
    return klee::Context::get().isLittleEndian() ? (size_t)+1 : (size_t)-1; // underflow is NOT a problem
  }
}

using namespace kleenet;

ExprBuilder::RefExpr ExprBuilder::buildRead8(klee::Array const* array, size_t offset) {
  return klee::ReadExpr::alloc( // needs review XXX
      klee::UpdateList(array,0/*UpdateList can handle null-heads, it simly doesn't have a pointer to the most recent update, ... I think*/)
    , klee::ConstantExpr::alloc(offset,array->getDomain())
  );
}

ExprBuilder::RefExpr ExprBuilder::buildConcat(RefExpr msb, RefExpr lsb) {
  RefExpr arr[2] = {lsb,msb};
  return klee::ConcatExpr::create(arr[beginEndianRange(0,2)],arr[beginEndianRange(0,2)+incEndianRange()]);
}

ExprBuilder::RefExpr ExprBuilder::buildCompleteRead(klee::Array const* array) {
  size_t const begin = beginEndianRange(0,array->size), inc = incEndianRange(), end = endEndianRange(0,array->size);
  assert(begin != end && "Cannot build a read expression of a zero length array.");
  RefExpr cat = buildRead8(array,begin);
  for (size_t i = begin+inc; i != end; i += inc) {
    cat = klee::ConcatExpr::create(buildRead8(array,i),cat);
  }
  return cat;
}

ExprBuilder::RefExpr ExprBuilder::buildEquality(ExprBuilder::RefExpr lhs, ExprBuilder::RefExpr rhs) {
  return klee::EqExpr::create(lhs,rhs);
}

ExprBuilder::RefExpr ExprBuilder::buildEquality(klee::Array const* lhs, klee::Array const* rhs) {
  return buildEquality(buildCompleteRead(lhs),buildCompleteRead(rhs));
}

ExprBuilder::RefExpr ExprBuilder::buildImplication(ExprBuilder::RefExpr premise, ExprBuilder::RefExpr conclusion) {
  return klee::OrExpr::create(klee::NotExpr::create(assertTrue(premise)), assertTrue(conclusion));
}

ExprBuilder::RefExpr ExprBuilder::makeZeroBits(klee::Expr::Width width) {
  return klee::ConstantExpr::fromMemory(&*std::vector<uint8_t>((width+7)/8,0).begin(),width);
}
ExprBuilder::RefExpr ExprBuilder::makeOneBits(klee::Expr::Width width) {
  //return klee::ConstantExpr::fromMemory(&*std::vector<uint8_t>((width+7)/8,0xFF).begin(),width);
  return klee::NotExpr::create(makeZeroBits(width));
}

ExprBuilder::RefExpr ExprBuilder::assertTrue(RefExpr expr) {
  // for some odd reason NeExpr (not-equal) are evil ...
  return klee::NotExpr::create(klee::EqExpr::create(makeZeroBits(expr->getWidth()),expr));
}
ExprBuilder::RefExpr ExprBuilder::assertFalse(RefExpr expr) {
  return klee::EqExpr::create(makeZeroBits(expr->getWidth()),expr);
}

ExprBuilder::RefExpr ExprBuilder::makeTrue() {
  return ExprBuilder::makeOneBits(klee::Expr::Bool);
}
ExprBuilder::RefExpr ExprBuilder::makeFalse() {
  return ExprBuilder::makeZeroBits(klee::Expr::Bool);
}


