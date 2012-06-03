#pragma once

#include "net/util/Type.h"

namespace net {
  namespace util {

    template <typename Container> struct HasKeyType : sfinae_test {
      template <typename T> static Yes test(typename T::key_type*);
      template <typename T> static No test(...);
      enum {value = (sizeof(test<Container>(0)) == sizeof(Yes))};
    };
    template <typename Container> struct HasSizeType : sfinae_test {
      template <typename T> static Yes test(typename T::size_type*);
      template <typename T> static No test(...);
      enum {value = (sizeof(test<Container>(0)) == sizeof(Yes))};
    };

    template <typename Container, typename Enabled = void> struct KeyType {
    };
    template <typename Container> struct KeyType<Container
    , typename select_if<
        HasKeyType<Container>::value
      , void, char[1]>::Type
    > {
      typedef typename Container::key_type Type;
    };
    template <typename Container> struct KeyType<Container
    , typename select_if<
        !HasKeyType<Container>::value && HasSizeType<Container>::value
      , void, char[2]>::Type
    > {
      typedef typename Container::size_type Type;
    };

    template <typename Container, typename Enabled = void> struct SizeType {
      typedef size_t size_type;
    };
    template <typename Container> struct SizeType<Container
    , typename select_if<
        HasSizeType<Container>::value
      , void, char[1]>::Type
    > {
      typedef typename Container::size_type Type;
    };

    namespace extract_container_keys {
      template <typename Container, typename Enable = void> struct const_iterator {
        typedef typename Container::const_iterator parent_iterator;
        typedef typename SizeType<Container>::Type size_type;
        size_type pi;
        const_iterator(size_type const pi, parent_iterator const) : pi(pi) {}
        size_type const& operator*() const {
          return pi;
        }
        const_iterator& operator++() {
          ++pi;
          return *this;
        }
        const_iterator operator++(int) {
          return const_iterator(pi++);
        }
        bool operator==(const_iterator const& other) const {
          return pi == other.pi;
        }
        bool operator!=(const_iterator const& other) const {
          return pi != other.pi;
        }
      };
      template <typename Container> struct const_iterator<Container,typename enable_if<HasKeyType<Container>::value>::Type> {
        typedef typename Container::const_iterator parent_iterator;
        typedef typename Container::key_type value_type;
        typedef typename SizeType<Container>::Type size_type;
        parent_iterator pi;
        const_iterator(size_type const, parent_iterator const pi) : pi(pi) {}
        value_type const& operator*() const {
          return pi->first;
        }
        value_type const* operator->() const {
          return &(pi->first);
        }
        const_iterator& operator++() {
          ++pi;
          return *this;
        }
        const_iterator operator++(int) {
          return const_iterator(pi++);
        }
        bool operator==(const_iterator const& other) const {
          return pi == other.pi;
        }
        bool operator!=(const_iterator const& other) const {
          return pi != other.pi;
        }
      };
    }

    template <typename Container> struct ExtractContainerKeys {
      public:
        typedef typename KeyType<Container>::Type value_type;
        typedef typename SizeType<Container>::Type size_type;
        typedef extract_container_keys::const_iterator<Container> const_iterator;
      private:
        typename Container::const_iterator b;
        typename Container::const_iterator e;
        size_type const s;
      public:
        explicit ExtractContainerKeys(Container const& extractFrom)
          : b(extractFrom.begin())
          , e(extractFrom.end())
          , s(extractFrom.size()) {
        }
        ExtractContainerKeys(typename Container::const_iterator b, typename Container::const_iterator e, size_type s)
          : b(b)
          , e(e)
          , s(s) {
        }
        size_type size() const {
          return s;
        }
        const_iterator begin() const {
          return const_iterator(0,b);
        }
        const_iterator end() const {
          return const_iterator(s,e);
        }
    };

  }
}
