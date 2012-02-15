
#include "llvm/Pass.h"

#include <set>
#include <sstream>

namespace prefix_symbols {

  class PrefixSymbols : public llvm::FunctionPass {
  public:
    std::set<std::string> ignoreList;
    static char ID;
    PrefixSymbols() : llvm::FunctionPass((intptr_t)&ID) {};
    virtual bool doInitialization(llvm::Module &M);
    virtual bool runOnFunction(llvm::Function &F);
    virtual bool doFinalization(llvm::Module &M);

  private:
    llvm::Module* currModule;
  };
}
