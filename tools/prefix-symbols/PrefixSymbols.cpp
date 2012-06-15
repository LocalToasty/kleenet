
#include "PrefixSymbols.h"

#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"

#define NELEMS(array) (sizeof(array)/sizeof(array[0]))

using namespace llvm;

namespace prefix_symbols {

  cl::opt<std::string>
    Prefix("prefix",
        cl::desc("Prefix to be appended to all symbols."),
        cl::init(""));

  cl::opt<std::string>
    Ignore("ignore",
        cl::desc("Comma-separated list of symbols to ignore."),
        cl::init(""));

  // XXX: symbols we don't rename
  static const char *dontRename[] = {
    "puts",
    "_printf$LDBL128",
    "setvbuf",
    "llvm.memcpy.i32",
    "strlen",
    "strncmp",
    "gettimeofday",
    "lvm.bswap.i16",
    "__stdoutp",
  };

  bool PrefixSymbols::doInitialization(Module &M) {
    currModule = &M;

    ignoreList.insert(dontRename, dontRename+NELEMS(dontRename));
    std::stringstream ss(Ignore);
    std::string s;
    while (getline(ss, s, ',')) {
      ignoreList.insert(s);
    }

    // rename globals
    for (Module::global_iterator it = currModule->global_begin(),
        ie = currModule->global_end(); it != ie; ++it) {
      std::string str = it->getNameStr();
      if (!ignoreList.count(str)) {
        it->setName(Prefix + str);
      }
    }
    return true;
  }

  // rename functions
  bool PrefixSymbols::runOnFunction(Function &F) {
    std::string str = F.getNameStr();
    if (!ignoreList.count(str)) {
      F.setName(Prefix + str);
    }
    return false;
  }

  bool PrefixSymbols::doFinalization(Module &M) {
    return false;
  }

  PrefixSymbols::ID_t PrefixSymbols::ID = 0;
  RegisterPass<PrefixSymbols> X("llvm-prefix-symbols", "LLVM prefix symbols");
}
