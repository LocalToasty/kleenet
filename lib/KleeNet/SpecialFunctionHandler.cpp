#include "SpecialFunctionHandler.h"

#include "NetExecutor.h"
#include "PacketInfo.h"
#include "kleenet/Searcher.h"
#include "AtomImpl.h"
#include "ExprBuilder.h"
#include "kexPPrinter.h"
#include "ConstraintSet.h"

#include "net/Time.h"
#include "net/util/SharedPtr.h"
#include "net/StateMapper.h"
#include "net/DataAtom.h"
#include "net/EventSearcher.h"

#include "klee/util/Ref.h"
#include "klee/util/ExprPPrinter.h"

#include "klee_headers/Context.h"
#include "klee_headers/Memory.h"
#include "klee_headers/Common.h"

#include "llvm/Module.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"

#include <set>
#include <vector>
#include <cassert>
#include <sstream>
#include <tr1/unordered_map>

#include "net/util/debug.h"

namespace {
  llvm::cl::opt<bool>
  DumpKleenetSfhCalls("sde-dump-kleenet-sfh-calls",
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
        std::ostringstream outbuf1;
        std::ostringstream outbuf2;
        if (DumpKleenetSfhCalls) {
          outbuf1 << "SFH[" << &ha.state << " @" << this->executor->kleeNet.getStateNode(ha.state).id << "]";
          outbuf2 << binding << "(";
          std::string del = "";
          for (ConstArgs::const_iterator it = constArgs.begin(), en = constArgs.end(); it != en; ++it) {
            outbuf2 << del << (*it)->getZExtValue();
            del = ", ";
          }
          outbuf2 << ")";
          klee::klee_message("%s calling %s ...",outbuf1.str().c_str(),outbuf2.str().c_str());
        }
        Returns ret;
        this->executor->bindLocal(target, ha.state,
          klee::ConstantExpr::create(
            ret = this->hnd(ha,constArgs),
            klee::Context::get().getPointerWidth()));
        if (DumpKleenetSfhCalls) {
          outbuf2 << " -> " << ret;
          klee::klee_message("%s returns %s",outbuf1.str().c_str(),outbuf2.str().c_str());
        }
      }
    };
    template <typename Self, unsigned args, char const* binding, bool doesNotReturn, bool doNotOverride>
    struct SFH<Self,void,args,binding,doesNotReturn,doNotOverride> : SFHAuto<Self,void,args,binding,doesNotReturn,doNotOverride> {
      void callHnd(klee::KInstruction* target,
                   HandleArgs const ha,
                   ConstArgs const& constArgs) {
        if (DumpKleenetSfhCalls) {
          std::ostringstream outbuf;
          outbuf << "SFH[" << &ha.state << " @" << this->executor->kleeNet.getStateNode(ha.state).id << "]" << " calling " << binding << "(";
          std::string del = "";
          for (ConstArgs::const_iterator it = constArgs.begin(), en = constArgs.end(); it != en; ++it) {
            outbuf << del << (*it)->getZExtValue();
            del = ", ";
          }
          outbuf << ")";
          klee::klee_message("%s",outbuf.str().c_str());
        }
        this->hnd(ha,constArgs);
      }
    };
  }

  void addDynamicSpecialFunctionHandlers(std::set<std::string>& set) { // used DIRECTLY by by main.cpp!
    typedef sfh::SFHBase::ListEntry::GlobalList SourceSet;
    for (SourceSet::const_iterator it = sfh::SFHBase::ListEntry::globalList()->begin(), end = sfh::SFHBase::ListEntry::globalList()->end(); it != end; ++it) {
      set.insert((*it)->name);
    }
  }

  template <typename Self, typename Returns, unsigned args, char const* binding, bool doesNotReturn = false, bool doNotOverride = false>
  struct SFH : sfh::SFH<Self,Returns,args,binding,doesNotReturn,doNotOverride> {
    typedef sfh::HandleArgs HandleArgs;
  };

  class SfhNodeContainer { // Not yet used
    public:
      typedef signed PublicId; // non-continuous
      typedef net::NodeId InternalId; // continuous, starting at net::Node::FIRST_NODE.id
      PublicId const invalidPublicId;
    private:
      std::tr1::unordered_map<PublicId, InternalId> publicToInternal;
      std::vector<PublicId> internalToPublic;
    public:
      InternalId lookupInternal(PublicId publicId) {
        if (publicId == invalidPublicId)
          klee::klee_warning("%s %d %s","Using the invlid node id ",invalidPublicId," in as a valid node id. The results may be unexpected.");
        std::tr1::unordered_map<PublicId, InternalId>::const_iterator internalId = publicToInternal.find(publicId);
        if (internalId == publicToInternal.end()) {
          publicToInternal[publicId] = internalToPublic.size();
          internalToPublic.push_back(publicId);
        }
        return publicToInternal[publicId];
      }
      PublicId lookupPublic(InternalId internalId) {
        if (internalId < 0 || static_cast<std::vector<PublicId>::size_type>(internalId) >= internalToPublic.size())
          return invalidPublicId; // special meaning. It's your own fault if you use it as valid id.
        else
          return internalToPublic[internalId];
      }
      explicit SfhNodeContainer(PublicId const invalidPublicId)
        : invalidPublicId(invalidPublicId)
        , publicToInternal()
        , internalToPublic()
      {}
  };

  SpecialFunctionHandler::SpecialFunctionHandler(Executor& netEx)
    : klee::SpecialFunctionHandler(netEx) // implicit downcast
    , netEx(netEx)
    , nodes(*(new SfhNodeContainer(-1))) {
    typedef sfh::SFHBase::ListEntry::GlobalList GL;
    net::util::SharedPtr<GL> gl = sfh::SFHBase::ListEntry::globalList();
    for (GL::iterator it = gl->begin(), en = gl->end(); it != en; ++it) {
      (*it)->sfhBase->main = this;
      (*it)->sfhBase->executor = &netEx;
      learnHandlerInfo(**it);
    }
  }

  SpecialFunctionHandler::~SpecialFunctionHandler() {
    delete &nodes;
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
          siblingEs->transferConstraints(*destEs);
          destEs->transferConstraints(*siblingEs);
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

  size_t SpecialFunctionHandler::acquireExprRange(ExDataCarrier* out1, std::vector<klee::ref<klee::Expr> >* out2, klee::ExecutionState& sourceState, klee::ref<klee::Expr> const dataSource, size_t len /*mutated!*/) const {
    // grab the source ObjetState and the offset
    klee::ResolutionList rl;
    sourceState.addressSpace.resolve(sourceState, netEx.solver, dataSource, rl);
    assert(rl.size() == 1 && "data range memcpy src must resolve to precisely one object");
    klee::MemoryObject const* const srcMo = rl[0].first;
    klee::ObjectState const* const srcOs = rl[0].second;
    unsigned const srcOffset =
      dyn_cast<ConstantExpr>(srcMo->getOffsetExpr(dataSource))->getZExtValue();
    if (!len)
      len = srcMo->size; //user didn't provide the length so they want everything

    assert(srcOffset + len <= srcMo->size);

    if (out1)
      out1->exData.reserve(out1->exData.size()+len);
    if (out2)
      out2->reserve(out2->size()+len);
    for (size_t i = 0; i < len; i++) {
      klee::ref<Expr> const re = srcOs->read8(srcOffset + i);
      typedef net::util::SharedPtr<net::DataAtom> DA;
      if (out1) {
        if (isa<ConstantExpr>(re)) {
          out1->exData.push_back(DA(new ConcreteAtom(dyn_cast<ConstantExpr>(re)->getZExtValue())));
        } else {
          out1->exData.push_back(DA(new SymbolicAtom(re)));
        }
      }
      if (out2)
        out2->push_back(re);
    }

    return len;
  }

  std::pair<klee::MemoryObject const*,size_t> SpecialFunctionHandler::findDestMo(klee::ExecutionState& state, klee::ref<klee::Expr> const& dest) const {
    klee::ResolutionList rl;
    state.addressSpace.resolve(state, netEx.solver, dest, rl);
    assert(rl.size() == 1 && "KleeNet memory operations expressions must resolve to precisely one object");

    return std::make_pair(rl[0].first,dyn_cast<ConstantExpr>(rl[0].first->getOffsetExpr(dest))->getZExtValue());
  }

  void SpecialFunctionHandler::memoryTransferWrapper(klee::ExecutionState& state,
                                                     klee::ref<Expr> dest, size_t destLen,
                                                     ExDataCarrier const& src,
                                                     Node destNode) {
    std::pair<klee::MemoryObject const*,size_t> const destMo = findDestMo(state,dest);

    // prepare mapping
    kleenet::PacketInfo pi(dyn_cast<ConstantExpr>(dest)->getZExtValue(),
                           destMo.second,
                           destLen,
                           destMo.first,
                           netEx.kleeNet.getStateNode(state),
                           destNode);

    netEx.kleeNet.memTxRequest(state, pi, src.exData);
  }

  HAND(void,kleenet_memcpy,4) {
    Node const destNode = args[3]->getZExtValue();
    size_t const len = args[2]->getZExtValue();
    assert(len > 0 && "n must be > 0");

    ExDataCarrier values;
    main->memoryTransferWrapper(ha.state, ha.arguments[0], main->acquireExprRange(&values, 0, ha.state, ha.arguments[1], len), values, destNode);
  }


  HAND(void,kleenet_pull,2) { // TODO change to four argument version and alias as macro in interface/
    Node const srcNode = args[1]->getZExtValue();

    std::vector<ExprBuilder::RefExpr> conjunctions;

    klee::ConstraintManager cm;
    if (net::StateMapper* const sm = executor->kleeNet.getStateMapper()) {
      sm->findTargets(ha.state, srcNode);
      for (net::StateMapper::iterator it = sm->begin(), end = sm->end(); it != end; ++it) {
        klee::ExecutionState* const es = static_cast<State*>(*it)->executionState();
        std::vector<ExprBuilder::RefExpr> exprs;
        main->acquireExprRange(0, &exprs, *es, ha.arguments[0], 0/* figure the length out yourself, please*/);
        std::vector<ExprBuilder::RefExpr> const constr = ConstraintSet(TransmissionKind::pull,*es,exprs.begin(),exprs.end()).extractFor(ha.state).extractConstraints(ConstraintSetTransfer::FORCEALL);
        (*it)->incCompletedPullRequests();
        for (std::vector<ExprBuilder::RefExpr>::const_iterator it = constr.begin(); it != constr.end(); ++it)
          pprint(net::DEBUG<net::debug::external1>(),*it,". ");
        conjunctions.push_back(ExprBuilder::conjunction(constr.begin(),constr.end()));
      }
      sm->invalidate();
    }
    ExprBuilder::RefExpr disjunction = ExprBuilder::disjunction(conjunctions.begin(),conjunctions.end());

    net::DEBUG<net::debug::external1>::cout << "Pulled disjunction of conjunctions: " << net::DEBUG<net::debug::external1>::endl << "> ";
    pprint(net::DEBUG<net::debug::external1>(),disjunction,"> ");
  }

  HAND(void,kleenet_memset,4) {
    ExDataCarrier value;
    size_t const len = args[2]->getZExtValue();
    assert(len && "asked to kleenet_memset 0 bytes.");
    value.exData.push_back(net::util::SharedPtr<net::DataAtom>(new ConcreteAtom(args[1]->getZExtValue())));

    main->memoryTransferWrapper(ha.state, ha.arguments[0], len, value, args[3]->getZExtValue());
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
        llvm::Twine() + "Invalid node id (" + llvm::Twine(node.id) + ") passed to kleenet_set_node_id. This is used as magic number. Note: The default node id is (" + llvm::Twine(Node::FIRST_NODE.id) + ") but you may specify any id greater or equal " + llvm::Twine(Node::INVALID_NODE.id) + ".",
        "exec.err");
    } else if (node < net::Node::FIRST_NODE) {
      executor->terminateStateOnError(
        ha.state,
        llvm::Twine() + "Invalid node id (" + llvm::Twine(node.id) + ") passed to kleenet_set_node_id. Note: The default node id is (" + llvm::Twine(Node::FIRST_NODE.id) + ") but you may specify any id greater or equal " + llvm::Twine(Node::FIRST_NODE.id) + ".",
        "exec.err");
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

  HAND(void,kleenet_barrier,0) {
    executor->getNetSearcher()->netSearcher()->barrier(&(ha.state));
  }

  // strong debug stuff .......
  HAND(void,kleenet_dump_constraints,0) {
    klee::ExprPPrinter::printConstraints(std::cout,ha.state.constraints);
  }


}
