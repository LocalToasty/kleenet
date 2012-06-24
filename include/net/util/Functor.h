#pragma once

namespace net {
  namespace util {
    template <typename T>
    struct DynamicFunctor {
      virtual void operator()(T) const = 0;
      virtual ~DynamicFunctor() {}
    };
    template <typename T>
    struct StaticFunctor {
    };

    template <typename T = void, typename F = void, template <typename _T> class FunctorKind = StaticFunctor>
    struct Functor : FunctorKind<T> {
      F f;
      Functor(F f) : f(f) {}
      Functor() : f() {}
      void operator()(T arg) {
        f(arg);
      }
      void operator()(T arg) const {
        f(arg);
      }
    };
    template <>
    struct Functor<void,void> {
      template <typename U>
      void operator()(U) {}
    };
    template <typename T, template <typename _T> class FunctorKind = StaticFunctor>
    struct FunctorBuilder {
      template <typename F>
      static Functor<T,F,FunctorKind> build(F f) {
        return Functor<T,F,FunctorKind>(f);
      }
      private:
        FunctorBuilder(); // I'm shy, don't construct me
    };
  }
}
