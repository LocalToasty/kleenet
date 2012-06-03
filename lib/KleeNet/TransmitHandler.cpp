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
      virtual Action visitRead(klee::ReadExpr const& re) {
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
  // important remark: data might be longer or shorter than pi.length. Always obey the size dictated by PacketInfo.
  for (unsigned i = 0; i < pi.length; i++) {
    wos->write(pi.offset + i, rt[i]);
  }
  // copy over the constraints (TODO: only copy "required" constraints subset)
  bg::Graph<bg::Props<unsigned,void*> > g;
  g.addNodes<std::vector<unsigned> >(std::vector<unsigned>());
  //bg::ExtractContainer<std::map<unsigned,float> >::key_type kt;
  std::map<unsigned,float> m;
  std::vector<float> f;
  net::util::ExtractContainerKeys<std::map<unsigned,float> > eck1(m);
  net::util::ExtractContainerKeys<std::vector<float> > eck2(f);
  //bool b = bg::HasKeyType<std::map<int,float> >::value;

  klee::ConstraintManager& sc(sender.constraints);
  klee::ConstraintManager& rc(receiver.constraints);
  for (klee::ConstraintManager::const_iterator it = sc.begin(), en = sc.end(); it != en; ++it) {
    rc.addConstraint(rt(*it));
  }
}
