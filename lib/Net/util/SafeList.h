#pragma once

#include <cassert>
#include <stdlib.h>

#include "net/util/SharedPtr.h"

namespace net {
  namespace util {
    // forward declarations
    template <class T> class SafeList;
    template <class T> class SafeListMonad;
    template <class T> class SafeListIterator;
    template <class T> class SafeListHeadItem;

    template <class T> class SafeListItem {
      // SafeList not supported for non-pointer types!
    };

    template <class T_> class SafeListItem<T_*> {
      typedef T_* T;
      friend class SafeList<T>;
      friend class SafeListIterator<T>;
      private:
        SafeListItem* left;
        SafeListItem* right;
        SafeListHeadItem<T>* head;
        struct Arena {
          SafeListItem* list;
          Arena() : list(NULL) {}
          void cleanup() {
            while (list) {
              SafeListItem* const i = list->right;
              delete list;
              list = (list != i)?i:NULL;
            }
          }
          ~Arena() {
            cleanup();
          }
        };
        static Arena arena;
        SafeListItem(T c, SafeListItem *l, SafeListItem *r)
          : left(l), right(r), head(l->head), content(c) { // O(1)
          // We are implementing a ring-list, so there must always be a right
          // and a left neighbour.
          assert(left && "left must not be NULL!");
          assert(right && "right must not be NULL!");
          assert(left->head == right->head && "Cannot join two lists.");
        }
      protected:
        SafeListItem(SafeListHeadItem<T>* head)
          : left(this), right(this), head(head), content() {} // O(1)
        ~SafeListItem() {}
      public:
        // Suggest pointer or base type (not tested for non-pod).
        T content;
        bool check() const { // O(1)
          return ((right->left == this) && (left->right == this));
        }
        // If 'this' is not a sentinel element this will work fine, otherwise
        // you will get 'true' for an empty list (only containing the sentinel)
        bool isSingleton() const { // O(1)
          return left == right;
        }
        static SafeListItem* allocate(T c, SafeListItem* l, SafeListItem* r) {
          if (arena.list == 0)
            return new SafeListItem(c,l,r);
          SafeListItem* result = arena.list;
          if (arena.list == arena.list->right) {
            arena.list = 0;
          } else {
            arena.list->left->right = arena.list->right;
            arena.list->right->left = arena.list->left;
          }
          result->left = l;
          result->right = r;
          result->content = c;
          return result;
        }
        static void reclaimAll(SafeListItem* i) {
          assert(i && i->left && i->right && "Cannot reclaim nonsense");
          if (arena.list == 0) {
            arena.list = i;
          } else {
            arena.list->left->right = i;
            i->left->right = arena.list;
            SafeListItem* const left = arena.list->left;
            arena.list->left = i->left;
            i->left = left;
          }
        }
    };
    template <class T_> typename SafeListItem<T_*>::Arena SafeListItem<T_*>::arena;

    template <class T> class SafeListHeadItem : SafeListItem<T> {
      friend class SafeList<T>;
      friend class SafeListMonad<T>;
      friend class SafeListIterator<T>;
      private:
        SafeList<T> const* const list;
      public:
        SafeListHeadItem(SafeList<T> const* list) : SafeListItem<T>(this), list(list) {
        }
    };

    template <class T> class SafeList {
      friend class SafeListIterator<T>;
      friend class SafeListMonad<T>;
      // This is a sentinelled ring-list, to use if you really don't care about
      // the ordering. Also, don't expect set-behaviour.
      private:
        SafeListHeadItem<T> head; // head is always the sentinel!
        size_t _size;
        // Locks will be optimised because it is private and never set in this class.
        // We expect this not to exceed 1, so a char will suffice.
        mutable unsigned char locks;
        unsigned char getLock() const { // O(1)
          locks++;
          // Nobody would willingly get 256 locks ... right?
          assert(locks && "Lock limit reached (overflow detected).");
          return locks;
        }
        unsigned char releaseLock() const { // O(1)
          assert(locks && "Attempt to unlock a non-locked list.");
          return --locks;
        }
        SafeList(SafeList const& takeover) __attribute__ ((deprecated)) : head(this), _size(0), locks(0) { // O(1)
          assert(0 && "Calling COPY-CTOR of SafeList is not supported.");
        }
      public:
        typedef SafeListIterator<T> iterator;
        SafeList() : head(this), _size(0), locks(0) { // O(1)
          assert(head.check());
          assert(head.list == this);
        }
        size_t size() const {
          return _size;
        }
        ~SafeList() { // O(1)
          assert((!locks) && "Attempt to destroy a locked list.");
          // The list is empty iff head is the only element in the list.
          assert(&head == head.left && &head == head.right && "Attempt to destroy a non-empty list.");
        }
        unsigned char isLocked() const { // O(1)
          return locks;
        }
        SafeListItem<T> *put(T c) { // O(1)
          assert((!locks) && "Attempt to modify locked list by insertion.");
          assert(head.right && "Invalid FL: right is NULL");
          _size++;
          // We insert the element between 'head' and 'head.right'.
          head.right = (head.right->left = (SafeListItem<T>::allocate(c, &head, head.right)));
          assert(head.check() && head.right->check() &&
            "SafeList::put produced an inconsistent result. I cannot recover, sorry.");
          return head.right;
        }
        void drop(SafeListItem<T> *i) { // O(1)
          assert(i);
          assert(i->head == &head && "Attempt to delete foreign item.");
          // This is essentially impossible outside the list, but better safe than sorry.
          assert(i != i->head && "Attempt to delete sentinel element.");
          assert(!locks && "Attempt to modify locked list by deletion.");
          _size -= i != i->right;
          i->left->right = i->right;
          i->right->left = i->left;
          i->left = i;
          i->right = i;
          SafeListItem<T>::reclaimAll(i);
        }
        void dropAll() { // O(1)
          assert((!locks) && "Attempt to clear locked list.");
          if (_size) {
            _size = 0;
            head.left->right = head.right;
            head.right->left = head.left;
            SafeListItem<T>::reclaimAll(head.right);
            head.left = head.right = &head;
          }
        }
    };
    template <class T> class SharedSafeList {
      friend class SafeListIterator<T>;
      private:
        SharedPtr<SafeList<T> > safeList;
      public:
        typedef typename SafeList<T>::iterator iterator;
        SharedSafeList() : safeList(new SafeList<T>()) {
        }
        // DEFAULT COPY CTOR!!! That's what we do that whole mumbo jumbo for, after all!
        // DEFAULT DTOR
        size_t size() const {
          return safeList->size();
        }
        unsigned char isLocked() const { // O(1)
          return safeList->isLocked();
        }
        SafeListItem<T>* put(T c) { // O(1)
          return safeList->put(c);
        }
        void drop(SafeListItem<T>* i) { // O(1)
          safeList->drop(i);
        }
        void dropAll() { // O(1)
          safeList->dropAll();
        }
    };
    template <class T> class SafeListIterator {
      private:
        SafeListHeadItem<T> const* head;
        SafeListItem<T>* move;
      public:
        // use this at your own risk! iterating WILL THROW A SEGFAULT!
        // more() will work, however
        // the reason for this constructor is to delay the assignment (call 'reassign' before using the object)
        SafeListIterator() : head(NULL), move(NULL) {}
        ~SafeListIterator() {
          unassign();
        }
        // for(SafeListIterator it(sl); it.more(); it.next()) {dostuff(it.get());}
        // call 'get' when there are no more elements and get bogus!
        SafeListIterator(SafeList<T> const* sl) : head(NULL), move(NULL) {
          reassign(sl);
        }
        SafeListIterator(SafeList<T> const& sl) : head(NULL), move(NULL) {
          reassign(sl);
        }
        SafeListIterator(SharedSafeList<T> const* sl) : head(NULL), move(NULL) {
          reassign(sl);
        }
        SafeListIterator(SharedSafeList<T> const& sl) : head(NULL), move(NULL) {
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
        void reassign(SafeList<T> const& sl) {
          unassign();
          // we are friends with SafeList<T>
          head = &(sl.head);
          sl.getLock();
          restart();
        }
        void reassign(SafeList<T> const* sl) {
          reassign(*sl);
        }
        void reassign(SharedSafeList<T> const& sl) {
          reassign(*(sl.safeList));
        }
        void reassign(SharedSafeList<T> const* sl) {
          reassign(*sl);
        }
        void restart() {
          move = head->right;
        }
        bool more() const {
          return move != head;
        }
        void next() {
          move = move->right;
        }
        T get() const {
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

    template <class Adopter, class Adoptable> class SafeListAdopter; // forward decl

    template <class Adopter, class Adoptable> class SafeListAdoptable {
      // Note: we cannot just put Adopter as a friend class, because we
      // do not want derived classes of SafeListAdoptable to be friends with us
      // (friendship is not inherited!).
      friend class SafeListAdopter<Adopter, Adoptable>;
      private:
        SafeListItem<Adoptable*> *sli;
        Adopter *adopter;
      protected:
        SafeListAdoptable() : sli(NULL), adopter(NULL) {} // create as orphan
        Adopter *getAdopter() const {
          return adopter;
        }
    };
    template <class Adopter, class Adoptable> class SafeListAdopter {
      private:
        SafeList<Adoptable*> children;
        Adopter* const self;
      protected:
        void iterateChildren(SafeListIterator<Adoptable*> &iterator) const {
          iterator.reassign(children);
        }
        // Note: due to multiple inheritance, 'self' may not equal 'this'
        SafeListAdopter(Adopter *_self) : self(_self) {}
      public:
        void adoptOrphan(Adoptable *orphan) {
          assert(orphan && "Invalid child.");
          SafeListAdoptable<Adopter, Adoptable> *orph =
            dynamic_cast<SafeListAdoptable<Adopter, Adoptable>*>(orphan);
          assert(orph && "Child of wrong type.");
          assert(!(orph->adopter) && "Child is not an orphan. Cannot adopt.");
          assert(!(orph->fli) && "Orphan is part of a family. Inconsistent.");
          orph->sli = children.put(orphan);
          orph->adopter = self;
          assert(orph->adopter);
        }
        void abandonChild(Adoptable *child) {
          assert(child && "Invalid child.");
          SafeListAdoptable<Adopter, Adoptable> *ch =
            dynamic_cast<SafeListAdoptable<Adopter, Adoptable>*>(child);
          assert(ch && "Child of wrong type.");
          assert(ch->adopter == self && "Cannot abandon foreign child.");
          assert(ch->sli && "Orphan is not part of a family. Inconsistent.");
          children.drop(ch->sli);
          ch->adopter = NULL;
          ch->sli = NULL;
        }
    };
  }
}

