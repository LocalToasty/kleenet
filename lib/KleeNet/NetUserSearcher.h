#pragma once

#include <memory>
#include "net/util/SharedPtr.h"
#include "net/ClusterSearcherStrategy.h"

namespace net {
  class SearcherStrategy;
}

namespace kleenet {
  namespace searcherautorun {
    struct AF {
      virtual ~AF() {}
    };
    class SearcherAutoRun {
      private:
        typedef std::auto_ptr<AF> F;
        net::util::SharedPtr<net::SearcherStrategy> baseStrategy;
        net::util::SharedPtr<net::SearcherStrategy> strategyAdapter;
        F lockStep, cooja, clusterLockStep, clusterCooja;
        SearcherAutoRun();
      public:
        static void linkme(); // stupid stupid linker!
    };
  }
}
