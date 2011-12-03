#include "kleenet/ExecutorInjector.h"

namespace klee {
  Searcher *constructUserSearcher(Executor &executor);
}

namespace kleenet {

  ExecutorInjector::ExecutorInjector() {}

  klee::Searcher* ExecutorInjector::constructUserSearcher(klee::Executor& e) {
    return klee::constructUserSearcher(e);
  }

}
