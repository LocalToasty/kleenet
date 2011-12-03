#include "kleenet/CustomSearcherFactory.h"

#include "llvm/Support/CommandLine.h"
#include "klee/Internal/ADT/RNG.h"

#include <memory>

#include "kleenet/Searcher.h"

#include "net/Time.h"

#include "net/LockStepSearcher.h"
#include "net/CoojaSearcher.h"
#include "net/ClusterSearcher.h"

#include "net/ClusterSearcherStrategies.h"
#include "net/util/SharedPtr.h"

namespace klee {
  extern RNG theRNG;
}

namespace kleenet {
  llvm::cl::opt<bool>
  UseLockStepSearch("use-lockstep-search",
                    llvm::cl::desc("Execute all states in a lock-step fashion (default)"));

  llvm::cl::opt<net::Time>
  LockStepIncrement("lockstep-increment",
                    llvm::cl::desc("Virtual time advance for lockstep searchers (default 1)"),
                    llvm::cl::init(1));

  llvm::cl::opt<bool>
  UseCoojaSearch("use-cooja-search",
                 llvm::cl::desc("Execute all states by a simulated Cooja Searcher (default)"));

  llvm::cl::opt<bool>
  UseLockStepClusterSearch("use-lockstep-cluster-search",
                           llvm::cl::desc("LockStepSearcher with support for clustering"));

  llvm::cl::opt<bool>
  UseCoojaClusterSearch("use-cooja-cluster-search",
                        llvm::cl::desc("CoojaSearcher with support for clustering"));

  llvm::cl::opt<unsigned>
  ClusterInstructions("cluster-instructions",
                    llvm::cl::desc("Number of instructions for which each cluster is executed repeatedly"),
                    llvm::cl::init(10000));

  llvm::cl::opt<bool>
  UseFifoStrategy("fifo-strategy",
              llvm::cl::desc("Use fifo strategy to choose the next cluster"),
              llvm::cl::init(false));

  llvm::cl::opt<bool>
  UseRandomStrategy("random-strategy",
              llvm::cl::desc("Use random strategy to choose the next cluster"),
              llvm::cl::init(false));

  struct RandomStrategy : net::RandomStrategy {
    unsigned prng(unsigned size) const {
      if (size)
        // XXX yack!
        return klee::theRNG.getInt32() % size;
      return 0;
    }
  };

  namespace searcherautorun {
    struct AF {
      virtual ~AF() {}
    };
    typedef llvm::cl::opt<bool> O;
    template <typename S> struct KleeNetSearcherAF : AF, CustomSearcherAutoFactory<S,O> {
      KleeNetSearcherAF(O& o) : CustomSearcherAutoFactory<S,O>(o,kleenet::CustomSearcherFactory::CSFP_OVERRIDE_LEGACY) {}
    };
    template <> struct KleeNetSearcherAF<net::LockStepSearcher> : AF, CustomSearcherAutoFactory<net::LockStepSearcher,O> {
      KleeNetSearcherAF(O& o) : CustomSearcherAutoFactory<net::LockStepSearcher,O>(o,kleenet::CustomSearcherFactory::CSFP_OVERRIDE_LEGACY) {}
      net::Searcher* newSearcher(net::PacketCacheBase* pcb) {
        return new net::LockStepSearcher(pcb,LockStepIncrement);
      }
    };
    template <> struct KleeNetSearcherAF<net::GenericClusterSearcher<net::LockStepSearcher> > : AF, CustomSearcherAutoFactory<net::GenericClusterSearcher<net::LockStepSearcher>,llvm::cl::opt<bool>,false> {
      struct Alloc {
        net::Searcher* operator()(net::PacketCacheBase* pcb) {
          return new net::LockStepSearcher(pcb,LockStepIncrement);
        }
      };
      net::util::SharedPtr<net::SearcherStrategy> strat;
      KleeNetSearcherAF(O& o,net::util::SharedPtr<net::SearcherStrategy> strat) : CustomSearcherAutoFactory<net::GenericClusterSearcher<net::LockStepSearcher>,llvm::cl::opt<bool>,false>(o,kleenet::CustomSearcherFactory::CSFP_OVERRIDE_LEGACY), strat(strat) {}
      net::Searcher* newSearcher(net::PacketCacheBase* pcb) {
        return new net::GenericClusterSearcher<net::LockStepSearcher,Alloc>(strat,pcb);
      }
    };
    template <> struct KleeNetSearcherAF<net::GenericClusterSearcher<net::CoojaSearcher> > : AF, CustomSearcherAutoFactory<net::GenericClusterSearcher<net::CoojaSearcher>,llvm::cl::opt<bool>,false> {
      struct Alloc {
        net::Searcher* operator()(net::PacketCacheBase* pcb) {
          return new net::CoojaSearcher(pcb);
        }
      };
      net::util::SharedPtr<net::SearcherStrategy> strat;
      KleeNetSearcherAF(O& o,net::util::SharedPtr<net::SearcherStrategy> strat) : CustomSearcherAutoFactory<net::GenericClusterSearcher<net::CoojaSearcher>,llvm::cl::opt<bool>,false>(o,kleenet::CustomSearcherFactory::CSFP_OVERRIDE_LEGACY), strat(strat) {}
      net::Searcher* newSearcher(net::PacketCacheBase* pcb) {
        return new net::GenericClusterSearcher<net::CoojaSearcher,Alloc>(strat,pcb);
      }
    };
    class SearcherAutoRun {
      static SearcherAutoRun self;
      typedef std::auto_ptr<AF> F;
      net::util::SharedPtr<net::SearcherStrategy> baseStrategy;
      net::util::SharedPtr<net::SearcherStrategy> strategyAdapter;
      F lockStep, cooja, clusterLockStep, clusterCooja;
      SearcherAutoRun()
        : baseStrategy(UseFifoStrategy?static_cast<net::SearcherStrategy*>(new net::FifoStrategy()):(UseRandomStrategy?static_cast<net::SearcherStrategy*>(new RandomStrategy()):static_cast<net::SearcherStrategy*>(new net::NullStrategy())))
        , strategyAdapter((ClusterInstructions <= 1)?baseStrategy:net::util::SharedPtr<net::SearcherStrategy>(new net::RepeatStrategy(baseStrategy,ClusterInstructions)))
        , lockStep(new KleeNetSearcherAF<net::LockStepSearcher>(UseLockStepSearch))
        , cooja(new KleeNetSearcherAF<net::CoojaSearcher>(UseCoojaSearch))
        , clusterLockStep(new KleeNetSearcherAF<net::GenericClusterSearcher<net::LockStepSearcher> >(UseLockStepClusterSearch,strategyAdapter))
        , clusterCooja(new KleeNetSearcherAF<net::GenericClusterSearcher<net::CoojaSearcher> >(UseCoojaSearch,strategyAdapter))
        {
        (void)self;
      }
    };
  }
}
