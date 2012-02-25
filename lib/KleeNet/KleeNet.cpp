#include "kleenet/KleeNet.h"

#include "kleenet/State.h"
#include "kleenet/Searcher.h"

#include "klee/ExecutionState.h"

#include "net/StateMapper.h"
#include "net/PacketCache.h"
#include "net/Searcher.h"

#include "TransmitHandler.h"
#include "PacketInfo.h"

#include "llvm/Support/CommandLine.h"

#include <vector>
#include <algorithm>

namespace {
  llvm::cl::opt<net::StateMappingType>
  StateMapping("state-mapping",
      llvm::cl::desc("Choose state mapping algorithm (super by default). This is a KleeNet extension."),
      llvm::cl::values(
          clEnumValN(net::SM_COPY_ON_BRANCH,
                     "cob",   "Copy-On-Branch"),
          clEnumValN(net::SM_COPY_ON_WRITE,
                     "cow",   "Copy-On-Write"),
          clEnumValN(net::SM_COPY_ON_WRITE2,
                     "cow2",  "Copy-On-Write 2"),
          clEnumValN(net::SM_SUPER_DSTATE,
                     "super", "Super-DState-Mapping (no clustering: fast but unsearchable)"),
          clEnumValN(net::SM_SUPER_DSTATE_WITH_BF_CLUS,
                     "super-bfc", "Super-DState-Mapping with brute-force clustering (experimental, slow but searchable)"),
          clEnumValN(net::SM_SUPER_DSTATE_WITH_SMART_CLUS,
                     "super-sc", "Super-DState-Mapping with smart clustering (exprimental, faster than bf and searchable)"),
          clEnumValEnd),
          llvm::cl::init(net::SM_SUPER_DSTATE));

  llvm::cl::opt<bool>
  UsePhonyPackets("phony-packets",
      llvm::cl::desc("Enable phony packet pruning (experimental!). This is a KleeNet extension."));
}


using namespace kleenet;

KleeNet::KleeNet(Executor* executor)
  : phonyPackets(UsePhonyPackets)
  , env(NULL)
  , executor(executor) {
  Searcher::globalKleenet = this;
}

KleeNet::PacketCache* KleeNet::getPacketCache() const {
  if (env) {
    return env->packetCache.get();
  }
  return NULL;
}
net::StateMapper* KleeNet::getStateMapper() const {
  if (env) {
    return env->stateMapper.get();
  }
  return NULL;
}

net::Node KleeNet::getStateNode(klee::ExecutionState const* state) const {
  if (state && env && env->stateMapper.get())
    return env->stateMapper->getStateNode(state);
  return net::Node(); // invalid node
}
net::Node KleeNet::getStateNode(klee::ExecutionState const& state) const {
  return getStateNode(&state);
}
void KleeNet::setStateNode(klee::ExecutionState const* state, net::Node const& n) const {
  if (state && env && env->stateMapper.get())
    env->stateMapper->setStateNode(state,n);
}
void KleeNet::setStateNode(klee::ExecutionState const& state, net::Node const& n) const {
  setStateNode(&state,n);
}

KleeNet::~KleeNet() {
  Searcher::globalKleenet = NULL;
}

void KleeNet::newSearcher(Searcher* s) {
  phonyPackets = phonyPackets && s->ns->supportsPhonyPackets();
}

KleeNet::RunEnv::RunEnv(KleeNet& kleenet, klee::ExecutionState* rootState)
  : kleenet(kleenet)
  , stateMapper(net::StateMapper::create(StateMapping,UsePhonyPackets,rootState))
  , transmitHandler(new TransmitHandler())
  , packetCache(new KleeNet::PacketCache(*stateMapper,*transmitHandler)) {
  kleenet.env = this;
  rootState->executor = kleenet.executor;
}

KleeNet::RunEnv::~RunEnv() {
  kleenet.env = NULL;
}

KleeNet::TerminateStateHandler::~TerminateStateHandler() {
}


void KleeNet::memTxRequest(klee::ExecutionState& state, PacketInfo const& pi, net::ExData const& exData) const {
  assert(env && "Network environment not running!");
  env->packetCache->cacheMapping(&state,pi,exData);
  if (!phonyPackets) {
    // searcher is useless, it wont commit the cache, so do it right now!
    env->packetCache->commitMappings();
  }
}

namespace net {
  namespace basic_terminate {
    struct cast {
      klee::ExecutionState* operator()(net::BasicState* state) const {
        return static_cast<klee::ExecutionState*>(static_cast<State*>(state));
      }
    };
  }
}

void KleeNet::terminateCluster(klee::ExecutionState& state, KleeNet::TerminateStateHandler const& terminate) {
  assert(env && "Network environment not running!");
  struct BasicTerminate : net::StateMapper::TerminateStateHandler {
    private:
      KleeNet::TerminateStateHandler const& terminate;
      mutable std::vector<klee::ExecutionState*> cache; // optimisation
    public:
      BasicTerminate(KleeNet::TerminateStateHandler const& terminate) : terminate(terminate) {}
      void operator()(net::BasicState& state, std::vector<net::BasicState*> const& appendix) const {
        cache.resize(appendix.size());
        std::transform(appendix.begin(),appendix.end(),cache.begin(),net::basic_terminate::cast());
        terminate(*(net::basic_terminate::cast()(&state)),cache);
      }
  };
  env->stateMapper->terminateCluster(state,BasicTerminate(terminate));
}
