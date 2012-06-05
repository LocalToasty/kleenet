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

    // This is a very thin object. It is perfectly valid to use it as temporary,
    // for instance wrapping an action Container when instantiating LoopConstIterator!
    // LoopConstIterator<...>(ExtractContainerKeys<...>(myContainer)) is valid.
    // This is a special case, because the actual ECK object will die when the expression
    // is over, BUT ITS ITERATORS REMAIN VALID!
    // You may want to use the extractContainerKeys free function instead of the ctor.
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
    template <typename Container>
    ExtractContainerKeys<Container> extractContainerKeys(Container const& container) {
      return ExtractContainerKeys<Container>(container);
    }

    template <typename Container>
    class LoopConstIterator {
      public:
        typedef typename Container::const_iterator const_iterator;
        typedef typename const_iterator::reference reference;
        typedef typename const_iterator::pointer pointer;
      private:
        // beginIt and endIt are logically const, but we want to be default assignable
        const_iterator beginIt;
        const_iterator endIt;
        const_iterator now;
        size_t distance;
      public:
        void reset() {
          now = beginIt;
          distance = 0;
        }
        LoopConstIterator(Container const& c)
          : beginIt(c.begin())
          , endIt(c.end()) {
          reset();
        }
        const_iterator begin() const {
          return beginIt;
        }
        const_iterator end() const {
          return endIt;
        }
        bool more() const {
          return now != endIt;
        }
        void next() {
          ++now;
          ++distance;
        }
        size_t index() const {
          return distance;
        }
        reference operator*() const {
          assert(more());
          return *now;
        }
        pointer operator->() const {
          assert(more());
          return now.operator->();
        }
    };


    enum DictionaryType {
      UncontinuousDictionary,
      ContinuousDictionary
    };

    template <typename Key, typename Value, DictionaryType dictionaryType> class Dictionary;
    template <typename Key, typename Value> class Dictionary<Key,Value,ContinuousDictionary> {// i.e. vector
      private:
        typedef std::vector<Value> Values;
        Values values;
      public:
        typedef typename Values::size_type size_type;
        typedef typename Values::size_type index_type;
        typedef Key key_type;
        typedef typename Values::value_type value_type; // coincides with Value
        void reserve(size_type const sz) {
          values.reserve(sz);
        }
        size_type size() const {
          return values.size();
        }
        value_type& operator[](key_type k) {
          if (k >= values.size())
            values.resize(k+1);
          return values[k];
        }
        index_type getIndex(key_type k) {
          if (static_cast<key_type>(k) >= values.size())
            values.resize(k+1);
          return k;
        }
        Key getKey(index_type i) const {
          assert(i < values.size());
          return i;
        }
        value_type const& find(key_type k) const {
          assert(k < values.size());
          return values[k];
        }
        value_type& find(key_type k) {
          assert(k < values.size());
          return values[k];
        }
        Value const& findByIndex(size_type i) const {
          assert(i < values.size());
          return values[i];
        }
        Value& findByIndex(size_type i) {
          assert(i < values.size());
          return values[i];
        }
    };
    template <typename Key, typename Value> class Dictionary<Key,Value,UncontinuousDictionary> : public Dictionary<size_t,typename std::pair<Key,Value>,ContinuousDictionary> {
      private:
        typedef Dictionary<size_t, std::pair<Key,Value>, ContinuousDictionary> Parent;
      public:
        typedef typename Parent::size_type size_type;
        typedef typename Parent::index_type index_type;
        typedef Key key_type;
        typedef typename Parent::value_type::second_type value_type;
      private:
        typedef std::map<key_type,size_type> Keys;
        Keys keys;
        size_type findValueKey(key_type k) const {
          typename Keys::const_iterator const it = keys.find(k);
          assert(it != keys.end());
          return it->second;
        }
      public:
        Value& operator[](key_type k) {
          std::pair<typename Keys::iterator,bool> found = keys.insert(std::make_pair(k,Parent::size()));
          return Parent::operator[](found.first->second).second;
        }
        index_type getIndex(key_type k) {
          std::pair<typename Keys::iterator,bool> found = keys.insert(std::make_pair(k,Parent::size()));
          return Parent::getIndex(found.first->second);
        }
        Key getKey(index_type i) const {
          return Parent::find(i).first;
        }
        Value const& find(key_type k) const {
          return Parent::find(findValueKey(k)).second;
        }
        Value& find(key_type k) {
          return Parent::find(findValueKey(k)).second;
        }
    };

  }
}
