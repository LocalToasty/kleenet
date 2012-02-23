#include "net/util/SharedPtr.h"

#include <assert.h>

namespace net {
  namespace util {
    SharedPtrBase::SharedPtrBase()
      : left(this), right(this), owned(true) {
    }
    SharedPtrBase::SharedPtrBase(SharedPtrBase const& from)
      : left(this), right(this), owned(true) {
      join(&from);
    }
    SharedPtrBase::~SharedPtrBase() {
      leave();
    }
    // Note: leaving is idempotent.
    // Try as much as you like, you will never leave yourself.
    void SharedPtrBase::leave() const {
      left->owned = left->owned && owned; // This is where the token traverses the ring!
      left->right = right;
      right->left = left;
      left = right = this;
      owned = false;
    }
    void SharedPtrBase::join(SharedPtrBase const* where) const {
      assert(where);
      leave();
      assert(where->left && where->right);
      left = where->left;
      left->right = this;
      right = where;
      right->left = this;
      owned = true;
    }
    void SharedPtrBase::release() {
      owned = false;
    }
    void SharedPtrBase::assertWrapper(bool condition) const {
      assert(condition);
    }
    bool SharedPtrBase::shouldDelete() const {
      return (left == this) && owned;
    }
  }
}
