#include "kleenet/State.h"

#include "NetExecutor.h"
#include "ConfigurationData.h"
#include "ConstraintSet.h"
#include "ExprBuilder.h"

#include "klee/ExecutionState.h"
#include "klee_headers/PTree.h"
#include "klee_headers/Common.h"
#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"

#include <vector>
#include <iterator>

#include "net/util/debug.h"
#include "kexPPrinter.h"

#define DD net::DEBUG<net::debug::external1>

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
  if (configurationData && onto.configurationData) {
    std::vector<klee::ref<klee::Expr> > constraints =
      ConstraintSet(TransmissionKind::merge,configurationData->self()).extractFor(onto.configurationData->self()).extractConstraints(ConstraintSetTransfer::FORCEALL);

    DD::cout << "ConstraintSet:" << DD::endl << "  "; pprint(DD(), onto.executionState()->constraints, "  ");

    for (std::vector<klee::ref<klee::Expr> >::const_iterator it = constraints.begin(), end = constraints.end(); it != end; ++it) {
      DD::cout << "________________________________________________________________________________" << DD::endl;
      DD::cout << " . I got a constraint to transfer: " << DD::endl;
      DD::cout << " .   # "; pprint(DD(), *it, " .   # ");
      DD::cout << " . simplified to: " << DD::endl;
      klee::ref<klee::Expr> simplified = onto.executionState()->constraints.simplifyExpr(*it);
      DD::cout << " .   # "; pprint(DD(), simplified, " .   # ");
      if (simplified->getKind() == klee::Expr::Constant) {
        if (llvm::cast<klee::ConstantExpr>(simplified)->isTrue()) {
          DD::cout << " . Ignoring this constraint, because it's already given by the current constraint set." << DD::endl;
        } else {
          DD::cout << " . This constraints is incompatible with the current constraint set. This is a false positive." << DD::endl;
          DD::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << DD::endl;
          return false;
        }
      }
      onto.executionState()->constraints.addConstraint(simplified);
      DD::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << DD::endl;
    }

    DD::cout << "EOF transferConstraints" << DD::endl << DD::endl;
  } else {
    DD::cout << "bypassing transferConstraints because at least one of the states doesn't have a configuration (i.e. wasn't ever involved in a communication)" << DD::endl;
  }
  return true;
}

klee::Array const* State::makeNewSymbol(std::string name, size_t size) {
  klee::ExecutionState& es = *executionState();
  klee::MemoryObject const* const mo = es.getExecutor()->memory->allocate(size,false,true,NULL);
  mo->setName(name);
  klee::Array const* const array = new klee::Array(name,mo->size);
  klee::ObjectState* const ose = new klee::ObjectState(mo,array);
  ose->initializeToZero();
  es.addressSpace.bindObject(mo,ose);
  es.addSymbolic(mo,array);
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
