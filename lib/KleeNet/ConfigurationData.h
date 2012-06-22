//===-- ConfigurationData.h -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "kleenet/KleeNet.h"

#include "DistributedConstraints.h"
#include "NameMangling.h"
#include "AtomImpl.h"

#include "kleenet/State.h"
#include "net/util/BipartiteGraph.h"
#include "net/util/Containers.h"

#include "klee/util/ExprVisitor.h"

#include "klee_headers/Memory.h"
#include "klee_headers/MemoryManager.h"
#include "klee_headers/Common.h"

#include <sstream>
#include <tr1/type_traits>

namespace klee {
  class ExecutionState;
  class ConstraintManager;
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
      Action visitRead(klee::ReadExpr const& re);

    public:
      ReadTransformator(NameMangler& mangler
                       , Seq const& seq
                       , LazySymbolTranslator::Symbols* preImageSymbols = NULL);

      klee::ref<klee::Expr> const operator[](unsigned const index);
      klee::ref<klee::Expr> const operator()(klee::ref<klee::Expr> const expr);
      LazySymbolTranslator::TxMap const& symbolTable() const;
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
      typedef bg::Graph<bg::Props<klee::ref<klee::Expr>,klee::Array const*,net::util::UncontinuousDictionary,net::util::UncontinuousDictionary> > BGraph;
      BGraph bGraph;
      klee::ConstraintManager& cm;
      size_t knownConstraints;
      void updateGraph();
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
      State& forState;
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
          klee::ref<klee::Expr> operator[](size_t index);
          ConList const& computeNewReceiverConstraints();
        private:
          std::vector<std::pair<klee::Array const*,klee::Array const*> > additionalSenderOnlyConstraints();
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
            void addArrayToStateNames(klee::ExecutionState& state, net::Node src, net::Node dest) const;
          };
          typedef std::vector<GeneratedSymbolInformation> NewSymbols;
          NewSymbols newSymbols();
      };
    private:
      std::tr1::aligned_storage<sizeof(TxData),std::tr1::alignment_of<TxData>::value>::type txData_;
      TxData* txData;
      void updateTxData(std::vector<net::DataAtomHolder> const& data);
    public:
      ConfigurationData(klee::ExecutionState& state, net::Node src);
      ~ConfigurationData();
      TxData& transmissionProperties(std::vector<net::DataAtomHolder> const& data);
      ConfigurationData& self() {
        return *this;
      }
      static void configureState(klee::ExecutionState& state, KleeNet& kleenet);
  };
}
