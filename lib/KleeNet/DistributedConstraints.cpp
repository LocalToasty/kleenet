#include "DistributedConstraints.h"

#include "net/util/SharedPtr.h"

#include "klee/Expr.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"

namespace kleenet {
  // A locator for an array object of a particluar distributed symbol for arbitrary states.
  struct DistributedSymbol {
    // TODO: in the future, we should have a more intelligent data structure here. Maps are uncool, as opposed to bowties.
    std::map<StateDistSymbols const*,DistributedArray const*> of;
    DistributedSymbol() : of() {}
    DistributedSymbol(DistributedSymbol const&); // not implemented
  };

  class DistributedArray : public klee::Array {
    private:
      DistributedArray(DistributedArray const&); // not implemented
    public:
      net::util::SharedPtr<DistributedSymbol> const metaSymbol;
      virtual bool isBaseArray() const {return false;}
      static bool classof(klee::Array const* array) {
        return !array->isBaseArray();
      }

      virtual ~DistributedArray() {
      }
      DistributedArray(StateDistSymbols* state, klee::Array const* buildFrom, size_t forTx, net::Node src)
        : klee::Array(std::string() + buildFrom->name + "{node" + llvm::utostr(src.id) + ":tx" + llvm::utostr(forTx) + "}",buildFrom->size)
        , metaSymbol(new DistributedSymbol())
        {
        assert(!llvm::isa<DistributedArray>(*buildFrom));
        metaSymbol->of[state] = this;
      }
      explicit DistributedArray(StateDistSymbols* state, DistributedArray const& from)
        : klee::Array(from.name,from.size) // note: this is not the copy-ctor!
        , metaSymbol(from.metaSymbol)
        {
        DistributedArray const*& slot = metaSymbol->of[state];
        assert(!slot);
        slot = this;
      }
  };
}

using namespace kleenet;

DistributedArray const& StateDistSymbols::castOrMake(klee::Array const& from, size_t const forTx) {
  if (!llvm::isa<DistributedArray const>(from)) {
    DistributedArray const*& known = knownArrays[forTx][&from];
    if (!known)
      known = new DistributedArray(this,&from,forTx,node);
    return *known;
  }
  return static_cast<DistributedArray const&>(from);
}

klee::Array const* StateDistSymbols::locate(klee::Array const* const array, size_t const forTx, StateDistSymbols* inState) {
  assert(array);
  assert(inState);
  DistributedArray const& da = castOrMake(*array, forTx);
  DistributedArray const*& entry = da.metaSymbol->of[inState];
  if (!entry)
    entry = new DistributedArray(inState,da);
  return entry;
}

bool StateDistSymbols::isDistributed(klee::Array const* array) const {
  return llvm::isa<DistributedArray const>(*array);
}
