#pragma once

#include "net/util/SafeList.h"
#include "net/util/SharedPtr.h"

namespace net {
  namespace util {
    template <class T,bool pool> class SharedSafeList {
      friend class SafeListIterator<T,pool>;
      private:
        SharedPtr<SafeList<T,pool> > safeList;
      public:
        typedef typename SafeList<T,pool>::iterator iterator;
        SharedSafeList() : safeList(new SafeList<T,pool>()) {
        }
        // DEFAULT COPY CTOR!!! That's what we do that whole mumbo jumbo for, after all!
        // DEFAULT DTOR
        size_t size() const { // O(1)
          return safeList->size();
        }
        unsigned char isLocked() const { // O(1)
          return safeList->isLocked();
        }
        SafeListItem<T,pool>* put(T c) { // O(1)
          return safeList->put(c);
        }
        void drop(SafeListItem<T,pool>* i) { // O(1)
          safeList->drop(i);
        }
        void dropAll() { // O(1)
          safeList->dropAll();
        }
    };
  }
}
