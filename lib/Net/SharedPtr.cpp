#include "net/util/SharedPtr.h"

#include <assert.h>

namespace net {
  namespace util {
    SharedPtrBase::SharedPtrBase()
      : left(this), right(this) {
    }
    SharedPtrBase::SharedPtrBase(SharedPtrBase const& from)
      : left(this), right(this) {
      join(&from);
    }
    SharedPtrBase::~SharedPtrBase() {
      leave();
    }
    // Note: leaving is idempotent.
    // Try as much as you like, you will never leave yourself.
    void SharedPtrBase::leave() const {
      left->right = right;
      right->left = left;
    }
    void SharedPtrBase::join(SharedPtrBase const* where) const {
      assert(where);
      leave();
      assert(where->left && where->right);
      left = where->left;
      left->right = this;
      right = where;
      right->left = this;
    }
    void SharedPtrBase::assertWrapper(bool condition) const {
      assert(condition);
    }
    bool SharedPtrBase::isSingleton() const {
      return left == this;
    }
  }
}
