#include "NetExecutor.h"

#include "llvm/Support/CommandLine.h"

#include "SpecialFunctionHandler.h" // the KleeNet version! It enhances the KLEE version
#include "kleenet/Searcher.h"
#include "kleenet/CustomSearcherFactory.h"

#include "net/PacketCache.h"

#include "NetUserSearcher.h"

#include <algorithm>
#include <iostream>

#include <net/util/debug.h>

#define DD net::DEBUG<net::debug::external2>

namespace klee {
  class ObjectState;
}

namespace llvm{
  class Twine;
}

namespace {
  enum DistributedTerminateBehaviour {
    DTB_singleTestCase,
    DTB_uniformTestCase,
    DTB_forceAllTestCase
  };
  llvm::cl::opt<DistributedTerminateBehaviour> distributedTerminateBehaviour(
    "sde-distributed-terminate",
    llvm::cl::desc("Decide what to do when a distributed scenario terminates (default: uniform)."),
    llvm::cl::values(
          clEnumValN(DTB_singleTestCase,
                     "single", "Create only one test case for the distributed scenario that terminates."),
          clEnumValN(DTB_uniformTestCase,
                     "uniform", "Create one test case for each state that belongs to the network that terminates. This is done using the same algorithm which may cause not all test cases to be produced (e.g. if they do not carry new information)."),
          clEnumValN(DTB_forceAllTestCase,
                     "force-all", "Create one test case for each state that belongs to the network that terminates. All test cases are forcibly created. This is the same behaviour as in KleeNet v0.1."),
          clEnumValEnd
    ),
    llvm::cl::init(DTB_uniformTestCase)
  );
}

namespace kleenet {
  struct NetExTHnd : KleeNet::TerminateStateHandler {
    private:
      Executor* e;
    protected:
      NetExTHnd(Executor* e) : e(e) {
      }
      virtual void term(klee::ExecutionState&) const = 0;
      Executor* getExecutor() const {
        return e;
      }
      virtual bool silent() const {
        return false;
      }
    public:
      inline void operator()(klee::ExecutionState* state) const {
        term(*state);
      }
      inline void operator()(klee::ExecutionState& state) const {
        term(state);
      }
      void operator()(klee::ExecutionState& state, std::vector<klee::ExecutionState*> const& appendix) const {
        for (std::vector<klee::ExecutionState*>::const_iterator it(appendix.begin()), end(appendix.end()); it != end; ++it) {
          (*it)->transferConstraints(state);
        }
        for (std::vector<klee::ExecutionState*>::const_iterator it(appendix.begin()), end(appendix.end()); it != end; ++it) {
          state.transferConstraints(**it);
        }
        if (!appendix.empty())
          e->netInterpreterHandler->incDScenariosExplored();
        (*this)(state);
        bool generateTestCases = true;
        if (!silent()) {
          switch (distributedTerminateBehaviour) {
            case DTB_uniformTestCase:
              std::for_each<std::vector<klee::ExecutionState*>::const_iterator,NetExTHnd const&>(appendix.begin(),appendix.end(),*this);
              break;
            case DTB_singleTestCase:
              generateTestCases = false;
              // Intentional fall through!
            case DTB_forceAllTestCase:
              for (std::vector<klee::ExecutionState*>::const_iterator it(appendix.begin()), end(appendix.end()); it != end; ++it) {
                if (generateTestCases)
                  e->netInterpreterHandler->processTestCase(**it,NULL,NULL);
                e->klee::Executor::terminateState(**it);
              }
          }
        }
      }
  };
}

using namespace kleenet;

Executor::Executor(const InterpreterOptions &opts,
                   InterpreterHandler *ih)
  : klee::Executor(opts,ih)
  , kleenet(this)
  , netInterpreterHandler(ih)
  , netSearcher(NULL)
  , kleeNet(kleenet) {
  kleenet::searcherautorun::SearcherAutoRun::linkme(); // stupid linker!
}

Searcher* Executor::getNetSearcher() const {
  return netSearcher;
}

void Executor::addedState(klee::ExecutionState* added) {
  addedStates.insert(added);
}

klee::PTree* Executor::getPTree() const {
  return processTree;
}

klee::SpecialFunctionHandler* Executor::newSpecialFunctionHandler() {
  // careful, we are in a different namespace than klee::Executor,
  // therefore we are refering here to kleenet::SpecialFunctionHandler!
  return new SpecialFunctionHandler(*this);
}

klee::Searcher* Executor::constructUserSearcher(klee::Executor& e) {
  net::PacketCacheBase* const pcb = kleenet.getPacketCache();
  if ((netSearcher = CustomSearcherFactory::attemptConstruction(CustomSearcherFactory::/*Precedence::*/CSFP_OVERRIDE_LEGACY,kleenet,pcb))) {
    return netSearcher;
  }
  klee::Searcher* ks;
  if ((ks = klee::Executor::constructUserSearcher(e))) {
    return ks;
  }
  if ((netSearcher = CustomSearcherFactory::attemptConstruction(CustomSearcherFactory::/*Precedence::*/CSFP_AMEND_LEGACY,kleenet,pcb))) {
    return netSearcher;
  }
  return NULL;
}

void Executor::run(klee::ExecutionState& initialState) {
  KleeNet::RunEnv knRunEnv(kleenet,&initialState);
  klee::Executor::run(initialState);
}

void Executor::terminateStateEarly_klee(klee::ExecutionState& state,
                                        llvm::Twine const& message) {
  klee::Executor::terminateStateEarly(state,message);
}

void Executor::terminateStateEarly(klee::ExecutionState& state,
                                   llvm::Twine const& message) {
  struct TSE : NetExTHnd {
    llvm::Twine const& message;
    TSE(Executor* e, llvm::Twine const& message) : NetExTHnd(e), message(message) {}
    void term(klee::ExecutionState& state) const {
      DD::cout << "[TSE] term(" << &state << ") {" << DD::endl;
      getExecutor()->terminateStateEarly_klee(state,message);
      DD::cout << "[TSE] } term(" << &state << ")" << DD::endl;
    }
  };
  TSE hnd(this,message);
  DD::cout << "[NetExecutor::terminateStateEarly] asking KleeNet to terminateCluster of " << (&state) << " /*" << message.str() << "*/" << DD::endl;
  kleenet.terminateCluster(state,hnd);
  DD::cout << "[NetExecutor::terminateStateEarly] EOF" << DD::endl;
}

void Executor::terminateStateOnExit_klee(klee::ExecutionState& state) {
  klee::Executor::terminateStateOnExit(state);
}

void Executor::terminateStateOnExit(klee::ExecutionState& state) {
  struct TSoE : NetExTHnd {
    TSoE(Executor* e) : NetExTHnd(e) {
    }
    void term(klee::ExecutionState& state) const {
      DD::cout << "[TSoExit] term(" << &state << ") {" << DD::endl;
      getExecutor()->terminateStateOnExit_klee(state);
      DD::cout << "[TSoExit] } term(" << &state << ")" << DD::endl;
    }
  };
  TSoE hnd(this);
  DD::cout << "[NetExecutor::terminateStateOnExit] asking KleeNet to terminateCluster of " << (&state) << " " << DD::endl;
  kleenet.terminateCluster(state,hnd);
  DD::cout << "[NetExecutor::terminateStateOnExit] EOF" << DD::endl;
}


void Executor::terminateStateOnError_klee(klee::ExecutionState& state,
                                          llvm::Twine const& messaget,
                                          char const* suffix,
                                          llvm::Twine const& info) {
  klee::Executor::terminateStateOnError(state,messaget,suffix,info);
}

void Executor::terminateStateOnError(klee::ExecutionState& state,
                                     llvm::Twine const& messaget,
                                     char const* suffix,
                                     llvm::Twine const& info) {
  struct TSoE : NetExTHnd {
    llvm::Twine const& messaget;
    char const* suffix;
    llvm::Twine const& info;
    TSoE(Executor* e, llvm::Twine const& messaget, char const* suffix, llvm::Twine const& info)
      : NetExTHnd(e), messaget(messaget), suffix(suffix), info(info) {}
    void term(klee::ExecutionState& state) const {
      DD::cout << "[TSoError] term(" << &state << ") {" << DD::endl;
      getExecutor()->terminateStateOnError_klee(state,messaget,suffix,info);
      DD::cout << "[TSoError] } term(" << &state << ")" << DD::endl;
    }
  };
  TSoE hnd(this,messaget,suffix,info);
  DD::cout << "[NetExecutor::terminateStateOnError] asking KleeNet to terminateCluster of " << (&state) << " " << DD::endl;
  kleenet.terminateCluster(state,hnd);
  DD::cout << "[NetExecutor::terminateStateOnError] EOF" << DD::endl;
}

void Executor::terminateState_klee(klee::ExecutionState& state) {
  klee::Executor::terminateState(state);
}

void Executor::terminateState(klee::ExecutionState& state) {
  struct TS : NetExTHnd {
    TS(Executor* e)
      : NetExTHnd(e) {}
    bool silent() const {
      return true;
    }
    void term(klee::ExecutionState& state) const {
      DD::cout << "[TS] term(" << &state << ") {" << DD::endl;
      getExecutor()->terminateState_klee(state);
      DD::cout << "[TS] } term(" << &state << ")" << DD::endl;
    }
  };
  DD::cout << "[NetExecutor::terminateState] asking KleeNet to terminateCluster of " << (&state) << " " << DD::endl;
  kleenet.terminateCluster(state,TS(this));
  DD::cout << "[NetExecutor::terminateState] EOF" << DD::endl;
}
