#pragma once

#include <functional>

namespace net {
  namespace util {

    enum LazyBool {
      LB_false = false,
      LB_true = true,
      LB_undefined
    };

    template <typename Cmp, typename B> class Operator {
      public:
        typedef typename Cmp::first_argument_type A;
      private:
        A const& a1;
        A const& a2;
        B const& b;
      public:
        Operator(A const& a1, A const& a2, B const& b)
          : a1(a1), a2(a2), b(b) {
        }
        operator bool() const {
          switch (static_cast<LazyBool>(Cmp(a1,a2))) {
            case LB_false:
              return false;
            case LB_true:
              return true;
            case LB_undefined:
            default: // g++ does not understand that we have all branches :(
              return static_cast<bool>(b);
          }
        }
    };

    template <typename Fn> class BinaryFunctorMonad {
      public:
        typedef typename Fn::first_argument_type first_argument_type;
        typedef typename Fn::second_argument_type second_argument_type;
        typedef typename Fn::result_type result_type;
      private:
        first_argument_type const& a;
        second_argument_type const& b;
      public:
        BinaryFunctorMonad(first_argument_type const& a, second_argument_type const& b)
          : a(a), b(b) {
        }
        operator result_type() {
          return Fn()(a,b);
        }
    };

    enum CmpSymmetry {
      CS_Symmetric = false,
      CS_Asymmetric = true
    };

    template <typename Cmp, CmpSymmetry asymmetric> class LazyCmp {
      public:
        typedef typename Cmp::first_argument_type first_argument_type;
        typedef typename Cmp::first_argument_type second_argument_type;
        typedef LazyBool result_type;
        LazyBool operator()(first_argument_type const& a1, second_argument_type const& a2) const {
          if (Cmp(a1,a2))
            return LB_true;
          if (asymmetric && Cmp(a2,a1))
            return LB_false;
          return LB_undefined;
        }
    };

    template <typename A, typename B> struct MultiLess {
      typedef
        Operator<
          BinaryFunctorMonad<
            LazyCmp<
              BinaryFunctorMonad<std::less<A> >,
              CS_Asymmetric>
          >, B>
        Type;
    };

    template <typename A, typename B> struct MultiEqual {
      typedef
        Operator<
          BinaryFunctorMonad<
            LazyCmp<
              BinaryFunctorMonad<std::equal_to<A> >,
              CS_Symmetric>
          >, B>
        Type;
    };
  }

  template <typename A, typename B> typename multioperator::MultiLess<A,B>::Type multiLess(A const& a1, A const& a2, B const& b) {
    return typename multioperator::MultiLess<A,B>::Type(a1,a2,b);
  }

  template <typename A, typename B> typename multioperator::MultiEqual<A,B>::Type multiEqual(A const& a1, A const& a2, B const& b) {
    return typename multioperator::MultiEqual<A,B>::Type(a1,a2,b);
  }
  // example1: multiLess(a1,a2,multiLess(b1,b2,multiLess(c1,c2,false)))



  template <typename A, typename Chain> class MultiComparable {
    private:
      A const* a; // this is not a & because we have to be operator='able
      Chain chain;
    protected:
      typedef MultiComparable MC;
      template <typename T, typename C> static MultiComparable<T,C> mc(T const* t, C c) {
        return MultiComparable<T,C>(t,c);
      }
      template <typename T> static MultiComparable<T,int> mc(T const* t) {
        return MultiComparable<T>(t,0);
      }
      MultiComparable(A const* a, Chain chain) : a(a), chain(chain) {
      }
    public:
      bool operator<(MultiComparable const& as) const {
        if (*a != *as.a)
          return *a < *as.a;
        return chain < as.chain;
      }
      bool operator==(MultiComparable const& as) const {
        if (*a != *as.a)
          return false;
        return chain == as.chain;
      }
  };

  // example2: struct X : MultiComparable<int,MultiComparable<float,bool> > {X() : mc(multiComparable(i,multiComparable(f,false))) {}}

}


