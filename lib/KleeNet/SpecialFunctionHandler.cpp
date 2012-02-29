#include "SpecialFunctionHandler.h"

#include "NetExecutor.h"
#include "PacketInfo.h"
#include "kleenet/Searcher.h"
#include "AtomImpl.h"

#include "net/Time.h"
#include "net/util/SharedPtr.h"
#include "net/StateMapper.h"
#include "net/DataAtom.h"
#include "net/EventSearcher.h"

#include "klee/util/Ref.h"

#include "llvm/Module.h"
#include "llvm/ADT/Twine.h"

#include <set>
#include <assert.h>

// XXX ugly intrusive stuff ...
#include "../Core/Context.h"
#include "../Core/Memory.h"

#include <iostream>

#include "llvm/Support/CommandLine.h"

namespace {
  llvm::cl::opt<bool>
  DumpKleenetSfhCalls("dump-kleenet-sfh-calls",
      llvm::cl::desc("This is a debug feature for interface code. If you enable this, all invocations of special function handlers will be dumped on standard out, prefixed by 'SFH'."));
}

namespace kleenet {
  // some aliases
  using llvm::isa;
  using llvm::dyn_cast;
  using klee::Expr;
  using klee::ConstantExpr;
  using net::Node;

  /* How does this unit work?
   *   We implement special function handlers (sfhs) by implementing classes which
   *   have private static members of their own type. So by declaring a class, you instantiate it.
   *   This private static instance will insert itself in a static global list owned by SFHBase.
   *   During the construction of the SpecialFuntionHandler instance, which is NOT a static object
   *   (hence is constructed AFTER all static objects are created and after ::main was called)
   *   the CTOR peaks in the List of SFHBase and creates wrappers for all listed sfhs in its
   *   parent, which is klee::SpecialFunctionHandler, which in turn can accept arbitrary external sfhs.
   * Uhm, okay but how do I create a new sfh?
   *   Do not bother to create your SFH instance yourself, later in this file you will find a HAND
   *   helper-macro that does all the dirty work for you.
   */

  namespace sfh {
    struct SFHBase {
      public:
        struct ListEntry : public SpecialFunctionHandler::HandlerInfo {
          SFHBase* sfhBase;
          typedef std::set<ListEntry const*> GlobalList;
          static net::util::SharedPtr<GlobalList> globalList() {
            static net::util::SharedPtr<GlobalList> _globalList(new GlobalList());
            return _globalList;
          }
          ListEntry(SFHBase* sfhBase, SpecialFunctionHandler::HandlerInfo const& hi)
            : SpecialFunctionHandler::HandlerInfo(hi)
            , sfhBase(sfhBase) {
            globalList()->insert(this);
          }
          // not copy-CTOR'able, not assignable
          ~ListEntry() {
            globalList()->erase(this);
          }
        };
        Executor* executor;
        SpecialFunctionHandler* main;
    };
    struct HandleArgs {
      klee::ExecutionState& state;
      klee::KInstruction* target;
      std::vector<klee::ref<klee::Expr> >& arguments;
    };
    template <typename Return> struct HasReturnValue {
      static bool const is = true;
    };
    template <> struct HasReturnValue<void> {
      static bool const is = false;
    };

    typedef std::vector<ConstantExpr*> ConstArgs;

    template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn, bool doNotOverride>
    struct SFHAuto : SFHBase, SpecialFunctionHandler::ExternalHandler {
      static Self self;
      static SpecialFunctionHandler::HandlerInfo const hi;
      static ListEntry const listEntry;

      virtual Returns hnd(HandleArgs const, ConstArgs const&) = 0;
      virtual void callHnd(klee::KInstruction *target, HandleArgs const, ConstArgs const&) = 0;
      void operator()(klee::ExecutionState &state,
                      klee::KInstruction *target,
                      std::vector<klee::ref<klee::Expr> > &arguments) {
        (void)self;
        (void)listEntry;

        HandleArgs const ha = {state,target,arguments};
        ConstArgs constArgs;

        assert(executor);
        assert(main);
        if (args != (unsigned)-1) {
          assert((ha.arguments.size() == args) && "Wrong number of arguments to kleenet_ handler");
          unsigned k = 0;
          constArgs.reserve(ha.arguments.size());
          for (std::vector<klee::ref<Expr> >::const_iterator it = ha.arguments.begin(), en = ha.arguments.end(); it != en; ++it,++k) {
            ConstantExpr* const ce = dyn_cast<ConstantExpr>(*it);
            if (ce) {
              constArgs.push_back(ce);
            } else {
              executor->terminateStateOnError(
                ha.state,
                llvm::Twine() + "Argument " + llvm::Twine(k) + " of " + llvm::Twine(ha.arguments.size()) + " to special function `" + binding + "` does not evaluate to a constant expression.",
                "exec.err");
              return;
              // ignore invalid argument for now, NOTE: the state is dead already
              // no reason to stop the cosmos, just because one state shits itself
            }
          }
        }
        callHnd(target,ha,constArgs);
      }
    };
    template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn, bool doNotOverride>
    Self SFHAuto<Self,Returns,args,binding,doesNotReturn,doNotOverride>::self;
    template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn, bool doNotOverride>
    SpecialFunctionHandler::HandlerInfo const SFHAuto<Self,Returns,args,binding,doesNotReturn,doNotOverride>::hi = {
      binding, &SpecialFunctionHandler::handleExternalHandler, doesNotReturn, HasReturnValue<Returns>::is, doNotOverride, &self
    };
    template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn, bool doNotOverride>
    SFHBase::ListEntry const SFHAuto<Self,Returns,args,binding,doesNotReturn,doNotOverride>::listEntry(&self, hi);

    template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn = false, bool doNotOverride = false>
    struct SFH : SFHAuto<Self,Returns,args,binding,doesNotReturn,doNotOverride> {
      void callHnd(klee::KInstruction* target,
                   HandleArgs const ha,
                   ConstArgs const& constArgs) {
        if (DumpKleenetSfhCalls) {
          std::cout << "SFH[" << &ha.state << "]" << " calling " << binding << "(";
          std::string del = "";
          for (ConstArgs::const_iterator it = constArgs.begin(), en = constArgs.end(); it != en; ++it) {
            std::cout << del << (*it)->getZExtValue();
            del = ", ";
          }
        }
        Returns ret;
        this->executor->bindLocal(target, ha.state,
          klee::ConstantExpr::create(
            ret = this->hnd(ha,constArgs),
            klee::Context::get().getPointerWidth()));
        if (DumpKleenetSfhCalls) {
          std::cout << ")" << " -> " << ret << std::endl;
        }
      }
    };
    template <typename Self, unsigned args, char const* binding, bool doesNotReturn, bool doNotOverride>
    struct SFH<Self,void,args,binding,doesNotReturn,doNotOverride> : SFHAuto<Self,void,args,binding,doesNotReturn,doNotOverride> {
      void callHnd(klee::KInstruction* target,
                   HandleArgs const ha,
                   ConstArgs const& constArgs) {
        if (DumpKleenetSfhCalls) {
          std::cout << "SFH[" << &ha.state << "]" << " calling " << binding << "(";
          std::string del = "";
          for (ConstArgs::const_iterator it = constArgs.begin(), en = constArgs.end(); it != en; ++it) {
            std::cout << del << (*it)->getZExtValue();
            del = ", ";
          }
          std::cout << ")" << std::endl;
        }
        this->hnd(ha,constArgs);
      }
    };
  }

  template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn = false, bool doNotOverride = false>
  struct SFH : sfh::SFH<Self,Returns,args,binding,doesNotReturn,doNotOverride> {
    typedef sfh::HandleArgs HandleArgs;
  };

  SpecialFunctionHandler::SpecialFunctionHandler(Executor& netEx)
    : klee::SpecialFunctionHandler(netEx) // implicit downcast
    , netEx(netEx) {
    typedef sfh::SFHBase::ListEntry::GlobalList GL;
    net::util::SharedPtr<GL> gl = sfh::SFHBase::ListEntry::globalList();
    for (GL::iterator it = gl->begin(), en = gl->end(); it != en; ++it) {
      (*it)->sfhBase->main = this;
      (*it)->sfhBase->executor = &netEx;
      learnHandlerInfo(**it);
    }
  }


// I AM SOOOOOO LAZY, THAT IT HURTS ...
#define FULLHAND(RET,NAME,COUNT,DNR,DNO) \
  char hnd_name_##NAME[] = #NAME; \
  struct Handler__##NAME : SFH<Handler__##NAME,RET,COUNT,hnd_name_##NAME,DNR,DNO> { \
    RET hnd(HandleArgs const ha,sfh::ConstArgs const& ca); \
  }; \
  RET Handler__##NAME::hnd(HandleArgs const ha,sfh::ConstArgs const& args)

#define HAND(RET,NAME,COUNT) FULLHAND(RET,NAME,COUNT,false,false)

  HAND(void,kleenet_early_exit,1) {
    executor->terminateStateEarly(ha.state, main->readStringAtAddress(ha.state, ha.arguments[0]));
  }

  HAND(void,kleenet_get_global_symbol,4) {
    Node const dest = args[3]->getZExtValue();
    assert(dest != executor->kleeNet.getStateNode(ha.state) && "destination node == calling node");

    size_t const len = args[2]->getZExtValue();
    assert(len > 0 && "n must be > 0");

    // grab the source os
    klee::ResolutionList rl;
    ha.state.addressSpace.resolve(ha.state, executor->solver, ha.arguments[0], rl);
    assert(rl.size() == 1 && "kleenet_get_global_symbol: dest must resolve to precisely one object");
    klee::MemoryObject const* destMo = rl[0].first;
    assert(!dyn_cast<ConstantExpr>(destMo->getOffsetExpr(ha.arguments[0]))->getZExtValue() && "mo offset must be 0");
    rl.clear();

    std::string const symbol = main->readStringAtAddress(ha.state, ha.arguments[1]);

    net::StateMapper* const sm = executor->kleeNet.getStateMapper();

    // traverse global symbols
    for (std::map<const llvm::GlobalValue*, klee::MemoryObject*>::iterator
           it = executor->globalObjects.begin(),
           ite = executor->globalObjects.end(); it != ite; ++it) {
      // symbol found
      if (it->first->getName().str() == symbol) {
        assert(len == it->second->size && "size mismatch");

        std::set<Node> tmpSet;
        tmpSet.insert(dest);
        std::vector<net::BasicState*> siblings;
        /* XXX
           Why are we exploding in a logically non-mutating get request?
           Me thinks we have to perform some sciencing here ...
         */
        sm->explode(&ha.state, sm->nodes(), tmpSet, &siblings);
        siblings.push_back(&ha.state);
        for (std::vector<net::BasicState*>::iterator sit = siblings.begin(),
            sie = siblings.end(); sit != sie; ++sit) {
          klee::ExecutionState* siblingEs = static_cast<klee::ExecutionState*>(*sit);
          sm->findTargets(*siblingEs, dest);
          net::StateMapper::iterator smit = sm->begin();
          assert(smit != sm->end() && "No states available");
          klee::ExecutionState* destEs = static_cast<klee::ExecutionState*>(*smit);
          // src data
          klee::ObjectState const* srcOs = destEs->addressSpace.findObject(it->second);
          // dest area
          klee::ObjectState const* destOs = siblingEs->addressSpace.findObject(destMo);
          klee::ObjectState* wos = siblingEs->addressSpace.getWriteable(destMo, destOs);
          // memcpy
          for (unsigned i = 0; i < len; i++) {
            wos->write(i, srcOs->read8(i));
          }
          // merge with destination ha.state's constraints
          siblingEs->mergeConstraints(*destEs);
          assert(++smit == sm->end() && "More than one ha.state on dest node");
          // invalidate mapping
          sm->invalidate();
        }
        return;
      }
    }
    // XXX no symbol found, aborting
    assert(0 && "symbol not found");
  }


  HAND(net::NodeId,kleenet_get_node_id,0) {
    /****** just for comparison: OLD CODE:
       ##  void SpecialFunctionHandler::handleGetNodeId(ExecutionState &state,
       ##                                               KInstruction *target,
       ##                                               std::vector<ref<Expr> > &arguments) {
       ##    assert(arguments.size()==0 && "invalid number of arguments to kleenet_get_node_id");
       ##    Expr::Width WordSize = Context::get().getPointerWidth();
       ##    if (WordSize == Expr::Int32) {
       ##      executor.bindLocal(target, state,
       ##                         ConstantExpr::create(state.mappingInformation->node.id,
       ##                                              Expr::Int32));
       ##    } else if (WordSize == Expr::Int64) {
       ##      executor.bindLocal(target, state,
       ##                         ConstantExpr::create(state.mappingInformation->node.id,
       ##                                              Expr::Int64));
       ##    } else {
       ##      assert(0 && "Unknown word size!");
       ##    }
       ##  }
    ***** and NEW CODE: */
    return executor->kleeNet.getStateNode(ha.state).id;
  }

  HAND(net::Time,kleenet_get_virtual_time,0) {
    return executor->getNetSearcher()->getStateTime(ha.state);
  }

  // unfortunately, aliasing here is not enough :(
  // ... but I forgot why (thanks very much past me)
  struct ExDataCarrier {
    net::ExData exData;
  };

  void SpecialFunctionHandler::memoryTransferWrapper(klee::ExecutionState& state,
                                                     klee::ref<Expr> dest, size_t destLen,
                                                     ExDataCarrier const& src,
                                                     Node destNode) {
    // grab the dest's ObjetState and the offset
    klee::ResolutionList rl;
    state.addressSpace.resolve(state, netEx.solver, dest, rl);
    assert(rl.size() == 1 && "kleenet_memcpy/memset dest must resolve to precisely one object");

    klee::MemoryObject const* const destMo = rl[0].first;
    unsigned const destOffset = dyn_cast<ConstantExpr>(destMo->getOffsetExpr(dest))->getZExtValue();

    // prepare mapping
    //PacketInfo(uint64_t addr, uint64_t offset, size_t length, klee::MemoryObject const* destMo, net::Node src, net::Node dest);
    kleenet::PacketInfo pi(dyn_cast<ConstantExpr>(dest)->getZExtValue(),
                           destOffset,
                           destLen,
                           destMo,
                           netEx.kleeNet.getStateNode(state),
                           destNode);

    netEx.kleeNet.memTxRequest(state, pi, src.exData);
  }

  HAND(void,kleenet_memcpy,4) {
    size_t const len = args[2]->getZExtValue();
    Node const destNode = args[3]->getZExtValue();
    assert(len > 0 && "n must be > 0");

    // grab the source ObjetState and the offset
    klee::ResolutionList rl;
    ha.state.addressSpace.resolve(ha.state, executor->solver, ha.arguments[1], rl);
    assert(rl.size() == 1 && "kleenet_memcpy src must resolve to precisely one object");
    klee::MemoryObject const* const srcMo = rl[0].first;
    klee::ObjectState const* const srcOs = rl[0].second;
    unsigned const srcOffset =
      dyn_cast<ConstantExpr>(srcMo->getOffsetExpr(ha.arguments[1]))->getZExtValue();

    ExDataCarrier values;
    for (size_t i = 0; i < len; i++) {
      klee::ref<Expr> const re = srcOs->read8(srcOffset + i);
      typedef net::util::SharedPtr<net::DataAtom> DA;
      if (isa<ConstantExpr>(re)) {
        values.exData.push_back(DA(new ConcreteAtom(dyn_cast<ConstantExpr>(re)->getZExtValue())));
      } else {
        values.exData.push_back(DA(new SymbolicAtom(re)));
      }
    }

    main->memoryTransferWrapper(ha.state, ha.arguments[0], len, values, destNode);
  }

  HAND(void,kleenet_memset,4) {
    ExDataCarrier value;
    value.exData.push_back(net::util::SharedPtr<net::DataAtom>(new ConcreteAtom(args[1]->getZExtValue())));

    main->memoryTransferWrapper(ha.state, ha.arguments[0], args[2]->getZExtValue(), value, args[3]->getZExtValue());
  }

  HAND(void,kleenet_schedule_boot_state,1) {
    net::EventSearcher* const ev = executor->getNetSearcher()->netSearcher()->toEventSearcher();
    if (ev) { // hey, we do have an event-capable searcher :)
      ev->scheduleStateIn(&(ha.state),args[0]->getZExtValue(),net::EventSearcher::EK_Boot);
    }
  }
  HAND(void,kleenet_schedule_state,1) {
    net::EventSearcher* const ev = executor->getNetSearcher()->netSearcher()->toEventSearcher();
    if (ev) { // hey, we do have an event-capable searcher :)
      ev->scheduleStateIn(&(ha.state),args[0]->getZExtValue(),net::EventSearcher::EK_Normal); // EK_Normal is normally implied but wayne
    }
  }

  HAND(void,kleenet_set_node_id,1) {
    net::Node const node = args[0]->getZExtValue();
    if (node == net::Node::INVALID_NODE) {
      executor->terminateStateOnError(
        ha.state,
        llvm::Twine() + "Invalid node id (" + llvm::Twine(node.id) + ") passed to kleenet_set_node_id. This is used as magic number. Note: The default node id is (" + llvm::Twine(Node::FIRST_NODE.id) + ") but you may specify any id that does not equal " + llvm::Twine(Node::INVALID_NODE.id) + ".",
        "exec.err");
      //std::cerr << "invalid node id, please use an id != " << INVALID_NODE_ID << ".\n";
      //assert(0 && "invalid node id");
    } else {
      return executor->kleeNet.setStateNode(ha.state,node);
    }
  }

  HAND(void,kleenet_yield_state,0) {
    net::EventSearcher* const ev = executor->getNetSearcher()->netSearcher()->toEventSearcher();
    if (ev) { // hey, we do have an event-capable searcher :)
      ev->yieldState(&(ha.state));
    }
  }

  HAND(void,kleenet_wakeup_dest_states,1) {
    net::EventSearcher* const ev = executor->getNetSearcher()->netSearcher()->toEventSearcher();
    if (ev) { // hey, we do have an event-capable searcher :)
      Node const dest = args[0]->getZExtValue();

      net::StateMapper* const sm = executor->kleeNet.getStateMapper();
      // call mapping
      sm->map(ha.state, dest);
      sm->findTargets(ha.state, dest);
      for (net::StateMapper::iterator it = sm->begin(), end = sm->end(); it != end; ++it) {
        net::BasicState *bs = *it;
        // schedule immediate wakeup
        //assert(es->schedulingInformation.isScheduled);
        ev->scheduleStateAt(bs, ev->getStateTime(&ha.state));
      }
      // invalidate found states
      sm->invalidate();
    }
  }

  HAND(uintptr_t,kleenet_get_state,0) {
    return reinterpret_cast<uintptr_t>(&(ha.state));
  }

}
