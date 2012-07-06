#include "NetExecutor.h"

#include "llvm/Support/CommandLine.h"

#include "SpecialFunctionHandler.h" // the KleeNet version! It enhances the KLEE version
#include "ConfigurationData.h"
#include "kleenet/Searcher.h"
#include "kleenet/CustomSearcherFactory.h"

#include "net/PacketCache.h"

#include "NetUserSearcher.h"
#include "OverrideOpt.h"

#include <algorithm>
#include <iostream>

#include <net/util/debug.h>

typedef net::DEBUG<net::debug::term> DD;

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

namespace executor_options {
  extern llvm::cl::opt<bool> UseCache;
  extern llvm::cl::opt<bool> UseCexCache;
}


namespace {
  llvm::cl::opt<bool> Override_UseCache(
    "sde-override-cache",
    llvm::cl::desc("Override KLEE caching. If manually set to =0, use KLEE default setting. This can cause critical errors! You have been warned."),
    llvm::cl::init(true)
  );
  llvm::cl::opt<bool> Override_UseCexCache(
    "sde-override-cex-cache",
    llvm::cl::desc("Override KLEE cex caching. If manually set to =0, use KLEE default setting. This can cause critical errors! You have been warned."),
    llvm::cl::init(true)
  );
}

namespace kleenet {
  struct NetExTHnd : KleeNet::TerminateStateHandler {
    private:
      Executor* e;
    protected:
      NetExTHnd(Executor* e) : e(e) {
      }
      virtual void term(klee::ExecutionState& state) const {
        DD::cout << "[NetExTHnd] term(" << &state << ") {" << DD::endl;
        getExecutor()->terminateState_klee(state);
        DD::cout << "[NetExTHnd] } term(" << &state << ")" << DD::endl;
      }
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
        bool feasible = true;
        for (std::vector<klee::ExecutionState*>::const_iterator it(appendix.begin()), end(appendix.end()); feasible && it != end; ++it) {
          feasible = (*it)->transferConstraints(state);
        }
        for (std::vector<klee::ExecutionState*>::const_iterator it(appendix.begin()), end(appendix.end()); feasible && it != end; ++it) {
          feasible = state.transferConstraints(**it);
        }
        if (feasible && !appendix.empty())
          e->netInterpreterHandler->incDScenariosExplored();
        NetExTHnd silentHandler(e);
        NetExTHnd const& useHandler = feasible?*this:silentHandler;
        useHandler(state);
        bool generateTestCases = true;
        if (!silent()) {
          switch (distributedTerminateBehaviour) {
            case DTB_uniformTestCase:
              std::for_each<std::vector<klee::ExecutionState*>::const_iterator,NetExTHnd const&>(appendix.begin(),appendix.end(),useHandler);
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
  : klee::Executor(
        OverrideChain()
          .overrideOpt(executor_options::UseCache).withValue(false).onlyIf(Override_UseCache).chain()
          .overrideOpt(executor_options::UseCexCache).withValue(false).onlyIf(Override_UseCexCache).chain()
          .passSomeCRef(opts)
      , ih)
  , kleenet(this)
  , netInterpreterHandler(ih)
  , netSearcher(NULL)
  , kleeNet(kleenet) {
  kleenet::searcherautorun::SearcherAutoRun::linkme(); // stupid linker!
}

klee::TimingSolver* Executor::getTimingSolver() {
  return this->solver;
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
      if (state.configurationData && (state.configurationData->self().flags & StateFlags::ERROR)) {
        DD::cout << "[TSoError] term(" << &state << ") /* error condition */ {" << DD::endl;
        getExecutor()->terminateStateOnError_klee(state,messaget,suffix,info);
        DD::cout << "[TSoError] } term(" << &state << ")" << DD::endl;
      } else {
        DD::cout << "[TSoError] term(" << &state << ") /* other (early) */ {" << DD::endl;
        getExecutor()->terminateStateEarly_klee(state,llvm::Twine("DScenario termination because of local error: ") + messaget);
        DD::cout << "[TSoError] } term(" << &state << ")" << DD::endl;
      }
    }
  };
  ConfigurationData::configureState(state);
  state.configurationData->self().flags |= StateFlags::ERROR;
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
