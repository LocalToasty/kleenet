#include "SpecialFunctionHandler.h"

#include "NetExecutor.h"
#include "PacketInfo.h"
#include "kleenet/Searcher.h"
#include "AtomImpl.h"
#include "ExprBuilder.h"
#include "kexPPrinter.h"
#include "ConstraintSet.h"
#include "ConfigurationData.h"

#include "net/Node.h"
#include "net/Time.h"
#include "net/StateMapper.h"
#include "net/DataAtom.h"
#include "net/PacketCache.h"
#include "net/EventSearcher.h"

#include "net/util/SharedPtr.h"
#include "net/util/Functor.h"

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
#include <utility>
#include <cassert>
#include <sstream>
#include <tr1/unordered_map>

#include "net/util/debug.h"

typedef net::DEBUG<net::debug::external1> DD;

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
   *   During the construction of the SpecialFunctionHandler instance, which is NOT a static object
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
        SpecialFunctionHandler_impl* main;
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

  struct SpecialFunctionHandler_impl {
    Executor& netEx;
    SfhNodeContainer nodes;
    SpecialFunctionHandler& parent;
    typedef std::map<std::string,klee::MemoryObject*> KnownGlobals; // FIXME replace this with an efficient Trie
    KnownGlobals knownGlobals;

    SpecialFunctionHandler_impl(SpecialFunctionHandler* parent, Executor& netEx)
      : netEx(netEx)
      , nodes(-1)
      , parent(*parent)
      {
    }

    klee::MemoryObject* operator[](std::string const& key) {
      if (knownGlobals.size() != netEx.globalObjects.size()) // updating a cache is hard, but hey, so is not using it at all
        for (std::map<llvm::GlobalValue const*, klee::MemoryObject*>::iterator
               it = netEx.globalObjects.begin();
               it != netEx.globalObjects.end(); ++it)
          knownGlobals[it->first->getName().str()] = it->second;
      KnownGlobals::iterator location = knownGlobals.find(key);
      if (location == knownGlobals.end())
        return NULL;
      return location->second;
    }

    // returns the length (equalling `len` iff `len` != 0)
    size_t acquireExprRange(net::ExData* out1, std::vector<klee::ref<klee::Expr> >* out2, klee::ExecutionState& sourceState, klee::ref<klee::Expr> dataSource, size_t len /*Maybe 0*/) const {
      klee::ResolutionList rl;
      sourceState.addressSpace.resolve(sourceState, netEx.solver, dataSource, rl);
      assert(rl.size() == 1 && "data range memcpy src must resolve to precisely one object");
      klee::MemoryObject const* const srcMo = rl[0].first;
      klee::ObjectState const* const srcOs = rl[0].second;
      unsigned const srcOffset =
        dyn_cast<ConstantExpr>(srcMo->getOffsetExpr(dataSource))->getZExtValue();

      return acquireExprRange(out1,out2,sourceState,srcOs,srcOffset,len);
    }

    size_t acquireExprRange(net::ExData* out1, std::vector<klee::ref<klee::Expr> >* out2, klee::ExecutionState& sourceState, klee::MemoryObject const* const srcMo, /* implies an offset of 0! */ size_t len /*Maybe 0*/) const {
      klee::ObjectState const* const srcOs = sourceState.addressSpace.findObject(srcMo);

      return acquireExprRange(out1,out2,sourceState,srcOs,0/*!*/,len);
    }

    // You probably don't want to use this overload directly, it's called from the other ones.
    // Note that it has one extra paramter, as data cannot be located only with an ObjectState -- we need an additional offset.
    size_t acquireExprRange(net::ExData* const out1, std::vector<klee::ref<klee::Expr> >* const out2, klee::ExecutionState& sourceState, klee::ObjectState const* const srcOs, size_t const srcOffset, size_t len /*Maybe 0*/) const {
      if (!len)
        len = srcOs->size; //user didn't provide the length so they want everything

      assert(srcOffset + len <= srcOs->size);

      if (out1)
        out1->reserve(out1->size()+len);
      if (out2)
        out2->reserve(out2->size()+len);
      for (size_t i = 0; i < len; i++) {
        klee::ref<Expr> const re = srcOs->read8(srcOffset + i);
        typedef net::util::SharedPtr<net::DataAtom> DA;
        if (out1) {
          if (isa<ConstantExpr>(re)) // this distinction is made only for the PacketCache (i.e. phony-packet semantics)
            out1->push_back(DA(new ConcreteAtom(dyn_cast<ConstantExpr>(re)->getZExtValue())));
          else
            out1->push_back(DA(new SymbolicAtom(re)));
        }
        if (out2)
          out2->push_back(re);
      }

      return len;
    }

    template <typename SourceDataIdentifier /*something I can pass to acquireExprRange*/>
    void reverseMemoryTransfer(klee::ExecutionState& state, ExprBuilder::RefExpr destAddr, SourceDataIdentifier sourceDataIdentifier, size_t const len, Node const srcNode) {
      typedef ExprBuilder::RefExpr Expr;

      Expr requirements = ExprBuilder::makeFalse();
      ConfigurationData::configureState(state);
      klee::Array const* array = state.makeNewSymbol(state.configurationData->self().compileSpecialSymbolName(TransmissionKind::pull),len);
      Expr accumulation = ExprBuilder::buildCompleteRead(array);

      klee::ConstraintManager cm;
      if (net::StateMapper* const sm = netEx.kleeNet.getStateMapper()) {
        sm->findTargets(state, srcNode);
        for (net::StateMapper::iterator it = sm->begin(), end = sm->end(); it != end; ++it) {
          klee::ExecutionState* const sourceState = static_cast<State*>(*it)->executionState();
          std::vector<Expr> expr;
          acquireExprRange(0, &expr, *sourceState, sourceDataIdentifier, len); // fills the newly pushed empty vector with our precious data, ... my precious!
          ConstraintSetTransfer const cst = ConstraintSet(TransmissionKind::pull, *sourceState, expr.begin(), expr.end()).extractFor(state);
          for (size_t i = 0; i < expr.size(); ++i) {
            expr[i] = cst.receiverData()[i];
          }
          assert(!expr.empty());
          // the (symbolic) value of the source.
          Expr value(ExprBuilder::buildEquality(accumulation, ExprBuilder::concat(expr.begin(),expr.end())));
          DD::cout << "  found new value: " << DD::endl << "> ";
          pprint(DD(),value,"> ");

          std::vector<ExprBuilder::RefExpr> const constr = cst.extractConstraints(ConstraintSetTransfer::FORCEALL);
          (*it)->incCompletedPullRequests();
          // these constraints will have to apply to the value.
          Expr constraints(ExprBuilder::conjunction(constr.begin(),constr.end()));
          // if the solver decides to use the value from this state for the symbol, it has to obey the constraints of this state.
          requirements = ExprBuilder::build<klee::OrExpr>(requirements,ExprBuilder::build<klee::AndExpr>(value,constraints));
          // note that it will at least have to choose one valuation, in order to satisfy the big-or.
        }
        sm->invalidate();
      }

      DD::cout << "Pulled REQUIREMENTS: " << DD::endl << "   ";
      pprint(DD(),requirements,"   ");

      std::pair<klee::MemoryObject const*,size_t> const destMo = findDestMo(state,destAddr);
      klee::ObjectState const* oseDest = state.addressSpace.findObject(destMo.first);
      assert(oseDest && "Destination ObjectState not found.");
      klee::ObjectState* wosDest = state.addressSpace.getWriteable(destMo.first, oseDest);
      for (size_t i = 0; i < len; ++i) {
        pprint(DD(), ExprBuilder::buildRead8(array,i), "           ");
        wosDest->write(destMo.second + i, ExprBuilder::buildRead8(array,i));
      }

      state.constraints.addConstraint(requirements);
    }

    // returns the unique memory object and the respective offset
    std::pair<klee::MemoryObject const*,size_t> findDestMo(klee::ExecutionState& state, klee::ref<klee::Expr> const& dest) const {
      klee::ResolutionList rl;
      state.addressSpace.resolve(state, netEx.solver, dest, rl);
      assert(rl.size() == 1 && "KleeNet memory operations expressions must resolve to precisely one object");

      return std::make_pair(rl[0].first,dyn_cast<ConstantExpr>(rl[0].first->getOffsetExpr(dest))->getZExtValue());
    }

    void memoryTransferWrapper(klee::ExecutionState& state,
                               klee::ref<klee::Expr> dest, size_t destLen,
                               net::ExData const& src,
                               net::Node destNode) {
      std::pair<klee::MemoryObject const*,size_t> const destMo = findDestMo(state,dest);

      // prepare mapping
      kleenet::PacketInfo pi(dyn_cast<ConstantExpr>(dest)->getZExtValue(),
                             destMo.second,
                             destLen,
                             destMo.first,
                             netEx.kleeNet.getStateNode(state),
                             destNode);

      netEx.kleeNet.memTxRequest(state, pi, src);
    }

    std::string readStringAtAddress(klee::ExecutionState& state, klee::ref<klee::Expr> address) {
      return parent.readStringAtAddress(state,address);
    }
  };

  SpecialFunctionHandler::SpecialFunctionHandler(Executor& netEx)
    : klee::SpecialFunctionHandler(netEx) // implicit downcast
    , impl(*(new SpecialFunctionHandler_impl(this,netEx))) {
    typedef sfh::SFHBase::ListEntry::GlobalList GL;
    net::util::SharedPtr<GL> gl = sfh::SFHBase::ListEntry::globalList();
    for (GL::iterator it = gl->begin(), en = gl->end(); it != en; ++it) {
      (*it)->sfhBase->main = &impl;
      (*it)->sfhBase->executor = &netEx;
      learnHandlerInfo(**it);
    }
  }

  SpecialFunctionHandler::~SpecialFunctionHandler() {
    delete &impl;
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
    klee::ExecutionState& state = ha.state;
    ExprBuilder::RefExpr destAddr = ha.arguments[0];
    size_t const len = args[2]->getZExtValue();
    Node const srcNode = args[3]->getZExtValue();
    assert(srcNode != executor->kleeNet.getStateNode(ha.state) && "destination node == calling node");

    if (!(0<len && len<(1u<<24))) // XXX arbitrary magic number (this has the reason to detect underflows, if user was brain-dead enough to pass us a negative length)
      klee::klee_error("Invalid data length for kleenet_get_global_symbol: %u\n",(unsigned)len);

    std::string const symbolName = main->readStringAtAddress(ha.state, ha.arguments[1]);

    if (klee::MemoryObject* const srcMo = (*main)[symbolName]) {
      if (len != srcMo->size)
        klee::klee_error("Size mismatch in kleenet_get_global_symbol, expected symbol of size %u but found symbol of size %u\n",(unsigned)len,(unsigned)(srcMo->size));

      main->reverseMemoryTransfer(state, destAddr, srcMo, len, srcNode); // <- main party here
    } else {
      klee::klee_error("Couldn't find symbol of name '%s' in kleenet_get_global_symbol.",symbolName.c_str());
    }
  }


  HAND(net::NodeId,kleenet_get_node_id,0) {
    return executor->kleeNet.getStateNode(ha.state).id;
  }

  HAND(net::Time,kleenet_get_virtual_time,0) {
    return executor->getNetSearcher()->getStateTime(ha.state);
  }

  HAND(void,kleenet_memcpy,4) {
    Node const destNode = args[3]->getZExtValue();
    size_t const len = args[2]->getZExtValue();
    assert(len > 0 && "n must be > 0");

    net::ExData values;
    main->memoryTransferWrapper(ha.state, ha.arguments[0], main->acquireExprRange(&values, 0, ha.state, ha.arguments[1], len), values, destNode);
  }

  HAND(void,kleenet_reverse_memcpy,4) {
    klee::ExecutionState& state = ha.state;
    ExprBuilder::RefExpr destAddr = ha.arguments[0];
    ExprBuilder::RefExpr sourceData = ha.arguments[1];
    size_t const len = args[2]->getZExtValue();
    Node const srcNode = args[3]->getZExtValue();

    main->reverseMemoryTransfer(state, destAddr, sourceData, len, srcNode);
  }

  HAND(void,kleenet_memset,4) {
    net::ExData value;
    size_t const len = args[2]->getZExtValue();
    assert(len && "asked to kleenet_memset 0 bytes.");
    value.push_back(net::util::SharedPtr<net::DataAtom>(new ConcreteAtom(args[1]->getZExtValue())));

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
    struct WakeupFunctor : net::util::DynamicFunctor<net::Node> {
      kleenet::Executor* executor;
      net::EventSearcher* ev;
      klee::ExecutionState& es;
      net::Node dest;
      WakeupFunctor(kleenet::Executor* executor, net::EventSearcher* ev, klee::ExecutionState& es, net::Node dest)
        : executor(executor), ev(ev), es(es), dest(dest) {}
      void operator()(net::Node d) const {
        if (net::StateMapper* const sm = executor->kleeNet.getStateMapper())
          if ((executor->stateCondition(&es) > 0) && (d == dest)) {
            /* sm->map(es, dest);
             We do not map for wakeup requests to allow the packet cache to work its magic.
             */
            sm->findTargets(es, dest);
            std::vector<net::BasicState*> targets(sm->begin(),sm->end());
            sm->invalidate();
            for (net::StateMapper::iterator it = targets.begin(), end = targets.end(); it != end; ++it) {
              net::BasicState *bs = *it;
              // schedule immediate wakeup
              ev->scheduleStateAt(bs, ev->lowerBound());
            }
          }
      }
    };
    net::EventSearcher* const ev = executor->getNetSearcher()->netSearcher()->toEventSearcher();
    if (ev) { // hey, we do have an event-capable searcher :)
      Node const dest = args[0]->getZExtValue();

      net::util::SharedPtr<net::util::DynamicFunctor<net::Node> > action(new WakeupFunctor(executor,ev,ha.state,dest));
      if (net::PacketCacheBase* pc = executor->kleeNet.getPacketCache()) {
        pc->onCommitDo(action);
      } else {
        // The following line is for testing only, it will IMMEDIATELY invoke the WakeupFunctor.
        // Change `true` to `false` above.
        (*action)(dest);
      }
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
