#include "DistributedConstraints.h"

#include "net/util/SharedPtr.h"

#include "klee/Expr.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"

namespace kleenet {
  // A locator for an array object of a particluar distributed symbol for arbitrary states.
  struct DistributedSymbol {
    // TODO: in the future, we should have a more intelligent data structure here. Maps are uncool, as opposed to bowties.
    std::map<StateDistSymbols const*,DistributedArray*> of;
    DistributedSymbol() : of() {}
    DistributedSymbol(DistributedSymbol const&); // not implemented
  };

  class DistributedArray : public klee::Array {
    private:
      DistributedArray(DistributedArray const&); // not implemented
    public:
      net::util::SharedPtr<DistributedSymbol> const metaSymbol;
      static bool classof(klee::Array const* array) {
        return array->name.size() && (array->name[array->name.size()-1] == '}'); // slight hack?
      }

      virtual ~DistributedArray() {
      }
      DistributedArray(StateDistSymbols* state, klee::Array const* buildFrom, size_t forTx, net::Node src)
        : klee::Array(std::string() + buildFrom->name + "{tx" + llvm::utostr(forTx) + ":" + llvm::utostr(src.id) + "}",buildFrom->size)
        , metaSymbol(new DistributedSymbol())
        {
        assert(!llvm::isa<DistributedArray>(*buildFrom));
        metaSymbol->of[state] = this;
      }
      explicit DistributedArray(StateDistSymbols* state, DistributedArray const& from)
        : klee::Array(from.name,from.size) // note: this is not the copy-ctor!
        , metaSymbol(from.metaSymbol)
        {
        DistributedArray*& slot = metaSymbol->of[state];
        assert(!slot);
        slot = this;
      }
  };
}

using namespace kleenet;

DistributedArray& StateDistSymbols::castOrMake(klee::Array& from, size_t const forTx) {
  if (!llvm::isa<DistributedArray>(from)) {
    DistributedArray*& known = knownArrays[forTx][&from];
    assert(!known && "Reinserting object!");
    return *(known = new DistributedArray(this,&from,forTx,node));
  }
  return static_cast<DistributedArray&>(from);
}

klee::Array* StateDistSymbols::locate(klee::Array* const array, size_t const forTx, StateDistSymbols* inState) {
  assert(array);
  assert(inState);
  DistributedArray& da = castOrMake(*array, forTx);
  DistributedArray*& entry = da.metaSymbol->of[inState];
  if (!entry)
    entry = new DistributedArray(inState,da);
  return entry;
}
