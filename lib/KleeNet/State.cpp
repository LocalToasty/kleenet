#include "kleenet/State.h"

#include "NetExecutor.h"

#include "klee/ExecutionState.h"
#include "../Core/PTree.h"

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

void State::mergeConstraints(State& with) {
  std::cerr << "WARNING: mergeConstraints not yet implemented!" << std::endl; // FIXME
}
