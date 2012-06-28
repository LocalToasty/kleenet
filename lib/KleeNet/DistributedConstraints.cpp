#include "DistributedConstraints.h"

#include "net/util/SharedPtr.h"

#include "klee/Expr.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"

#include "net/util/debug.h"

#include <tr1/unordered_map>
#include <tr1/unordered_set>

#define DD net::DEBUG<net::debug::external1>

namespace kleenet {
  // A locator for an array object of a particluar distributed symbol for arbitrary states.
  struct DistributedSymbol {
    // TODO: in the future, we should have a more intelligent data structure here. Maps are uncool, as opposed to bowties.
    std::tr1::unordered_map<StateDistSymbols const*,DistributedArray const*> of;
    std::string const globalName;
    explicit DistributedSymbol(std::string const globalName) : of(), globalName(globalName) {}
    DistributedSymbol(DistributedSymbol const&); // not implemented
  };

  struct StateDistSymbols_impl {
    StateDistSymbols& parent;
    // Note that the keys in `knownArrays` are always pure klee::Array objects, never DistributedArray objects.
    typedef std::tr1::unordered_map<klee::Array const*,DistributedArray const*> KnownArrays;
    KnownArrays knownArrays;
    // These are ALL DistributedArrays objects associated with this state (note that knownArrays may note contain those, if they don't correspond to a local klee::Array)
    typedef std::tr1::unordered_set<DistributedArray const*> AllDistributedArrays;
    AllDistributedArrays allDistributedArrays;

    StateDistSymbols_impl(StateDistSymbols& parent) : parent(parent), knownArrays() {}
    StateDistSymbols_impl(StateDistSymbols_impl const& copyFrom, StateDistSymbols& parent);

    DistributedArray const& castOrMake(klee::Array const&, std::string);
    bool taintLocalSymbols() const;
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
        if (state->pimpl.taintLocalSymbols())
          return name + std::string("@") + llvm::itostr(state->node.id);
        return name;
      }
      static std::string makeGlobalName(klee::Array const* buildFrom, std::string designation, net::Node src) {
        return std::string() + buildFrom->name + "{node" + llvm::itostr(src.id) + ":" + designation + "}";

      }
      DistributedArray(StateDistSymbols* state, klee::Array const* buildFrom, std::string designation, net::Node src)
        : klee::Array(taint(state,makeGlobalName(buildFrom,designation,src)), buildFrom->size)
        , metaSymbol(new DistributedSymbol(makeGlobalName(buildFrom,designation,src)))
        {
        state->pimpl.allDistributedArrays.insert(this);
        assert(!llvm::isa<DistributedArray>(*buildFrom));
        metaSymbol->of[state] = this;
        DD::cout << "| ############## [state " << state << "] +Symbol[" << this << "] " << this->name << ", +MetaSymbol[" << &*metaSymbol << "] " << metaSymbol->globalName << "; built from " << buildFrom->name << DD::endl;
      }
      DistributedArray(StateDistSymbols* state, DistributedArray const& from)
        : klee::Array(taint(state,from.metaSymbol->globalName),from.size) // note: this is not the copy-ctor!
        , metaSymbol(from.metaSymbol)
        {
        state->pimpl.allDistributedArrays.insert(this);
        DistributedArray const*& slot = metaSymbol->of[state];
        assert(!slot);
        slot = this;
        DD::cout << "| ############## [state " << state << "] +Symbol[" << this << "] " << this->name << DD::endl;
      }
  };
}

using namespace kleenet;


StateDistSymbols::StateDistSymbols(net::Node const node)
  : pimpl(*(new StateDistSymbols_impl(*this)))
  , node(node)
  {
}
StateDistSymbols::StateDistSymbols(StateDistSymbols const& from)
  : pimpl(*(new StateDistSymbols_impl(from.pimpl,*this)))
  , node(from.node)
  {
}
StateDistSymbols::~StateDistSymbols() {
  delete &pimpl;
}

bool StateDistSymbols_impl::taintLocalSymbols() const {
  return true;
}

StateDistSymbols_impl::StateDistSymbols_impl(StateDistSymbols_impl const& copyFrom, StateDistSymbols& parent)
  : parent(parent)
  , knownArrays(copyFrom.knownArrays)
  , allDistributedArrays(copyFrom.allDistributedArrays)
  {
  for (AllDistributedArrays::const_iterator it = allDistributedArrays.begin(), end = allDistributedArrays.end(); it != end; ++it) {
    (*it)->metaSymbol->of[&parent] = *it;
  }
}

DistributedArray const& StateDistSymbols_impl::castOrMake(klee::Array const& from, std::string const designation) {
  if (llvm::isa<DistributedArray const>(from))
    return static_cast<DistributedArray const&>(from);
  DistributedArray const*& known = knownArrays[&from];
  if (!known)
    known = new DistributedArray(&parent,&from,designation,parent.node);
  return *known;
}

klee::Array const* StateDistSymbols::locate(klee::Array const* const array, std::string const designation, StateDistSymbols* inState) {
  assert(array);
  assert(inState);
  DistributedArray const& da = pimpl.castOrMake(*array, designation);
  DistributedArray const*& entry = da.metaSymbol->of[inState];
  if (!entry)
    entry = new DistributedArray(inState,da);
  return entry;
}

void StateDistSymbols::iterateArrays(net::util::DynamicFunctor<klee::Array const*> const& func) const {
  for (StateDistSymbols_impl::AllDistributedArrays::const_iterator it = pimpl.allDistributedArrays.begin(), end = pimpl.allDistributedArrays.end(); it != end; ++it) {
    func(*it);
  }
}

bool StateDistSymbols::isDistributed(klee::Array const* array) const {
  return llvm::isa<DistributedArray const>(*array);
}
