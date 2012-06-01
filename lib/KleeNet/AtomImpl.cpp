#include "AtomImpl.h"

#include "klee/Expr.h"

using namespace kleenet;



ConcreteAtom::ConcreteAtom(Data data) : data(data) {
}

bool ConcreteAtom::operator==(net::DataAtom const& as) const {
  return data == static_cast<ConcreteAtom const&>(as).data;
}
bool ConcreteAtom::operator<(net::DataAtom const& as) const {
  return data < static_cast<ConcreteAtom const&>(as).data;
}

ConcreteAtom::operator klee::ref<klee::Expr>() const {
  return klee::ref<klee::Expr>(klee::ConstantExpr::alloc(data,8*sizeof(Data)));
}


SymbolicAtom::SymbolicAtom(klee::ref<klee::Expr> expr) : expr(expr) {
}
// symbolic data is expensive to compare, so we say it is always unequal,
// unless we are comparing the same object.
bool SymbolicAtom::operator==(net::DataAtom const& as) const {
  return expr.get() == static_cast<SymbolicAtom const&>(as).expr.get();
}
bool SymbolicAtom::operator<(net::DataAtom const& as) const {
  return expr.get() < static_cast<SymbolicAtom const&>(as).expr.get();
}

SymbolicAtom::operator klee::ref<klee::Expr>() const {
  return expr;
}


template <typename Child> class AtomIsA : public net::DataAtomT<Child> {
  private:
    // TODO: This implementation should be removed, and just left
    // unimplemented to trigger link-time errors.
    bool operator<(net::DataAtom const& that) const {
      assert(0);
    }
  public:
    bool operator==(net::DataAtom const& that) const {
      return this->sameClass(that);
    }
};

klee::ref<klee::Expr> kleenet::dataAtomToExpr(net::DataAtomHolder const& atomHolder) {
  net::DataAtom& atom = *static_cast<net::util::SharedPtr<net::DataAtom> >(atomHolder);
  if (AtomIsA<ConcreteAtom>() == atom)
    return static_cast<ConcreteAtom const&>(atom);
  if (AtomIsA<SymbolicAtom>() == atom)
    return static_cast<SymbolicAtom const&>(atom);
  return klee::ref<klee::Expr>(NULL);
}
