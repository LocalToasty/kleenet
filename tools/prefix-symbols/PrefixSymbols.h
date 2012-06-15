#pragma once

#include "klee/Config/Version.h"
#include "llvm/Pass.h"

#include <set>
#include <sstream>

namespace prefix_symbols {

  class PrefixSymbols : public llvm::FunctionPass {
  public:
    std::set<std::string> ignoreList;
#if LLVM_VERSION_CODE <= LLVM_VERSION(2,7)
    typedef intptr_t ID_t;
#else
    typedef char ID_t;
#endif
    static ID_t ID;
    PrefixSymbols() : llvm::FunctionPass(ID) {};
    virtual bool doInitialization(llvm::Module &M);
    virtual bool runOnFunction(llvm::Function &F);
    virtual bool doFinalization(llvm::Module &M);

  private:
    llvm::Module* currModule;
  };
}
