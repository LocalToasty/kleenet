#pragma once

#include <assert.h>

#include "util/SharedPtr.h"
#include "util/Type.h"

namespace net {
  template <typename T> class ConstIteratorHolder;

  template <typename T> class ConstIteratable {
    template <typename U> friend class ConstIteratorHolder;
    private:
      virtual util::SharedPtr<ConstIteratable> dup() const = 0;
      virtual ConstIteratorHolder<T> const* isHolder() const {
        return 0; // double dispatch, in a sense
      }
    public:
      typedef typename util::RemoveReference<T>::Type Content;
      virtual Content operator*() const = 0;
      virtual Content operator->() const {
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
      util::SharedPtr<ConstIteratable<T> > it;
      util::SharedPtr<ConstIteratable<T> > dup() const {
        return util::SharedPtr<ConstIteratable<T> >(new ConstIteratorHolder(*this));
      }
      ConstIteratorHolder const* isHolder() const {
        return this;
      }
    public:
      ConstIteratorHolder(ConstIteratable<T> const& iter) : it(iter.dup()) {}
      ConstIteratorHolder(ConstIteratorHolder const& from) : it(from.it) {}
      typename ConstIteratable<T>::Content operator*() const {
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
      util::SharedPtr<ConstIteratable<T> > dup() const {
        return util::SharedPtr<ConstIteratable<T> >(new SingletonIterator(*this));
      }
    public:
      SingletonIterator(T const* subject) : subject(subject) {}
      // dereferencing a default-ctor'd iterator results in undefined behaviour
      SingletonIterator() : subject(0) {}
      SingletonIterator(SingletonIterator const& from) : subject(from.subject) {}
      typename ConstIteratable<T>::Content operator*() const {
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
      util::SharedPtr<ConstIteratable<T> > dup() const {
        return util::SharedPtr<ConstIteratable<T> >(new StdConstIterator(*this));
      }
    public:
      StdConstIterator(typename Container::const_iterator it) : it(it) {
      }
      StdConstIterator(StdConstIterator const& from) : it(from.it) {
      }
      typename ConstIteratable<T>::Content operator*() const {
        return *it;
      }
      ConstIteratable<T>& operator++() {
        ++it;
        return *this;
      }
      bool operator==(ConstIteratable<T> const& with) const {
        return static_cast<StdConstIterator const&>(with).it == it;
      }
  };

  template <typename T, typename Container, typename Transform> class StdConstTransformIterator : public ConstIteratable<T> {
    private:
      typename Container::const_iterator it;
      Transform transform;
      util::SharedPtr<ConstIteratable<T> > dup() const {
        return util::SharedPtr<ConstIteratable<T> >(new StdConstTransformIterator(*this));
      }
    public:
      StdConstTransformIterator(typename Container::const_iterator it, Transform transform) : ConstIteratable<T>(), it(it), transform(transform) {
      }
      StdConstTransformIterator(StdConstTransformIterator const& from) : ConstIteratable<T>(from), it(from.it), transform(from.transform) {
      }
      typename ConstIteratable<T>::Content operator*() const {
        return transform(*it);
      }
      ConstIteratable<T>& operator++() {
        ++it;
        return *this;
      }
      bool operator==(ConstIteratable<T> const& with) const {
        return static_cast<StdConstTransformIterator const&>(with).it == it;
      }
  };

  template <typename T>
  struct StdIteratorFactory {
    template <typename It>
    struct IteratorPair {
      It begin;
      It end;
      IteratorPair(It begin, It end) : begin(begin), end(end) {}
    };
    template <typename Container>
    static IteratorPair<StdConstIterator<T,Container> > build(Container const& container) {
      return IteratorPair<StdConstIterator<T,Container> >(
          StdConstIterator<T,Container>(container.begin())
        , StdConstIterator<T,Container>(container.end())
      );
    }
    template <typename Container, typename Transform>
    static IteratorPair<StdConstTransformIterator<T,Container,Transform> > build(Container const& container, Transform transform) {
      return IteratorPair<StdConstTransformIterator<T,Container,Transform> >(
          StdConstTransformIterator<T,Container,Transform>(container.begin(),transform)
        , StdConstTransformIterator<T,Container,Transform>(container.end(),transform)
      );
    }
  };
}

