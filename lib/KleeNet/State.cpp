#include "kleenet/State.h"

#include "NetExecutor.h"
#include "ConfigurationData.h"
#include "ConstraintSet.h"
#include "ExprBuilder.h"

#include "klee/ExecutionState.h"
#include "klee_headers/PTree.h"
#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"
#include "klee_headers/TimingSolver.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/CommandLine.h"

#include <vector>
#include <iterator>

#include "net/util/debug.h"
#include "kexPPrinter.h"

#define DD net::DEBUG<net::debug::external1 | net::debug::transmit>

namespace {
  llvm::cl::opt<bool>
  addExtraPacketSymbols("sde-add-packet-symbols"
    , llvm::cl::desc("If you activate this option you will see the actual packet data that travelled through the virtual wire as individual symbols. Default: off.")
  );

}

using namespace kleenet;

State* State::forceFork() {
  assert(executor);
  klee::ExecutionState* const es = static_cast<klee::ExecutionState*>(this);
  assert(es->ptreeNode);
  klee::ExecutionState *ns = es->branch();
  assert(ns != es);
  // the engine has to be informed about our faked state.
  executor->addedState(ns);

  assert(es->ptreeNode);
  es->ptreeNode->data = 0;
  std::pair<klee::PTreeNode*, klee::PTreeNode*> res =
    executor->getPTree()->split(es->ptreeNode, ns, es);
  ns->ptreeNode = res.first;
  es->ptreeNode = res.second;

  return ns;
}

bool State::transferConstraints(State& onto) {
  bool isFeasible = true; // we are gullible sons of *******
  if (configurationData && onto.configurationData) {
    std::vector<klee::ref<klee::Expr> > constraints =
      ConstraintSet(TransmissionKind::merge,configurationData->self()).extractFor(onto.configurationData->self()).extractConstraints(ConstraintSetTransfer::FORCEALL);

    DD::cout << "ConstraintSet:" << DD::endl << "  "; pprint(DD(), onto.executionState()->constraints, "  ");

    for (std::vector<klee::ref<klee::Expr> >::const_iterator it = constraints.begin(), end = constraints.end(); isFeasible && it != end; ++it) {
      DD::cout << "________________________________________________________________________________" << DD::endl;
      DD::cout << " . I got a constraint to transfer: " << DD::endl;
      DD::cout << " .   # "; pprint(DD(), *it, " .   # ");
      DD::cout << " . simplified to: " << DD::endl;
      klee::ref<klee::Expr> simplified = onto.executionState()->constraints.simplifyExpr(*it);
      DD::cout << " .   # "; pprint(DD(), simplified, " .   # ");
      klee::Solver::Validity validity;
      bool success = executor->getTimingSolver()->evaluate(*(onto.executionState()),simplified,validity);
      assert(success && "Unhandled solver error");
      switch (validity) {
        case klee::Solver::True:
          DD::cout << " . Ignoring this constraint, because it's already given by the current constraint set." << DD::endl;
          break;
        case klee::Solver::False:
          DD::cout << " . This constraint is incompatible with the current constraint set. This is a false positive." << DD::endl;
          isFeasible = false;
          break;
        case klee::Solver::Unknown:
          onto.executionState()->constraints.addConstraint(simplified);
          break;
      }
      DD::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << DD::endl;
    }

    DD::cout << "EOF transferConstraints" << DD::endl << DD::endl;
  } else {
    DD::cout << "bypassing transferConstraints because at least one of the states doesn't have a configuration (i.e. wasn't ever involved in a communication)" << DD::endl;
  }
  return isFeasible;
}

klee::Array const* State::makeNewSymbol(std::string name, size_t size) {
  klee::Array const* const array = new klee::Array(name,size);
  if (addExtraPacketSymbols) {
    klee::ExecutionState& es = *executionState();
    klee::MemoryObject const* const mo = es.getExecutor()->memory->allocate(size,false,true,NULL);
    mo->setName(name);
    klee::ObjectState* const ose = new klee::ObjectState(mo,array);
    ose->initializeToZero();
    es.addressSpace.bindObject(mo,ose);
    es.addSymbolic(mo,array);
  }
  return array;
}

Executor* State::getExecutor() const {
  return executor;
}

klee::ExecutionState const* State::executionState() const {
  return static_cast<klee::ExecutionState const*>(this);
}

klee::ExecutionState* State::executionState() {
  return static_cast<klee::ExecutionState*>(this);
}
