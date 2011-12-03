#pragma once

namespace net {
  namespace util {
    // never instantiate this type yourself!
    template <typename T> struct Type__impl {
      // assuming T is NOT a reference!
      typedef T Rigid;
      typedef T& Ref;
      typedef T const& ConstRef;
    };
    // C++11 template aliases would be the thing to do here ...
    template <typename T> struct Type : Type__impl<T> {
    };
    template <typename T> struct Type<T&> : Type__impl<T> {
    };
  }
}
