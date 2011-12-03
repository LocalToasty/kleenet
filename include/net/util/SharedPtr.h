#pragma once

#include <stddef.h>

// since we cannot use boost nor C++11, nor TR1, we have to reinvent the wheel, sigh
namespace net {
  namespace util {
    class SharedPtrBase {
      private:
        mutable SharedPtrBase const* left;
        mutable SharedPtrBase const* right;
      protected:
        SharedPtrBase();
        SharedPtrBase(SharedPtrBase const& from);
        ~SharedPtrBase();
        void leave() const; // Note: leaving is idempotent
        void join(SharedPtrBase const* where) const;
        void assertWrapper(bool) const;
      public:
        bool isSingleton() const;
    };
    // TODO: make specialisation for something that provides us with a nested refcoutner
    // Eg. we could specialise for children of some 'SharedPtrAble' using SFINAE.

    template <typename T> struct Deallocator {
      private:
        bool doDelete; // not const to be def. assignable
      public:
        // Implicit Constructor on purpose!
        Deallocator(bool doDelete = true) : doDelete(doDelete) {}
        bool willDelete() const {
          return doDelete;
        }
        void operator()(T* t) const {
          if (doDelete)
            delete t;
        }
    };

    template <typename T, typename Delete = Deallocator<T> > class SharedPtr : private SharedPtrBase {
      private:
        Delete del;
        T* ptr;
        void drop() {
          if (ptr && isSingleton()) {
            del(ptr);
            ptr = NULL;
          }
          leave();
        }
      public:
        // SharedPtr(p,false) will not delete the pointer! Useful for Stackobjects!
        explicit SharedPtr(T* ptr = NULL, Delete del = Delete())
          : SharedPtrBase(), del(del), ptr(ptr) {
        }
        SharedPtr(SharedPtr const& from)
          : SharedPtrBase(from), del(from.del), ptr(from.ptr) {
        }
        ~SharedPtr() {
          drop();
        }
        SharedPtr& operator=(SharedPtr const& from) {
          if (&from != this) {
            drop();
            del = from.del;
            ptr = from.ptr;
            join(&from);
          }
          return *this;
        }
        bool operator==(SharedPtr const& with) const {
          return operator==(with.ptr);
        }
        bool operator<(SharedPtr const& with) const {
          return operator<(with.ptr);
        }
        bool operator==(T* with) const {
          return ptr == with;
        }
        bool operator<(T* with) const {
          return ptr < with;
        }
        T& operator*() const {
          return *operator->();
        }
        T* operator->() const {
          assertWrapper(ptr);
          return ptr;
        }
        operator bool() const {
          return ptr;
        }
        // NOTE: if one day needed, realease could be implemented with an anti-token
        // that is included in the ring-list at an arbitrary position and prevents the last
        // SharedPtr from invoking 'del`.
    };
  }
}

