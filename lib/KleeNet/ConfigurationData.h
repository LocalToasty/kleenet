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
#include "net/util/Type.h"
#include "net/Iterator.h"

#include "klee/util/ExprVisitor.h"

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
                       , LazySymbolTranslator::Symbols* preImageSymbols = NULL
                       , LazySymbolTranslator::Symbols* translatedSymbols = NULL
                       );

      klee::ref<klee::Expr> const operator[](unsigned const index);
      klee::ref<klee::Expr> const operator()(klee::ref<klee::Expr> const expr);
      LazySymbolTranslator::TxMap const& symbolTable() const;
  };

  class ConstraintsGraph { // constant-time construction (but sizeable number of mallocs)
    public:
      typedef klee::ref<klee::Expr> Constraint;
      typedef std::vector<Constraint> ConstraintList;
    private:
      typedef bg::Graph<bg::Props<Constraint,klee::Array const*,net::util::UncontinuousDictionary,net::util::UncontinuousDictionary> > BGraph;
      BGraph bGraph;
      klee::ConstraintManager& cm;
      void updateGraph();
    public:
      ConstraintsGraph(klee::ConstraintManager& cm)
        : bGraph()
        , cm(cm)
        {
      }
      template <typename ArrayContainer>
      ConstraintList eval(ArrayContainer const request) {
        updateGraph();
        std::vector<Constraint> needConstrs;
        bGraph.search(request,BGraph::IGNORE,&needConstrs);
        return needConstrs;
      }
  };

  class SenderTxData;

  struct TransmissionKind {
    enum Enum {
      tx,
      pull
    };
  };

  class ConfigurationData : public ConfigurationDataBase { // constant-time construction (but sizeable number of mallocs)
    public:
      State& forState;
      ConstraintsGraph cg;
      StateDistSymbols distSymbols;
      typedef std::vector<klee::ref<klee::Expr> > ConList;
      class PerReceiverData {
        private:
          SenderTxData& txData;
          ConfigurationData& receiverConfig;
          NameManglerHolder nmh;
          ReadTransformator rt;
          bool constraintsComputed;
        public:
          std::string const& specialTxName;
        public:
          PerReceiverData(SenderTxData& txData, ConfigurationData& receiverConfig, size_t const beginPrecomputeRange, size_t const endPrecomputeRange);
          klee::ref<klee::Expr> operator[](size_t index);
          void transferNewReceiverConstraints(net::util::DynamicFunctor<klee::ref<klee::Expr> > const&, bool txConstraintsTransmission);
        private:
          std::vector<std::pair<klee::Array const*,klee::Array const*> > additionalSenderOnlyConstraints();
          LazySymbolTranslator::TxMap const& symbolTable() const {
            return rt.symbolTable();
          }
        public:
          bool isNonConstTransmission() const;
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
      SenderTxData* txData;
      ConfigurationData(ConfigurationData const&); // not implemented
      ConfigurationData(ConfigurationData const&,State*);
      ConfigurationData* fork(State*) const;
    public:
      ConfigurationData(klee::ExecutionState& state, net::Node src);
      ~ConfigurationData();
      SenderTxData& transmissionProperties(net::ConstIteratable<klee::ref<klee::Expr> > const&, net::ConstIteratable<klee::ref<klee::Expr> > const&, TransmissionKind::Enum kind);
      ConfigurationData& self() {
        return *this;
      }
      static void configureState(klee::ExecutionState& state);
  };
}
