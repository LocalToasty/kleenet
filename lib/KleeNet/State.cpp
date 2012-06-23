#include "kleenet/State.h"

#include "NetExecutor.h"

#include "klee/ExecutionState.h"
#include "klee_headers/PTree.h"
#include "klee_headers/Common.h"

#include "net/util/debug.h"

#define DD net::DEBUG<net::debug::slack>

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
  if (configurationData && with.configurationData) {
    ConfigurationData& myConfig = configurationData->self();
    ConfigurationData& theirConfig = with.configurationData->self();
    klee::klee_warning("mergeConstraints not yet implemented; configuration objects: %p -> %p",(void*)&myConfig,(void*)&theirConfig);
  } else {
    DD::cout << "bypassing mergeConstraints because at least one of the states doesn't have a configuration" << DD::endl;
  }
}

Executor* State::getExecutor() const {
  return executor;
}
