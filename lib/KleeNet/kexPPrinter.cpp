#include "kexPPrinter.h"

#include "klee/util/ExprPPrinter.h"

namespace kleenet {
  namespace kexpp {
    template <>
    char ppHelper<void>::buf[] = {};
    void ppHelper<klee::ConstraintManager>::pprint(std::ostream& str, klee::ConstraintManager const& obj) {
      klee::ExprPPrinter::printConstraints(str,obj);
    }
    void ppHelper<klee::ref<klee::Expr> >::pprint(std::ostream& str, klee::ref<klee::Expr> const& obj) {
      if (obj.get())
        klee::ExprPPrinter::printSingleExpr(str,obj);
    }
  }
}
