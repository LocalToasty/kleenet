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
  chosenNameMangleType("sde-distributed-symbol-name-mangling"
    , llvm::cl::desc("The name mangling algorithm to use (Aliasing by default). The Duplicating algorithm is faster but will produce a significant amount of false positives, while Aliasing prevents symbols to separate over time.")
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
    DuplicatingNameMangler(std::string const designation, net::Node const src, net::Node const dest)
      : appendToName("{" + designation + ":" + llvm::itostr(src.id) + "->" + llvm::itostr(dest.id) + "}") {
    }
    klee::Array const* operator()(klee::Array const* array) const {
      return new klee::Array(array->name + appendToName, array->size);
    }
  };
  struct AliasingNameMangler : NameMangler {
    std::string const designation;
    StateDistSymbols& distSymbolsSrc;
    StateDistSymbols& distSymbolsDest;
    AliasingNameMangler(std::string const designation, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest)
      : designation(designation)
      , distSymbolsSrc(distSymbolsSrc)
      , distSymbolsDest(distSymbolsDest)
      {
    }
    klee::Array const* operator()(klee::Array const* array) const {
      return distSymbolsSrc.locate(array, designation, &distSymbolsDest);
    }
    klee::Array const* isReflexive(klee::Array const* array) const {
      return distSymbolsSrc.locate(array, designation, &distSymbolsSrc /*!*/);
    }
  };

}

using namespace kleenet;

NameMangler& NameManglerHolder::constructMangler(std::string const designation, StateDistSymbols& distSymbolsSrc, StateDistSymbols& distSymbolsDest) {
  switch (chosenNameMangleType) {
    case NMT_DUPLICATING:
      return *(new DuplicatingNameMangler(designation,distSymbolsSrc.node,distSymbolsDest.node));
    case NMT_ALIASING:
      return *(new AliasingNameMangler(designation,distSymbolsSrc,distSymbolsDest));
  }
  klee::klee_error("No valid name mangler selected.");
}

klee::Array const* LazySymbolTranslator::operator()(klee::Array const* array) {
  if (preImageSymbols)
    preImageSymbols->insert(array);
  klee::Array const*& it = txMap[array];
  if (!it) {
    it = mangle(array);
    if (translatedSymbols)
      translatedSymbols->insert(it);
  }
  return it;
}
