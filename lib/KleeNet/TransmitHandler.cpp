#include "TransmitHandler.h"

#include "klee/ExecutionState.h"
#include "klee/util/ExprVisitor.h"

#include "llvm/ADT/StringExtras.h"

#include "klee_headers/Memory.h"
#include "klee_headers/Common.h"
#include "klee_headers/MemoryManager.h"

#include "kleenet/KleeNet.h"
#include "AtomImpl.h"
#include "DistributedConstraints.h"
#include "NameMangling.h"
#include "NetExecutor.h"

#include <string>
#include <vector>
#include <algorithm>

#include "net/util/debug.h"
#include "kexPPrinter.h"

#include "net/util/BipartiteGraph.h"
#include "net/util/Containers.h"

#include <tr1/type_traits>

#define DD net::DEBUG<net::debug::external1>


namespace kleenet {

  template <typename T>
  void pprint(T const& t) {
    pprint(DD(),t,"| ");
  }

  class ReadTransformator : protected klee::ExprVisitor { // linear-time construction (linear in packet length)
    public:
      typedef std::vector<klee::ref<klee::Expr> > Seq;
    private:
      LazySymbolTranslator lst;
      Seq const& seq;
      Seq dynamicLookup;
      ReadTransformator(ReadTransformator const&); // don't implement
      ReadTransformator& operator=(ReadTransformator const&); // don't implement

      typedef klee::ExprVisitor::Action Action;
      Action visitRead(klee::ReadExpr const& re) {
        if (!llvm::isa<klee::ConstantExpr>(re.index)) {
          std::ostringstream errorBuf;
          errorBuf << "While parsing of read expression to '"
                   << re.updates.root->name;
          //klee::ExprPPrinter::printSingleExpr(errorBuf,re);
          errorBuf << "'; Encountered symbolic index which is currently not supported for packet data. Please file this as a feature request.";
          klee::klee_error("%s",errorBuf.str().c_str());
        }
        klee::ref<klee::Expr> const replacement = klee::ReadExpr::alloc( // needs review
            klee::UpdateList(lst(re.updates.root),re.updates.head) // head is cow-shared: magic
          , re.index /* XXX this could backfire, if we have complicated READ expressions in the index
                      * but for now we can simply assume not to have such weird stuff*/
        );
        return Action::changeTo(replacement);
      }

    public:
      ReadTransformator(NameMangler& mangler
                       , Seq const& seq
                       , LazySymbolTranslator::Symbols* preImageSymbols = NULL)
        : lst(mangler,preImageSymbols)
        , seq(seq)
        , dynamicLookup(seq.size(),klee::ref<klee::Expr>()) {
      }

      klee::ref<klee::Expr> const operator[](unsigned const index) {
        assert(dynamicLookup.size() && "Epsilon cannot be expanded into non-empty sequence.");
        unsigned const normIndex = index % dynamicLookup.size();
        klee::ref<klee::Expr>& slot = dynamicLookup[normIndex];
        if (slot.isNull())
          slot = visit(seq[normIndex]);
        return slot;
      }
      klee::ref<klee::Expr> const operator()(klee::ref<klee::Expr> const expr) {
        return visit(expr);
      }
      LazySymbolTranslator::TxMap const& symbolTable() const {
        return lst.symbolTable();
      }
  };


  template <typename BGraph, typename Key> class ExtractReadEdgesVisitor : public klee::ExprVisitor { // cheap construction (depends on Key copy-construction)
    private:
      BGraph& bg;
      Key key;
    protected:
      Action visitRead(klee::ReadExpr const& re) {
        bg.addUndirectedEdge(key,re.updates.root);
        return Action::skipChildren();
      }
    public:
      ExtractReadEdgesVisitor(BGraph& bg, Key key)
        : bg(bg)
        , key(key) {
      }
  };

  class ConstraintsGraph { // constant-time construction (but sizeable number of mallocs)
    private:
      typedef klee::ConstraintManager::constraints_ty Vec;
      typedef bg::Graph<bg::Props<klee::ref<klee::Expr>,klee::Array const*,net::util::UncontinuousDictionary,net::util::UncontinuousDictionary> > BGraph;
      BGraph bGraph;
      klee::ConstraintManager& cm;
      Vec::size_type knownConstraints;
      void updateGraph() {
        bGraph.addNodes(cm.begin()+knownConstraints,cm.end(),cm.size()-knownConstraints);
        for (Vec::const_iterator it = cm.begin()+knownConstraints, end = cm.end(); it != end; ++it) {
          ExtractReadEdgesVisitor<BGraph,Vec::value_type>(bGraph,*it).visit(*it);
        }
        knownConstraints = cm.size();
      }
    public:
      ConstraintsGraph(klee::ConstraintManager& cm)
        : bGraph()
        , cm(cm)
        , knownConstraints(0) {
      }
      template <typename ArrayContainer>
      std::vector<klee::ref<klee::Expr> > eval(ArrayContainer const request) {
        updateGraph();
        std::vector<klee::ref<klee::Expr> > needConstrs;
        bGraph.search(request,BGraph::IGNORE,&needConstrs);
        return needConstrs;
      }
  };

  class ConfigurationData : public ConfigurationDataBase { // constant-time construction (but sizeable number of mallocs)
    public:
      klee::ExecutionState& forState;
      ConstraintsGraph cg;
      StateDistSymbols distSymbols;
      class TxData { // linear-time construction (linear in packet length)
        friend class ConfigurationData;
        friend class PerReceiverData;
        private:
          size_t const currentTx;
          std::set<klee::Array const*> senderSymbols;
          ConfigurationData& cd;
          StateDistSymbols& distSymbolsSrc;
          typedef std::vector<klee::ref<klee::Expr> > ConList;
          ConList const seq;
          bool constraintsComputed;
          ConList senderConstraints; // untranslated!
          bool senderReflexiveArraysComputed;
          bool allowMorePacketSymbols; // once this flipps to false operator[] will be forbidden to find additional symbols in the packet
        public:
          std::string const specialTxName;
        private:

          template <typename T, typename It, typename Op>
          static std::vector<T> transform(It begin, It end, unsigned size, Op const& op, T /* type inference */) { // rvo takes care of us :)
            std::vector<T> v(size);
            std::transform(begin,end,v.begin(),op);
            return v;
          }
        public:
          TxData(ConfigurationData& cd, size_t currentTx, StateDistSymbols& distSymbolsSrc, std::vector<net::DataAtomHolder> const& data)
            : currentTx(currentTx)
            , senderSymbols()
            , cd(cd)
            , distSymbolsSrc(distSymbolsSrc)
            , seq(transform(data.begin(),data.end(),data.size(),dataAtomToExpr,klee::ref<klee::Expr>()/* type inference */))
            , constraintsComputed(false)
            , senderConstraints()
            , senderReflexiveArraysComputed(false)
            , allowMorePacketSymbols(true)
            , specialTxName(std::string("tx") + llvm::utostr(currentTx) + "(node" + llvm::itostr(distSymbolsSrc.node.id) + ")")
            {
          }
          ConList const& computeSenderConstraints() {
            if (!constraintsComputed) {
              allowMorePacketSymbols = false;
              constraintsComputed = true;
              assert(senderConstraints.empty() && "Garbate data in our sender's constraints buffer.");
              senderConstraints = cd.cg.eval(senderSymbols);
            }
            return senderConstraints;
          }
      };
      class PerReceiverData {
        public:
          TxData& txData;
        private:
          StateDistSymbols& distSymbolsDest;
          NameManglerHolder nmh;
          ReadTransformator rt;
          bool constraintsComputed;
          typedef TxData::ConList ConList;
          ConList receiverConstraints; // already translated!
        public:
          PerReceiverData(TxData& txData, StateDistSymbols& distSymbolsDest, size_t const beginPrecomputeRange, size_t const endPrecomputeRange)
            : txData(txData)
            , distSymbolsDest(distSymbolsDest)
            , nmh(txData.currentTx,txData.distSymbolsSrc,distSymbolsDest)
            , rt(nmh.mangler,txData.seq,&(txData.senderSymbols))
            , constraintsComputed(false)
            , receiverConstraints()
          {
            size_t const existingPacketSymbols = txData.senderSymbols.size();
            for (size_t it = beginPrecomputeRange; it != endPrecomputeRange; ++it)
              rt[it];
            assert((txData.allowMorePacketSymbols || (existingPacketSymbols == txData.senderSymbols.size())) \
              && "When precomuting all transmission atoms, we found completely new symbols, but we already assumed we were done with that.");
          }
          klee::ref<klee::Expr> operator[](size_t index) {
            size_t const existingPacketSymbols = txData.senderSymbols.size();
            klee::ref<klee::Expr> expr = rt[index];
            assert((txData.allowMorePacketSymbols || (existingPacketSymbols == txData.senderSymbols.size())) \
              && "When translating an atom, we found completely new symbols, but we already assumed we were done with that.");
            DD::cout << "| sndr[" << index << "] = "; pprint(txData.seq[index]);
            DD::cout << "| recv[" << index << "] = "; pprint(expr);
            return expr;
          }
          ConList const& computeNewReceiverConstraints() { // result already translated!
            if (!constraintsComputed) {
              constraintsComputed = true;
              assert(receiverConstraints.empty() && "Garbate data in our constraints buffer.");
              std::vector<klee::ref<klee::Expr> > const& senderConstraints = txData.computeSenderConstraints();
              receiverConstraints.resize(senderConstraints.size());
              std::transform<std::vector<klee::ref<klee::Expr> >::const_iterator, std::vector<klee::ref<klee::Expr> >::iterator, ReadTransformator&>(
                  senderConstraints.begin()
                , senderConstraints.end()
                , receiverConstraints.begin()
                , rt
              );
            }
            return receiverConstraints;
          }
        private:
          std::vector<std::pair<klee::Array const*,klee::Array const*> > additionalSenderOnlyConstraints() {
            std::vector<std::pair<klee::Array const*,klee::Array const*> > senderOnlyConstraints;
            if (!txData.senderReflexiveArraysComputed) {
              txData.senderReflexiveArraysComputed = true;
              txData.allowMorePacketSymbols = false;
              assert(senderOnlyConstraints.empty() && "Garbate data in our constraints buffer.");
              for (std::set<klee::Array const*>::const_iterator it = txData.senderSymbols.begin(), end = txData.senderSymbols.end(); it != end; ++it) {
                klee::Array const* const reflex = nmh.mangler.isReflexive(*it);
                if (reflex != *it) { // it's NOT reflexive, because it self-mangles to a different object
                  senderOnlyConstraints.reserve(txData.senderSymbols.size());
                  senderOnlyConstraints.push_back(std::make_pair(*it,reflex));
                }
              }
            }
            return senderOnlyConstraints;
          }
          LazySymbolTranslator::TxMap const& symbolTable() const {
            return rt.symbolTable();
          }
        public:
          bool isNonConstTransmission() const {
            return !(txData.senderSymbols.empty() && rt.symbolTable().empty());
          }
          struct GeneratedSymbolInformation {
            StateDistSymbols* belongsTo;
            klee::Array const* was;
            klee::Array const* translated;
            GeneratedSymbolInformation(StateDistSymbols* belongsTo, klee::Array const* was, klee::Array const* translated)
              : belongsTo(belongsTo)
              , was(was)
              , translated(translated) {
            }
            void addArrayToStateNames(klee::ExecutionState& state, net::Node src, net::Node dest) const {
              if (!state.arrayNames.insert(translated->name).second && !belongsTo->isDistributed(translated)) {
                klee::klee_error("%s",(std::string("In transmission of symbol '") + was->name + "' from node " + llvm::itostr(src.id) + " to node " + llvm::itostr(dest.id) + "; Symbol '" + translated->name + "' already exists on the target state. This is either a bug in KleeNet or you used the symbol '}' when specifying the name of a symbolic object which is not supported in KleeNet as it is used to mark special distributed symbols.").c_str());
              }
            }
          };
          typedef std::vector<GeneratedSymbolInformation> NewSymbols;
          NewSymbols newSymbols() { // rvo
            std::vector<std::pair<klee::Array const*,klee::Array const*> > senderOnlyConstraints = additionalSenderOnlyConstraints();
            NewSymbols result;
            result.reserve(senderOnlyConstraints.size()+rt.symbolTable().size());
            for (std::vector<std::pair<klee::Array const*,klee::Array const*> >::const_iterator it = senderOnlyConstraints.begin(), end = senderOnlyConstraints.end(); it != end; ++it) {
              if (it->first != it->second)
                result.push_back(GeneratedSymbolInformation(&(txData.distSymbolsSrc),it->first,it->second));
            }
            for (LazySymbolTranslator::TxMap::const_iterator it = rt.symbolTable().begin(), end = rt.symbolTable().end(); it != end; ++it) {
              if (it->first != it->second)
                result.push_back(GeneratedSymbolInformation(&distSymbolsDest,it->first,it->second));
            }
            return result;
          }
      };
    private:
      std::tr1::aligned_storage<sizeof(TxData),std::tr1::alignment_of<TxData>::value>::type txData_;
      TxData* txData;
      void updateTxData(std::vector<net::DataAtomHolder> const& data) {
        size_t const ctx = forState.getCompletedTransmissions() + 1;
        if (txData && (txData->currentTx != ctx))
          txData->~TxData();
        txData = new(&txData_) TxData(*this,ctx,distSymbols,data);
      }
    public:
      ConfigurationData(klee::ExecutionState& state, net::Node src)
        : forState(state)
        , cg(state.constraints)
        , distSymbols(src)
        , txData(0) {
      }
      ~ConfigurationData() {
        if (txData)
          txData->~TxData();
      }
      TxData& transmissionProperties(std::vector<net::DataAtomHolder> const& data) {
        updateTxData(data);
        return *txData;
      }
      ConfigurationData& self() {
        return *this;
      }
      static void configureState(klee::ExecutionState& state, KleeNet& kleenet) {
        if ((!state.configurationData) || (&(static_cast<ConfigurationData&>(*state.configurationData).forState) != &state)) {
          if (!state.configurationData)
            delete state.configurationData;
          state.configurationData = new ConfigurationData(state,kleenet.getStateNode(state)); //shared pointer takes care or deletion
        }
      }
  };
}

using namespace kleenet;

namespace klee {
  class ObjectState;
  class Expr;
}

void TransmitHandler::handleTransmission(PacketInfo const& pi, net::BasicState* basicSender, net::BasicState* basicReceiver, std::vector<net::DataAtomHolder> const& data) const {
  size_t const currentTx = basicSender->getCompletedTransmissions() + 1;
  DD::cout << DD::endl
           << "+------------------------------------------------------------------------------+" << DD::endl
           << "| STARTING TRANSMISSION #" << currentTx << " from node `" << pi.src.id << "' to `" << pi.dest.id << "'.                               |" << DD::endl
           << "|                                                                              |" << DD::endl;
  klee::ExecutionState& sender = static_cast<klee::ExecutionState&>(*basicSender);
  klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(*basicReceiver);

  ConfigurationData::configureState(sender,kleenet);
  ConfigurationData::configureState(receiver,kleenet);

  DD::cout << "| " << "Involved states: " << &sender << " ---> " << &receiver << DD::endl;
  ConfigurationData::PerReceiverData receiverData(
      sender.configurationData->self().transmissionProperties(data)
    , receiver.configurationData->self().distSymbols
    , 0, pi.length // precomputation of symbols
  );
  klee::ObjectState const* oseDest = receiver.addressSpace.findObject(pi.destMo);
  assert(oseDest && "Destination ObjectState not found.");
  klee::ObjectState* wosDest = receiver.addressSpace.getWriteable(pi.destMo, oseDest);
  bool const hasSymbolics = receiverData.isNonConstTransmission();
  if (hasSymbolics) {
    klee::MemoryObject* const mo = receiver.getExecutor()->memory->allocate(pi.length,false,true,NULL);
    mo->setName(receiverData.txData.specialTxName);
    klee::Array const* const array = new klee::Array(receiverData.txData.specialTxName,mo->size);
    klee::ObjectState* const ose = new klee::ObjectState(mo,array);
    receiver.addressSpace.bindObject(mo,ose);
    receiver.addSymbolic(mo,array);

    for (unsigned i = 0; i < pi.length; i++) {
      StateDistSymbols::RefExpr r8 = StateDistSymbols::buildRead8(array,i);
      wosDest->write(pi.offset + i, r8);
      receiver.constraints.addConstraint(StateDistSymbols::buildEquality(r8,receiverData[i]));
    }
  } else {
    for (unsigned i = 0; i < pi.length; i++) {
      wosDest->write(pi.offset + i, receiverData[i]);
    }
  }
  DD::cout << "| " << "! Using the following transmission context: " << &receiverData << DD::endl;
  if (hasSymbolics) {
    DD::cout << "| " << "Sender Constraints:" << DD::endl;
    DD::cout << "| "; pprint(sender.constraints);
    DD::cout << "| " << "Receiver Constraints:" << DD::endl;
    DD::cout << "| "; pprint(receiver.constraints);
    DD::cout << "| " << "Listing OFFENDING constraints:" << DD::endl;
    for (net::util::LoopConstIterator<std::vector<klee::ref<klee::Expr> > > it(receiverData.computeNewReceiverConstraints()); it.more(); it.next()) {
      DD::cout << "| " << "adding constraint ... " << DD::endl;
      DD::cout << "| "; pprint(*it);
      receiver.constraints.addConstraint(*it);
    }
    DD::cout << "| " << "EOF Constraints." << DD::endl;
    DD::cout << "| " << "Listing OFFENDING symbols:" << DD::endl;
    ConfigurationData::PerReceiverData::NewSymbols const newSymbols = receiverData.newSymbols();
    for (ConfigurationData::PerReceiverData::NewSymbols::const_iterator it = newSymbols.begin(), end = newSymbols.end(); it != end; ++it) {
      bool const isOnSender = it->belongsTo->node == pi.src;
      DD::cout << "| " << " + (on " << (isOnSender?"sender state)    ":"receiver state)  ") << it->was->name << "  |--->  " << it->translated->name << DD::endl;
      it->addArrayToStateNames(isOnSender?sender:receiver,pi.src,pi.dest);
      if (isOnSender) {
        DD::cout << "| " << "    -- reflexive: " << it->was->name << " == " << it->translated->name << DD::endl;
        klee::ref<klee::Expr> const eq = StateDistSymbols::buildEquality(it->was,it->translated);
        DD::cout << "| "; pprint(eq);
        sender.constraints.addConstraint(eq);
      }
    }
  } else {
    DD::cout << "| " << "All data in tx is constant. Bypassing Graph construction." << DD::endl;
  }
  DD::cout << "|                                                                              |" << DD::endl
           << "| EOF TRANSMISSION #" << currentTx << "                                                          |" << DD::endl
           << "+------------------------------------------------------------------------------+" << DD::endl
           << DD::endl;
}
