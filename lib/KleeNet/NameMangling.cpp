#include "NameMangling.h"

#include "klee/Expr.h"
#include "klee_headers/Common.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"

#include "DistributedConstraints.h"

namespace {
  enum NameMangleType {
      NMT_DUPLICATING
    , NMT_ALIASING
  };
  llvm::cl::opt<NameMangleType>
  chosenNameMangleType("distributed-symbol-name-mangling"
    , llvm::cl::desc("The name mangling algorithm to use (Aliasing by default). The Duplicating algorithm is faster but will produce a significant amount of false positives, while Aliasing prevents symbols to separate over time. This is a KleeNet extension.")
    , llvm::cl::values(clEnumValN(NMT_DUPLICATING
                                 , "duplicating"
                                 , "Duplicating NameMangler")
                     , clEnumValN(NMT_ALIASING
                                 , "aliasing"
                                 , "Aliasing NameMangler")
                     , clEnumValEnd)
    , llvm::cl::init(NMT_ALIASING)
  );
}

namespace kleenet {

  struct DuplicatingNameMangler : NameMangler{ // cheap construction
    std::string const appendToName;
    DuplicatingNameMangler(size_t const currentTx, net::Node const src, net::Node const dest)
      : appendToName("{tx" + llvm::utostr(currentTx) + ":" + llvm::itostr(src.id) + "->" + llvm::itostr(dest.id) + "}") {
    }
    klee::Array const* operator()(klee::Array const* array) const {
      return new klee::Array(array->name + appendToName, array->size);
    }
  };
  struct AliasingNameMangler : NameMangler {
    size_t const currentTx;
    StateDistSymbols& distSymbolsSrc;
    StateDistSymbols& distSymbolsDest;
    AliasingNameMangler(size_t const currentTx, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest)
      : currentTx(currentTx)
      , distSymbolsSrc(distSymbolsSrc)
      , distSymbolsDest(distSymbolsDest)
      {
    }
    klee::Array const* operator()(klee::Array const* array) const {
      return distSymbolsSrc.locate(array, currentTx, &distSymbolsDest);
    }
    klee::Array const* isReflexive(klee::Array const* array) const {
      return distSymbolsSrc.locate(array, currentTx, &distSymbolsSrc /*!*/);
    }
  };

}

using namespace kleenet;

NameMangler& NameManglerHolder::constructMangler(size_t const currentTx, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest) {
  switch (chosenNameMangleType) {
    case NMT_DUPLICATING:
      return *(new DuplicatingNameMangler(currentTx,distSymbolsSrc.node,distSymbolsDest.node));
    case NMT_ALIASING:
      return *(new AliasingNameMangler(currentTx,distSymbolsSrc,distSymbolsDest));
  }
  klee::klee_error("No valid name mangler selected.");
}

klee::Array const* LazySymbolTranslator::operator()(klee::Array const* array) {
  if (preImageSymbols)
    preImageSymbols->insert(array);
  klee::Array const*& it = txMap[array];
  if (!it)
    it = mangle(array);
  return it;
}
