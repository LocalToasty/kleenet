#pragma once

#include <cassert>
#include <stdlib.h>

namespace net {
  namespace util {
    // forward declarations
    template <class T> class SafeList;
    template <class T> class SafeListMonad;
    template <class T> class SafeListIterator;
    template <class T> class SafeListHeadItem;

    template <class T> class SafeListItem {
      friend class SafeList<T>;
      friend class SafeListIterator<T>;
      private:
        SafeListItem *left;
        SafeListItem *right;
        SafeListHeadItem<T> * const head;
      protected:
        SafeListItem(SafeListHeadItem<T> *_head)
          : left(this), right(this), head(_head), content() {} // O(1)
      public:
        // Suggest pointer or base type (not tested for non-pod).
        T const content;
        SafeListItem(T c, SafeListItem *l, SafeListItem *r)
          : left(l), right(r), head(l->head), content(c) { // O(1)
          // We are implementing a ring-list, so there must always be a right
          // and a left neighbour.
          assert(left && "left must not be NULL!");
          assert(right && "right must not be NULL!");
          assert(left->head == right->head && "Cannot join two lists.");
        }
        bool check() const { // O(1)
          return ((right->left == this) && (left->right == this));
        }
        // If 'this' is not a sentinel element this will work fine, otherwise
        // you will get 'true' for an empty list (only containing the sentinel)
        bool isSingleton() const { // O(1)
          return left == right;
        }
    };
    template <class T> class SafeListHeadItem : SafeListItem<T> {
      friend class SafeList<T>;
      friend class SafeListMonad<T>;
      friend class SafeListIterator<T>;
      private:
        SafeList<T> *list;
      public:
        SafeListHeadItem(SafeList<T> *l) : SafeListItem<T>(this), list(l) {
        }
    };
    template <class T> class SafeList {
      friend class SafeListIterator<T>;
      friend class SafeListMonad<T>;
      // This is a sentinelled ring-list, to use if you really don't care about
      // the ordering. In order to be efficient, we kindly ask the user to do
      // nothing funny. Also, don't expect set-behaviour.
      private:
        SafeListHeadItem<T> *head; // head is always the sentinel!
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
      public:
        typedef SafeListIterator<T> iterator;
        SafeList() : _size(0), locks(0) { // O(1)
          head = new SafeListHeadItem<T>(this); // sentinel
          assert(head->check());
          assert(head->list == this);
        }
        size_t size() const {
          return _size;
        }
        SafeList(SafeList const& takeover) __attribute__ ((deprecated)) : _size(0), locks(0) { // O(1)
          assert((!(takeover.locks)) && "Attempt of taking over a locked list.");
          // XXX hack to mutate the const object
          SafeList<T>* to = const_cast<SafeList<T>*>(&takeover);
          // pull the old switcheroo with the head member
          head = to->head;
          to->head = new SafeListHeadItem<T>(to);
          head->list = this;
          _size = to->_size;
          to->_size = 0;
          assert(to->head->check());
          assert(head->check());
        }
        ~SafeList() { // O(1)
          assert(head);
          assert((!locks) && "Attempt to destroy a locked list.");
          // The list is empty iff head is the only element in the list.
          assert(head == head->left && head == head->right && "Attempt to destroy a non-empty list.");
          delete head;
        }
        unsigned char isLocked() const { // O(1)
          return locks;
        }
        SafeListItem<T> *put(T c) { // O(1)
          assert((!locks) && "Attempt to modify locked list by insertion.");
          assert(head && "Invalid FL: head is NULL");
          assert(head->right && "Invalid FL: right is NULL");
          _size++;
          // We insert the element between 'head' and 'head->right'.
          head->right = (head->right->left = (new SafeListItem<T>(c, head, head->right)));
          assert(head->check() && head->right->check() &&
            "SafeList::put yielded an inconsistent result. I cannot recover, sorry.");
          return head->right;
        }
        // Watch out!
        static void drop(SafeListItem<T> *i) { // O(1)
          assert(i);
          // This is essentially impossible outside the list, but better safe than sorry.
          assert(i != i->head && "Attempt to delete sentinel element.");
          assert(!(i->head->list->locks) && "Attempt to modify locked list by deletion.");
          i->head->list->_size -= i != i->right;
          i->left->right = i->right;
          i->right->left = i->left;
          delete i;
        }
        void dropOwn(SafeListItem<T> *i) {
          assert(i);
          assert(i->head == head && "Attempt to delete foreign item.");
          drop(i);
        }
        void dropAll() { // O(n*dtor), dtor=Omega(1)
          assert((!locks) && "Attempt to clear locked list.");
          head->left->right = NULL;
          // NULL iff the list is "empty".
          SafeListItem<T> *i = head->right;
          SafeListItem<T> *j = NULL;
          head->left = head->right = head;
          while (i) {
            i = (j = i)->right;
            delete j;
          }
          _size = 0;
        }
    };
    /// The monad class may be used to postpone insert instructions.
    /// For instance while the FL is locked (e.g. by an iterator).
    template <class T> class SafeListMonad {
      private:
        SafeListHeadItem<T> *head;
        SafeListHeadItem<T> surrogate;
        size_t size;
      public:
        SafeListMonad(SafeList<T> *sl) : head(sl->head), surrogate(NULL), size(0) { // O(1)
          sl->getLock();
        }
        SafeListMonad(SafeList<T> &sl) : head(sl.head), surrogate(NULL), size(0) { // O(1)
          sl.getLock();
        }
        SafeList<T> *safeList() const { // O(1)
          return head->list;
        }
        SafeListItem<T> *put(T c) { // O(1)
          size++;
          return surrogate.right = surrogate.right->left =
            (new SafeListItem<T>(c, &surrogate, surrogate.right));
        }
        ~SafeListMonad() { // O(1)
          SafeList<T> *sl = head->list;
          sl->releaseLock();
          assert(!(sl->isLocked()) && "Attempt to merge into a locked list.");
          sl->_size += size;
          surrogate.left->right = head->right;
          head->right->left = surrogate.left;
          assert(head->right->check());
          head->right = surrogate.right;
          surrogate.right->left = head;
          assert(head->check());
          assert(head->left->check());
          assert(head->right->check());
          surrogate.left = surrogate.right = NULL;
        }
    };
    template <class T> class SafeListIterator {
      private:
        SafeListHeadItem<T> *head;
        SafeListItem<T> *move;
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
        SafeListIterator(const SafeList<T> *sl) : head(NULL), move(NULL) {
          reassign(sl);
        }
        SafeListIterator(const SafeList<T> &sl) : head(NULL), move(NULL) {
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
        void reassign(const SafeList<T> *sl) {
          unassign();
          // we are friends with SafeList<T>
          head = sl->head;
          sl->getLock();
          restart();
        }
        void reassign(const SafeList<T> &sl) {
          reassign(&sl);
        }
        void restart() {
          move = head->right;
        }
        bool more() const {
          return move != reinterpret_cast<SafeListItem<T>*>(head);
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
          children.dropOwn(ch->sli);
          ch->adopter = NULL;
          ch->sli = NULL;
        }
    };
  }
}
