#include "kleenet/State.h"

#include "NetExecutor.h"
#include "ConfigurationData.h"

#include "klee/ExecutionState.h"
#include "klee_headers/PTree.h"
#include "klee_headers/Common.h"

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

void State::transferConstraints(State& onto) {
  if (configurationData && onto.configurationData) {
    ConfigurationData& myConfig = configurationData->self();
    ConfigurationData& theirConfig = onto.configurationData->self();
    klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(onto);
    size_t const existingSymbols = receiver.arrayNames.size();
    DD::cout << "transferring Constraints; configuration objects: " << &myConfig << " -> " << &theirConfig << DD::endl;
    std::vector<klee::Array const*> ownArrays;
    myConfig.distSymbols.iterateArrays(
      net::util::FunctorBuilder<klee::Array const*,net::util::DynamicFunctor,net::util::IterateOperator>::build(
        std::back_inserter(ownArrays)
      )
    );

    for (std::vector<klee::Array const*>::const_iterator it = ownArrays.begin(), end = ownArrays.end(); it != end; ++it) {
      DD::cout << " - I got array[" << *it << "] of name: " << (*it)->name << DD::endl;
    }

    std::vector<klee::ref<klee::Expr> > constraints = myConfig.cg.eval(ownArrays);

    std::set<klee::Array const*> newSymbols;

    NameManglerHolder nmh("<*>",myConfig.distSymbols,theirConfig.distSymbols);
    ReadTransformator rt(nmh.mangler,constraints,NULL,&newSymbols);

    for (std::vector<klee::ref<klee::Expr> >::const_iterator it = constraints.begin(), end = constraints.end(); it != end; ++it) {
      DD::cout << "________________________________________________________________________________" << DD::endl;
      DD::cout << " . I got a constraint to transfer: " << DD::endl;
      DD::cout << " .   + "; pprint(DD(), *it, "     + ");
      klee::ref<klee::Expr> const sc = rt(*it);
      DD::cout << " . I will send the translated constraint: " << DD::endl;
      DD::cout << " .   # "; pprint(DD(), sc, " .   # ");
      DD::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << DD::endl;
      receiver.constraints.addConstraint(sc);
    }

    for (std::set<klee::Array const*>::const_iterator it = newSymbols.begin(), end = newSymbols.end(); it != end; ++it) {
      DD::cout << " ~~> " << (*it)->name << DD::endl;
      receiver.arrayNames.insert((*it)->name);
    }

    DD::cout << "New symbols for receiver state: " << (receiver.arrayNames.size() - existingSymbols) << DD::endl;

    DD::cout << "EOF transferConstraints" << DD::endl << DD::endl;
  } else {
    DD::cout << "bypassing transferConstraints because at least one of the states doesn't have a configuration (i.e. wasn't ever involved in a communication)" << DD::endl;
  }
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
