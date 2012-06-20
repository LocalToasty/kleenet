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
    std::string const globalName;
    explicit DistributedSymbol(std::string const globalName) : of(), globalName(globalName) {}
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
      static std::string taint(StateDistSymbols* state, std::string const name) {
        if (state->taintLocalSymbols())
          return name + std::string("@") + llvm::utostr(state->node.id);
        return name;
      }
      static std::string makeGlobalName(klee::Array const* buildFrom, size_t forTx, net::Node src) {
        return std::string() + buildFrom->name + "{node" + llvm::utostr(src.id) + ":tx" + llvm::utostr(forTx) + "}";
      }
      DistributedArray(StateDistSymbols* state, klee::Array const* buildFrom, size_t forTx, net::Node src)
        : klee::Array(taint(state,makeGlobalName(buildFrom,forTx,src)), buildFrom->size)
        , metaSymbol(new DistributedSymbol(makeGlobalName(buildFrom,forTx,src)))
        {
        assert(!llvm::isa<DistributedArray>(*buildFrom));
        metaSymbol->of[state] = this;
      }
      DistributedArray(StateDistSymbols* state, DistributedArray const& from)
        : klee::Array(taint(state,from.metaSymbol->globalName),from.size) // note: this is not the copy-ctor!
        , metaSymbol(from.metaSymbol)
        {
        DistributedArray const*& slot = metaSymbol->of[state];
        assert(!slot);
        slot = this;
      }
  };
}

using namespace kleenet;

bool StateDistSymbols::taintLocalSymbols() const {
  return true;
}

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
