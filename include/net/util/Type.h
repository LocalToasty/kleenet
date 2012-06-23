#pragma once

namespace net {
  namespace util {
    // never instantiate this type yourself!
    template <typename T> struct Type__impl {
      // assuming T is NOT a reference!
      typedef T Rigid;
      typedef T& Ref;
      typedef T* Ptr;
      typedef T const& ConstRef;
      typedef T const* ConstPtr;
    };
    // C++11 template aliases would be the thing to do here ...
    template <typename T> struct Type : Type__impl<T> {
    };
    template <typename T> struct Type<T&> : Type__impl<T> {
    };

    template <typename T> struct ConstRefUnlessPtr {
      typedef typename Type<T>::ConstRef Type;
    };
    template <typename T> struct ConstRefUnlessPtr<T*> {
      typedef T* Type;
    };


    template <bool condition, typename Enable = void> struct enable_if {
    };
    template <typename Enable> struct enable_if<true,Enable> {
      typedef Enable Type;
    };
    template <bool condition, typename Disable = void> struct disable_if {
    };
    template <typename Disable> struct disable_if<false,Disable> {
      typedef Disable Type;
    };
    template <bool condition, typename TrueType, typename FalseType> struct select_if {};
    template <typename TrueType, typename FalseType> struct select_if<true,TrueType,FalseType> {
      typedef TrueType Type;
    };
    template <typename TrueType, typename FalseType> struct select_if<false,TrueType,FalseType> {
      typedef FalseType Type;
    };

    struct sfinae_test {
      typedef char Yes;
      typedef long No;
    };

    template <typename Query, typename If, typename Then, typename Otherwise> struct TypeSelection {
      typedef Otherwise Type;
    };
    template <typename If, typename Then, typename Otherwise> struct TypeSelection<If,If,Then,Otherwise> {
      typedef Then Type;
    };

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
