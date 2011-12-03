#include "kleenet/NetExecutorBuilder.h"

#include "NetExecutor.h"

using namespace kleenet;

klee::Interpreter* NetExecutorBuilder::create(const klee::Interpreter::InterpreterOptions& opts, InterpreterHandler* ih) {
  return new Executor(opts, ih);
}
