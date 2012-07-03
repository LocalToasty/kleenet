#pragma once

namespace kleenet {
  class BaseArray {
    protected:
      // KleeNet patch: Array is extended in the kleenet:: module to efficiently handle transmissions,
      // and whoever cares to delete this, will do this via an klee::Array* handle. So we need the dtor to be virtual.
      virtual ~BaseArray() {}
    public:
      virtual bool isBaseArray() const {return true;}
  };
}
