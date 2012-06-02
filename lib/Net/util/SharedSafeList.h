#pragma once

#include "SafeList.h"
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
    template <class T, bool pool> class SafeListIterator {
      private:
        SafeListHeadItem<T,pool> const* head;
        SafeListItem<T,pool>* move;
        ssize_t remaining;
      public:
        // use this at your own risk! iterating WILL THROW A SEGFAULT!
        // more() will work, however
        // the reason for this constructor is to delay the assignment (call 'reassign' before using the object)
        SafeListIterator() : head(NULL), move(NULL), remaining(0) {}
        ~SafeListIterator() {
          unassign();
        }
        // for(SafeListIterator it(sl); it.more(); it.next()) {dostuff(it.get());}
        // call 'get' when there are no more elements and get bogus!
        SafeListIterator(SafeList<T,pool> const* sl) : head(NULL), move(NULL), remaining(0) {
          reassign(sl);
        }
        SafeListIterator(SafeList<T,pool> const& sl) : head(NULL), move(NULL), remaining(0) {
          reassign(sl);
        }
        SafeListIterator(SharedSafeList<T,pool> const* sl) : head(NULL), move(NULL), remaining(0) {
          reassign(sl);
        }
        SafeListIterator(SharedSafeList<T,pool> const& sl) : head(NULL), move(NULL), remaining(0) {
          reassign(sl);
        }
        void unassign() {
          if (head) {
            // remove the old lock
            head->list->releaseLock();
            head = NULL;
            move = NULL;
          }
        }
        void reassign(SafeList<T,pool> const& sl) {
          unassign();
          // we are friends with SafeList<T>
          head = &(sl.head);
          sl.getLock();
          restart();
        }
        void reassign(SafeList<T,pool> const* sl) {
          reassign(*sl);
        }
        void reassign(SharedSafeList<T,pool> const& sl) {
          reassign(*(sl.safeList));
        }
        void reassign(SharedSafeList<T,pool> const* sl) {
          reassign(*sl);
        }
        void restart() {
          remaining = head->list->size();
          move = head->right;
        }
        bool more() const {
          assert(remaining >= 0 && "SafeList bounds overrun");
          return move != head;
        }
        void next() {
          assert(remaining-- > 0 && "SafeList bounds overrun");
          move = move->right;
        }
        T get() const {
          assert(remaining > 0 && "SafeList bounds overrun");
          return move->content;
        }
        bool empty() const {
          return move == move->right;
        }
        bool singleton() const {
          return singletonOrEmpty() > empty();
        }
        bool singletonOrEmpty() const {
          // sometimes we already know there is stuff in there
          // (besides the sentinel which does not count)
          return move == move->right->right;
        }
    };
  }
}
