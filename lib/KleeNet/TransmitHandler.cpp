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
#include <sstream>

#include "net/util/debug.h"
#include "klee/util/ExprPPrinter.h"
#include <iostream>

#include "net/util/BipartiteGraph.h"
#include "net/util/Containers.h"

#include <tr1/type_traits>

#define DD net::DEBUG<net::debug::external1>

namespace {
  template <typename T>
  struct pp {
  };

  template <>
  struct pp<klee::ConstraintManager> {
    typedef klee::ConstraintManager const& Ref;
    static void pprint(Ref cm) {
      if (DD::enable) {
        klee::ExprPPrinter::printConstraints(std::cout,cm);
        std::cout << std::endl;
      }
    }
  };
  template <>
  struct pp<klee::ref<klee::Expr> > {
    typedef klee::ref<klee::Expr> Ref;
    static void pprint(Ref expr) {
      if (DD::enable) {
        klee::ExprPPrinter::printSingleExpr(std::cout,expr);
        std::cout << std::endl;
      }
    }
  };
  template <typename T>
  void pprint(T const& t) {
    pp<T>::pprint(t);
  }
}

namespace kleenet {

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
        klee::ref<klee::Expr> const replacement = klee::ReadExpr::alloc(
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
        DD::cout << needConstrs.size() << " constraint following ..." << DD::endl;
        return needConstrs;
      }
  };

  class ConfigurationData : public ConfigurationDataBase { // constant-time construction (but sizeable number of mallocs)
    public:
      klee::ExecutionState& forState;
      ConstraintsGraph cg;
      class TxData { // linear-time construction (linear in packet length)
        friend class ConfigurationData;
        friend class PerReceiverData;
        private:
          size_t const currentTx;
          std::set<klee::Array const*> senderSymbols;
          ConstraintsGraph& cg;
          StateDistSymbols& distSymbols;
          net::Node src;
          net::Node dest;
          std::vector<klee::ref<klee::Expr> > const seq;
          bool allowMorePacketSymbols; // once this flipps to false operator[] will be forbidden to find additional symbols in the packet
          template <typename T, typename It, typename Op>
          static std::vector<T> transform(It begin, It end, unsigned size, Op const& op, T /* type inference */) { // rvo takes care of us :)
            std::vector<T> v(size);
            std::transform(begin,end,v.begin(),op);
            return v;
          }
        public:
          TxData(size_t currentTx, StateDistSymbols& distSymbols, net::Node src, net::Node dest, std::vector<net::DataAtomHolder> const& data, ConstraintsGraph& cg)
            : currentTx(currentTx)
            , senderSymbols()
            , cg(cg)
            , distSymbols(distSymbols)
            , src(src)
            , dest(dest)
            , seq(transform(data.begin(),data.end(),data.size(),dataAtomToExpr,klee::ref<klee::Expr>()/* type inference */))
            , allowMorePacketSymbols(true)
            {
          }
      };
      class PerReceiverData {
        private:
          TxData& txData;
          NameManglerHolder nmh;
          ReadTransformator rt;
          bool constraintsComputed;
          std::vector<klee::ref<klee::Expr> > receiverConstraints; // already translated!
        public:
          PerReceiverData(TxData& txData)
            : txData(txData)
            , nmh(txData.currentTx,txData.src,txData.dest)
            , rt(nmh.mangler,txData.seq,&(txData.senderSymbols))
            , constraintsComputed(false)
            , receiverConstraints()
            {
          }
          klee::ref<klee::Expr> operator[](size_t index) {
            size_t const existingPacketSymbols = txData.senderSymbols.size();
            klee::ref<klee::Expr> expr = rt[index];
            assert((txData.allowMorePacketSymbols || (existingPacketSymbols == txData.senderSymbols.size())) \
              && "When translating an atom, we found completely new symbols, but we already assumed we were done with that.");
            DD::cout << "Packet[" << index << "] = "; pprint(expr);
            return expr;
          }
          std::vector<klee::ref<klee::Expr> > const& computeNewReceiverConstraints() { // result already translated!
            if (!constraintsComputed) {
              txData.allowMorePacketSymbols = false;
              constraintsComputed = true;
              assert(receiverConstraints.empty() && "Garbate data in our constraints buffer.");
              receiverConstraints = txData.cg.eval(txData.senderSymbols);
              std::transform<std::vector<klee::ref<klee::Expr> >::const_iterator, std::vector<klee::ref<klee::Expr> >::iterator, ReadTransformator&>(receiverConstraints.begin(),receiverConstraints.end(),receiverConstraints.begin(),rt);
            }
            return receiverConstraints;
          }
          std::set<klee::Array const*> const& peekSymbols() const { // debug
            return txData.senderSymbols;
          }
          LazySymbolTranslator::TxMap const& symbolTable() const {
            return rt.symbolTable();
          }
      };
    private:
      net::Node const src;
      StateDistSymbols symbols;
      std::tr1::aligned_storage<sizeof(TxData),std::tr1::alignment_of<TxData>::value>::type txData_;
      TxData* txData;
      void updateTxData(std::vector<net::DataAtomHolder> const& data, net::Node dest) {
        size_t const ctx = forState.getCompletedTransmissions() + 1;
        if (txData && (txData->currentTx != ctx))
          txData->~TxData();
        txData = new(&txData_) TxData(ctx,symbols,src,dest,data,cg);
      }
    public:
      ConfigurationData(klee::ExecutionState& state, net::Node src)
        : forState(state)
        , cg(state.constraints)
        , src(src)
        , symbols(src)
        , txData(0) {
      }
      ~ConfigurationData() {
        if (txData)
          txData->~TxData();
      }
      TxData& transmissionProperties(std::vector<net::DataAtomHolder> const& data, net::Node dest) {
        updateTxData(data,dest);
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
            //<< "+------------------------------------------------------------------------------+" << DD::endl
            << DD::endl;
  klee::ExecutionState& sender = static_cast<klee::ExecutionState&>(*basicSender);
  klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(*basicReceiver);

  ConfigurationData::configureState(sender,kleenet);
  ConfigurationData::configureState(receiver,kleenet);

  DD::cout << "Involved states: " << &sender << " ---> " << &receiver << DD::endl;
  ConfigurationData& cd = static_cast<ConfigurationData&>(*sender.configurationData); // it's a ConfigurationDataBase
  ConfigurationData::PerReceiverData receiverData(cd.transmissionProperties(data,pi.dest));
  // memcpy
  klee::ObjectState const* ose = receiver.addressSpace.findObject(pi.destMo);
  assert(ose && "Destination ObjectState not found.");
  klee::ObjectState* wos = receiver.addressSpace.getWriteable(pi.destMo, ose);
  DD::cout << "Packet data: " << DD::endl;
  // important remark: data might be longer or shorter than pi.length. Always obey the size dictated by PacketInfo.
  for (unsigned i = 0; i < pi.length; i++) {
    wos->write(pi.offset + i, receiverData[i]);
  }
  DD::cout << "! Using the following transmission context: " << &receiverData << DD::endl;
  if (!receiverData.peekSymbols().empty()) {
    DD::cout << "Symbols in transmission:" << DD::endl;
    for (std::set<klee::Array const*>::const_iterator it = receiverData.peekSymbols().begin(), end = receiverData.peekSymbols().end(); it != end; ++it) {
      DD::cout << " + " << (*it)->name << DD::endl;
    }
    DD::cout << "Sender Constraints:" << DD::endl;
    pprint(sender.constraints);
    DD::cout << "Receiver Constraints:" << DD::endl;
    pprint(receiver.constraints);
    DD::cout << "Listing OFFENDING constraints:" << DD::endl;
    for (net::util::LoopConstIterator<std::vector<klee::ref<klee::Expr> > > it(receiverData.computeNewReceiverConstraints()); it.more(); it.next()) {
      DD::cout << " ! adding constraint ... "; pprint(*it);
      receiver.constraints.addConstraint(*it);
    }
    DD::cout << "EOF Constraints." << DD::endl;
    DD::cout << "Listing OFFENDING symbols:" << DD::endl;
    for (LazySymbolTranslator::TxMap::const_iterator it = receiverData.symbolTable().begin(), end = receiverData.symbolTable().end(); it != end; ++it) {
      DD::cout << " + " << it->first->name << "  |--->  " << it->second->name << DD::endl;
      bool const didntExist = receiver.arrayNames.insert(it->second->name).second;
      if (!didntExist) {
        klee::klee_error("%s",(std::string("In transmission of symbol '") + it->first->name + "' from node " + llvm::utostr(pi.src.id) + " to node " + llvm::utostr(pi.dest.id) + "; Symbol '" + it->second->name + "' already exists on the target state. This is either a bug in KleeNet or you used the symbol '{' when specifying the name of a symbolic object.").c_str());
      }
      klee::MemoryObject* const mo = receiver.getExecutor()->memory->allocate(it->second->size,false,true,NULL);
      mo->setName(it->second->name);
      /* TODO */ receiver.addSymbolic(mo,it->second); /**/ // XXX I think this is wrong! CHECK! XXX
    }
  } else {
    DD::cout << "All data in tx is constant. Bypassing Graph construction." << DD::endl;
  }
  DD::cout << DD::endl
            //<< "+------------------------------------------------------------------------------+" << DD::endl
            << "| EOF TRANSMISSION #" << currentTx << "                                                          |" << DD::endl
            << "+------------------------------------------------------------------------------+" << DD::endl
            << DD::endl;
}
