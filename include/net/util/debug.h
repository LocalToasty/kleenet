#pragma once

// remember to undefine these at the end of the file
#define ND_FLAGS_slack     ((1u<<0))       /*default reason*/
#define ND_FLAGS_mapping   ((1u<<1))       /*debugging mapping*/
#define ND_FLAGS_clusters  ((1u<<2))       /*debugging clustering*/
#define ND_FLAGS_searchers ((1u<<3))       /*debugging searchers*/
#define ND_FLAGS_external1 ((1u<<4))       /*debugging external module (1)*/
#define ND_FLAGS_external2 ((1u<<5))       /*debugging external module (2)*/
#define ND_FLAGS_term      ((1u<<6))       /*debugging external module (2)*/
#define ND_FLAGS_all       ((1u<<15)-1)    /*everything!*/

#define ND_MAKEFLAG(what) what = ND_FLAGS_##what,

namespace net {
  namespace debug {
    enum DebugFlags {
      ND_MAKEFLAG(slack)
      ND_MAKEFLAG(mapping)
      ND_MAKEFLAG(clusters)
      ND_MAKEFLAG(searchers)
      ND_MAKEFLAG(external1)
      ND_MAKEFLAG(external2)
      ND_MAKEFLAG(term)
      ND_MAKEFLAG(all)
      none = 0
    };
  }
}

#define ENABLE_DEBUG (none)

#undef ND_MAKEFLAG

#ifdef NDEBUG
#  undef ENABLE_DEBUG
#  define ENABLE_DEBUG 0
#endif

#if ENABLE_DEBUG
#  include <iostream>
#endif

namespace net {
  namespace debug {
    enum EnableDebugWrapper { enable = ENABLE_DEBUG };
    struct Fake {};
    template <typename T>
    Fake& operator<<(Fake& out, T const&) {
      return out;
    }
    template <bool enable>
    struct ifdebug {
    };
    template <>
    struct ifdebug<false> {
      typedef Fake OutType;
      typedef Fake EndlType;
      OutType cout;
      OutType cerr;
      EndlType endl;
      static bool const enable = false;
    };
#if ENABLE_DEBUG
    template <>
    struct ifdebug<true> {
      typedef ::std::ostream OutType;
      typedef OutType& EndlType(OutType&);
      OutType& cout;
      OutType& cerr;
      EndlType& endl;
      static bool const enable = true;
      ifdebug() : cout(::std::cout), cerr(::std::cerr), endl(::std::endl) {}
    };
#endif
  }
  template <unsigned flag>
  struct DEBUG {
    typedef debug::ifdebug<flag & debug::enable> Manifold;
    static Manifold deb;
    static typename Manifold::OutType& cout;
    static typename Manifold::OutType& cerr;
    static typename Manifold::EndlType& endl;
    static bool const enable = Manifold::enable;
  };
  template <unsigned flag>
    typename DEBUG<flag>::Manifold DEBUG<flag>::deb;
  template <unsigned flag>
    typename DEBUG<flag>::Manifold::OutType& DEBUG<flag>::cout = DEBUG<flag>::deb.cout;
  template <unsigned flag>
    typename DEBUG<flag>::Manifold::OutType& DEBUG<flag>::cerr = DEBUG<flag>::deb.cerr;
  template <unsigned flag>
    typename DEBUG<flag>::Manifold::EndlType& DEBUG<flag>::endl = DEBUG<flag>::deb.endl;
}

#undef ENABLE_DEBUG
#undef ND_FLAGS_slack
#undef ND_FLAGS_mapping
#undef ND_FLAGS_clusters
#undef ND_FLAGS_searchers
#undef ND_FLAGS_external1
#undef ND_FLAGS_external2
#undef ND_FLAGS_all
