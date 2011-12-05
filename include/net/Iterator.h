#pragma once

#include <assert.h>

namespace net {
  template <typename T> class ConstIteratorHolder;

  template <typename T> class ConstIteratable {
    template <typename U> friend class ConstIteratorHolder;
    private:
      virtual ConstIteratable* dup() const = 0;
      virtual void reclaim() = 0;
      virtual ConstIteratorHolder<T> const* isHolder() const {
        return 0; // double dispatch, in a sense
      }
    public:
      virtual T const& operator*() const = 0;
      virtual T const& operator->() const {
        return **this;
      }
      virtual ConstIteratable& operator++() = 0;
      virtual bool operator==(ConstIteratable const&) const = 0;
      virtual bool operator!=(ConstIteratable const& with) const {
        return !(*this == with);
      }
  };

  template <typename T> class ConstIteratorHolder : public ConstIteratable<T> {
    private:
      ConstIteratable<T>* it;
      ConstIteratable<T>* dup() const {
        return new ConstIteratorHolder(*this);
      }
      void reclaim() {
        delete this;
      }
      ConstIteratorHolder const* isHolder() const {
        return this;
      }
    public:
      ConstIteratorHolder(ConstIteratable<T> const& iter) : it(iter.dup()) {}
      ConstIteratorHolder(ConstIteratorHolder const& from) : it(from.it->dup()) {}
      T const& operator*() const {
        return **it;
      }
      ConstIteratable<T>& operator++() {
        ++*it;
        return *this;
      }
      bool operator==(ConstIteratable<T> const& with) const {
        if (with.isHolder())
          return *(with.isHolder()->it) == *it;
        return with == *it; // good luck :)
      }
  };


  ////  IMPLEMENTATIONS  //////////////////////////////////////////////////////

  template <typename T> class SingletonIterator : public ConstIteratable<T> {
    private:
      T const* subject;
      SingletonIterator<T>* dup() const {
        return new SingletonIterator(*this);
      }
      void reclaim() {
        delete this;
      }
    public:
      SingletonIterator(T const* subject) : subject(subject) {}
      // dereferencing a default-ctor'd iterator results in undefined behaviour
      SingletonIterator() : subject(0) {}
      SingletonIterator(SingletonIterator const& from) : subject(from.subject) {}
      T const& operator*() const {
        assert(subject);
        return *subject;
      }
      ConstIteratable<T>& operator++() {
        subject = 0;
        return *this;
      }
      bool operator==(ConstIteratable<T> const& with) const {
        return static_cast<SingletonIterator const&>(with).subject == subject;
      }
  };

  template <typename T, typename Container> class StdConstIterator : public ConstIteratable<T> {
    private:
      typename Container::const_iterator it;
      ConstIteratable<T>* dup() const {
        return new StdConstIterator(*this);
      }
      void reclaim() {
        delete this;
      }
    public:
      StdConstIterator(typename Container::const_iterator it) : it(it) {}
      StdConstIterator(StdConstIterator const& from) : it(from.it) {}
      T const& operator*() const {
        // Sadly, without i, the compiler doesn't understand what we are doing!
        T const& i(*it);
        return i;
      }
      ConstIteratable<T>& operator++() {
        ++it;
        return *this;
      }
      bool operator==(ConstIteratable<T> const& with) const {
        return static_cast<StdConstIterator const&>(with).it == it;
      }
  };
}

