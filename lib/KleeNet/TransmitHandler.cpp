#include "TransmitHandler.h"

#include "klee/ExecutionState.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "klee/util/ExprUtil.h"

#include "llvm/ADT/StringExtras.h"

#include "klee_headers/Memory.h"

#include "AtomImpl.h"

#include <map>
#include <vector>
#include <algorithm>
#include <iterator>
#include <sstream>

#include <iostream> // Debug
#include "klee/util/ExprPPrinter.h"

#include "net/util/BipartiteGraph.h"
#include "net/util/Containers.h"

#include <tr1/type_traits>

namespace kleenet {
  class NameMangler { // cheap construction
    public:
      std::string const appendToName;
      NameMangler(std::string const appendToName)
        : appendToName(appendToName) {
      }
      std::string operator()(std::string const& str) const {
        //std::string const nameMangle = str + appendToName;
        //std::string uniqueName = nameMangle;
        //unsigned uniqueId = 1;
        //while (!scope.arrayNames.insert(uniqueName).second)
        //  uniqueName = nameMangle + "(" + llvm::utostr(++uniqueId) + ")"; // yes, we start with (2)
        return str + appendToName;
      }
      klee::Array* operator()(klee::Array const* array) const {
        return new klee::Array((*this)(array->name),array->size);
      }
  };

  class LazySymbolTranslator { // cheap construction
    public:
      typedef std::set<klee::Array const*> Symbols;
    private:
      NameMangler mangle;
    protected:
      typedef std::map<klee::Array const*,klee::Array*> TxMap;
      TxMap txMap;
      Symbols* const preImageSymbols;
    public:
      LazySymbolTranslator(NameMangler mangle, Symbols* const preImageSymbols = NULL)
        : mangle(mangle)
        , preImageSymbols(preImageSymbols) {
      }
      klee::Array* operator()(klee::Array const* array) {
        if (preImageSymbols)
          preImageSymbols->insert(array);
        klee::Array*& it = txMap[array];
        if (!it)
          it = mangle(array);
        return it;
      }
  };

  class ReplaceReadVisitor : public klee::ExprVisitor { // cheap construction
    private:
      typedef klee::ExprVisitor::Action Action;
      LazySymbolTranslator& lst;
    protected:
      Action visitRead(klee::ReadExpr const& re) {
        klee::ref<klee::Expr> const replacement = klee::ReadExpr::alloc(
            klee::UpdateList(lst(re.updates.root),re.updates.head) // head is cow-shared: magic
          , re.index /* XXX this could backfire, if we have complicated READ expressions in the index
                      * but for now we can simply assume not to have such weird stuff*/
        );
        return Action::changeTo(replacement);
      }
    public:
      ReplaceReadVisitor(LazySymbolTranslator& lst)
        : lst(lst) {
      }
  };

  class ReadTransformator { // linear-time construction (linear in packet length)
    public:
      typedef std::vector<klee::ref<klee::Expr> > Seq;
    private:
      template <typename T, typename It, typename Op> static std::vector<T> transform(It begin, It end, unsigned size, Op const& op) { // rvo takes care of us :)
        std::vector<T> v(size);
        std::transform(begin,end,v.begin(),op);
        return v;
      }
      LazySymbolTranslator lst;
      ReplaceReadVisitor rrv;
      Seq const seq;
      Seq dynamicLookup;
      ReadTransformator(ReadTransformator const&); // don't implement
      ReadTransformator& operator=(ReadTransformator const&); // don't implement

    public:
      template <typename Container, typename UnaryOperation>
      ReadTransformator(std::string const appendToName,
                        Container const& input, UnaryOperation const& op,
                        LazySymbolTranslator::Symbols* preImageSymbols = NULL)
        : lst(NameMangler(appendToName),preImageSymbols)
        , rrv(lst)
        , seq(transform<klee::ref<klee::Expr>,typename Container::const_iterator,UnaryOperation>(
                input.begin(),input.end(),input.size(),op))
        , dynamicLookup(input.size(),klee::ref<klee::Expr>()) {
        assert(dynamicLookup.size() == seq.size());
      }

      klee::ref<klee::Expr> const operator[](unsigned const index) {
        assert(dynamicLookup.size() && "Epsilon cannot be expanded into non-empty sequence.");
        unsigned const normIndex = index % dynamicLookup.size();
        klee::ref<klee::Expr>& slot = dynamicLookup[normIndex];
        if (slot.isNull())
          slot = rrv.visit(seq[normIndex]);
        return slot;
      }
      klee::ref<klee::Expr> const operator()(klee::ref<klee::Expr> const expr) {
        return rrv.visit(expr);
      }
  };


  template <typename BGraph, typename Key> class ExtractReadEdgesVisitor : public klee::ExprVisitor { // cheap construction (depends on Key copy-construction)
    private:
      BGraph& bg;
      Key key;
    protected:
      Action visitRead(klee::ReadExpr const& re) {
        //std::cout << "Adding array " << re.updates.root << " to constraint slot " << key << ";" << std::endl;
        //klee::ExprPPrinter::printSingleExpr(std::cout,klee::ref<klee::Expr>(const_cast<klee::ReadExpr*>(&re))); std::cout << std::endl;
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
      typedef bg::Graph<bg::Props<Vec::size_type,klee::Array const*,net::util::ContinuousDictionary,net::util::UncontinuousDictionary> > BGraph;
      BGraph bGraph;
      klee::ConstraintManager& cm;
      Vec::size_type knownConstraints;
      void updateGraph() {
        typedef net::util::ExtractContainerKeys<Vec> Constrs;
        // we need the differentiation between the container "begin" (first argument)
        // and the position to actually stary (second argument), in order to get the indices right
        Constrs constrs(cm.begin(),cm.begin()+knownConstraints,cm.end(),cm.size());
        bGraph.addNodes(constrs);
        for (Vec::const_iterator begin = cm.begin(), it = cm.begin()+knownConstraints, end = cm.end(); it != end; ++it) {
          klee::ExprPPrinter::printSingleExpr(std::cout,*it); std::cout << std::endl;
          ExtractReadEdgesVisitor<BGraph,Vec::size_type>(bGraph,it - begin).visit(*it);
        }
        knownConstraints = cm.size();
      }
      struct ConstraintLookup { // cheap construction
        klee::ConstraintManager const& cm;
        ConstraintLookup(klee::ConstraintManager const& cm) : cm(cm) {}
        klee::ConstraintManager::constraints_ty::value_type operator()(klee::ConstraintManager::constraints_ty::size_type index) const {
          return cm.begin()[index]; // vector iterators are pointers ;)
        }
      };
    public:
      ConstraintsGraph(klee::ConstraintManager& cm)
        : bGraph()
        , cm(cm)
        , knownConstraints(0) {
      }
      template <typename ArrayContainer>
      void eval(ArrayContainer const request) {
        updateGraph();
        std::vector<klee::ref<klee::Expr> > needConstrs;
        std::vector<Vec::size_type> needConstrIndices;
        std::vector<klee::Array const*> needSymbols;
        bGraph.search(request,&needSymbols,&needConstrIndices);
        //return net::util::adHocContainerTransformation( // you can see my Haskell roots, can't you?
        //  bGraph.search(request)
        //, ConstraintLookup(cm)
        //, klee::ref<klee::Expr>()//just for type inference
        //, std::vector<klee::ref<klee::Expr> >()//just for type inference
        //);
      }
  };

  class ConfigurationData : public ConfigurationDataBase { // constant-time construction (but sizeable number of mallocs)
    public:
      klee::ExecutionState& forState;
      ConstraintsGraph cg;
      class TxData { // linear-time construction (linear in packet length)
        friend class ConfigurationData;
        private:
          size_t const currentTx;
          std::set<klee::Array const*> senderSymbols;
          ReadTransformator rt;
        public:
          TxData(size_t currentTx, net::Node src, net::Node dest, std::vector<net::DataAtomHolder> const& data)
            : currentTx(currentTx)
            , senderSymbols()
            , rt("{tx" + llvm::utostr(currentTx) + ":" + llvm::utostr(src.id) + "->" + llvm::utostr(dest.id) + "}",data,dataAtomToExpr,&senderSymbols)
            //, senderArrayCollector(senderSymbols)
            //, visited(data.size(),false)
            {
          }
          klee::ref<klee::Expr> operator[](size_t index) {
            //assert(index < visited.size());
            klee::ref<klee::Expr> expr = rt[index];
            klee::ExprPPrinter::printSingleExpr(std::cout,expr); std::cout << std::endl;
            //if ((!llvm::isa<klee::ConstantExpr>(expr)) && (!visited[index]))
            //  senderArrayCollector.visit(expr);
            return expr;
          }
          std::set<klee::Array const*> const& peekSymbols() const { // debug
            return senderSymbols;
          }
      };
    private:
      net::Node const src;
      net::Node const dest;
      typename std::tr1::aligned_storage<sizeof(TxData),std::tr1::alignment_of<TxData>::value>::type txData_;
      TxData* txData;
      void updateTxData(std::vector<net::DataAtomHolder> const& data) {
        size_t const ctx = forState.getCompletedTransmissions() + 1;
        if (txData && (txData->currentTx != ctx))
          txData->~TxData();
        txData = new(&txData_) TxData(ctx,src,dest,data);
      }
    public:
      ConfigurationData(klee::ExecutionState& state, net::Node src, net::Node dest)
        : forState(state)
        , cg(state.constraints)
        , src(src)
        , dest(dest)
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
  };
}

using namespace kleenet;

namespace klee {
  class ObjectState;
  class Expr;
}

void TransmitHandler::handleTransmission(PacketInfo const& pi, net::BasicState* basicSender, net::BasicState* basicReceiver, std::vector<net::DataAtomHolder> const& data) const {
  std::cout << std::endl
            << "+------------------------------------------------------------------------------+" << std::endl
            << "| STARTING TRANSMISSION from node `" << pi.src.id << "' to `" << pi.dest.id << "'.                                  |" << std::endl
            << "+------------------------------------------------------------------------------+" << std::endl
            << std::endl;
  //size_t const currentTx = basicSender->getCompletedTransmissions() + 1;
  klee::ExecutionState& sender = static_cast<klee::ExecutionState&>(*basicSender);
  klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(*basicReceiver);
  if ((!sender.configurationData) || (&(static_cast<ConfigurationData&>(*sender.configurationData).forState) != &sender)) {
    if (!sender.configurationData)
      delete sender.configurationData;
    sender.configurationData = new ConfigurationData(sender,pi.src,pi.dest); //shared pointer takes care or deletion
  }
  ConfigurationData& cd = static_cast<ConfigurationData&>(*sender.configurationData); // it's a ConfigurationDataBase
  ConfigurationData::TxData& txData = cd.transmissionProperties(data);
  // memcpy
  klee::ObjectState const* ose = receiver.addressSpace.findObject(pi.destMo);
  assert(ose && "Destination ObjectState not found.");
  klee::ObjectState* wos = receiver.addressSpace.getWriteable(pi.destMo, ose);
  //std::set<klee::Array const*> symbols;
  // important remark: data might be longer or shorter than pi.length. Always obey the size dictated by PacketInfo.
  for (unsigned i = 0; i < pi.length; i++) {
    wos->write(pi.offset + i, txData[i]);
  }
  std::cout << "! Using the following transmission context: " << &txData << std::endl;
  std::cout << "Symbols in transmission:" << std::endl;
  for (std::set<klee::Array const*>::const_iterator it = txData.peekSymbols().begin(), end = txData.peekSymbols().end(); it != end; ++it) {
    std::cout << " + " << (*it)->name << std::endl;
  }
  std::cout << "Sender Constraints:" << std::endl;
  klee::ExprPPrinter::printConstraints(std::cout,sender.constraints); std::cout << std::endl;
  std::cout << "Receiver Constraints:" << std::endl;
  klee::ExprPPrinter::printConstraints(std::cout,receiver.constraints); std::cout << std::endl;
  std::cout << "EOF Constraints." << std::endl;
  //if (!symbols.empty()) {
  //  klee::ExprPPrinter::printConstraints(std::cout,sender.constraints); std::cout << std::endl;
  //  ConstraintsGraph cg(sender.constraints);
  //  cg.eval(symbols);
  //  // TODO: start the search with the ReadExpressions that are used in the transmission

  //  klee::ConstraintManager& sc(sender.constraints);
  //  klee::ConstraintManager& rc(receiver.constraints);
  //  for (klee::ConstraintManager::const_iterator it = sc.begin(), en = sc.end(); it != en; ++it) {
  //    rc.addConstraint(rt(*it));
  //  }
  //}
  std::cout << std::endl
            << "+------------------------------------------------------------------------------+" << std::endl
            << "| EOF TRANSMISSION                                                             |" << std::endl
            << "+------------------------------------------------------------------------------+" << std::endl
            << std::endl;
}
