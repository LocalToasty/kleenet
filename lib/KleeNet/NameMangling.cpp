#include "NameMangling.h"

#include "klee/Expr.h"
#include "llvm/ADT/StringExtras.h"

#include "DistributedConstraints.h"

namespace kleenet {

  struct DuplicatingNameMangler : NameMangler{ // cheap construction
    std::string const appendToName;
    DuplicatingNameMangler(size_t const currentTx, net::Node const src, net::Node const dest)
      : appendToName("{tx" + llvm::utostr(currentTx) + ":" + llvm::utostr(src.id) + "->" + llvm::utostr(dest.id) + "}") {
    }
    klee::Array const* operator()(klee::Array const* array) const {
      return new klee::Array(array->name + appendToName, array->size);
    }
  };
  struct AliasingNameMangler {
  };

}

using namespace kleenet;

NameMangler& NameManglerHolder::constructMangler(size_t const currentTx, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest) {
  return *(new DuplicatingNameMangler(currentTx,distSymbolsSrc.node,distSymbolsDest.node)); // XXX implemnt decently
}

klee::Array const* LazySymbolTranslator::operator()(klee::Array const* array) {
  if (preImageSymbols)
    preImageSymbols->insert(array);
  klee::Array const*& it = txMap[array];
  if (!it)
    it = mangle(array);
  return it;
}
