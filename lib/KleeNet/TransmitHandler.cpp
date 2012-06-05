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

#include "net/util/BinaryGraph.h"
#include "net/util/Containers.h"

namespace kleenet {
  class NameMangler {
    public:
      klee::ExecutionState& scope;
      std::string const appendToName;
      NameMangler(klee::ExecutionState& scope, std::string const appendToName)
        : scope(scope)
        , appendToName(appendToName) {
      }
      std::string operator()(std::string const& str) const {
        std::string const nameMangle = str + appendToName;
        std::string uniqueName = nameMangle;
        unsigned uniqueId = 1;
        while (!scope.arrayNames.insert(uniqueName).second)
          uniqueName = nameMangle + "(" + llvm::utostr(++uniqueId) + ")"; // yes, we start with (2)
        return uniqueName;
      }
      klee::Array* operator()(klee::Array const* array) const {
        return new klee::Array((*this)(array->name),array->size);
      }
  };

  class LazySymbolTranslator {
    private:
      NameMangler mangle;
    protected:
      typedef std::map<klee::Array const*,klee::Array*> TxMap;
      TxMap txMap;
    public:
      LazySymbolTranslator(NameMangler mangle)
        : mangle(mangle) {
      }
      klee::Array* operator()(klee::Array const* array) {
        klee::Array*& it = txMap[array];
        if (!it)
          it = mangle(array);
        return it;
      }
  };

  class ReplaceReadVisitor : public klee::ExprVisitor {
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

  class ReadTransformator {
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
      ReadTransformator(klee::ExecutionState& scope, std::string const appendToName,
                        Container const& input, UnaryOperation const& op)
        : lst(NameMangler(scope, appendToName))
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


  template <typename BGraph, typename Key> class ExtractReadEdgesVisitor : public klee::ExprVisitor {
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

  class ConstraintsGraph {
    private:
      typedef klee::ConstraintManager::constraints_ty Vec;
      typedef bg::Graph<bg::Props<Vec::size_type,klee::Array const*,net::util::ContinuousDictionary,net::util::UncontinuousDictionary> > BGraph;
      BGraph bGraph;
      void parse() { // TODO?
      }
    public:
      ConstraintsGraph(klee::ConstraintManager& cs) {
        typedef net::util::ExtractContainerKeys<Vec> Constrs;
        Constrs constrs(cs.begin(),cs.end(),cs.size());
        bGraph.addNodes(constrs);
        //dump();
        for (Vec::const_iterator it = cs.begin(), end = cs.end(), begin = cs.begin(); it != end; ++it) {
          klee::ExprPPrinter::printSingleExpr(std::cout,*it); std::cout << std::endl;
          ExtractReadEdgesVisitor<BGraph,Vec::size_type>(bGraph,it - begin).visit(*it);
        }
        //dump();
      }
      //void dump() __attribute__ ((deprecated)) {
      //  {
      //    std::cout << "Constraint slots: ";
      //    BGraph::Keys<BGraph::Node1>::Type k = bGraph.keys<BGraph::Node1>();
      //    for (BGraph::Keys<BGraph::Node1>::Type::const_iterator it = k.begin(), en = k.end(); it != en; ++it) {
      //      std::cout << "#" << *it << ":" << bGraph.getDegree(*it) << "" << " ";
      //    }
      //    std::cout << std::endl;
      //  }
      //  {
      //    std::cout << "Array slots: " << std::flush;
      //    BGraph::Keys<BGraph::Node2>::Type k = bGraph.keys<BGraph::Node2>();
      //    for (BGraph::Keys<BGraph::Node2>::Type::const_iterator it = k.begin(), en = k.end(); it != en; ++it) {
      //      std::cout << "#" << *it << ":" << bGraph.getDegree(*it) << "" << " " << std::flush;
      //    }
      //    std::cout << std::endl;
      //  }
      //}
  };
}

using namespace kleenet;

namespace klee {
  class ObjectState;
  class Expr;
}

void TransmitHandler::handleTransmission(PacketInfo const& pi, net::BasicState* basicSender, net::BasicState* basicReceiver, std::vector<net::DataAtomHolder> const& data) const {
  klee::ExecutionState& sender = static_cast<klee::ExecutionState&>(*basicSender);
  klee::ExecutionState& receiver = static_cast<klee::ExecutionState&>(*basicReceiver);
  // memcpy
  const klee::ObjectState* ose = receiver.addressSpace.findObject(pi.destMo);
  assert(ose && "Destination ObjectState not found.");
  klee::ObjectState* wos = receiver.addressSpace.getWriteable(pi.destMo, ose);
  ReadTransformator rt(receiver,"{" + llvm::utostr(pi.src.id) + "->" + llvm::utostr(pi.dest.id) + "}",data,dataAtomToExpr);
  bool symbolics = false;
  // important remark: data might be longer or shorter than pi.length. Always obey the size dictated by PacketInfo.
  for (unsigned i = 0; i < pi.length; i++) {
    klee::ref<klee::Expr> const w = rt[i];
    wos->write(pi.offset + i, w);
    symbolics = symbolics || !llvm::isa<klee::ConstantExpr>(w);
  }
  if (symbolics) {
    // copy over the constraints (TODO: only copy "required" constraints subset)
    klee::ExprPPrinter::printConstraints(std::cout,sender.constraints); std::cout << std::endl;
    ConstraintsGraph cg(sender.constraints);
    // TODO: start the search with the ReadExpressions that are used in the transmission

    klee::ConstraintManager& sc(sender.constraints);
    klee::ConstraintManager& rc(receiver.constraints);
    for (klee::ConstraintManager::const_iterator it = sc.begin(), en = sc.end(); it != en; ++it) {
      rc.addConstraint(rt(*it));
    }
  }
}
