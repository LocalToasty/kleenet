#pragma once

namespace net {

  class ClonerI;

  class Clonable {
    public:
      virtual ClonerI const& getCloner() const = 0;
  };

  class ClonerI {
    public:
      virtual Clonable& operator()(Clonable&) const = 0;
      virtual Clonable* operator()(Clonable*) const = 0;
  };

  template <typename T> class Cloner : public ClonerI {
    private:
      // ... we're pretty shy ...
      Cloner() {
      }
      Cloner(Cloner const&) {
      }
    public:
      static ClonerI const& getCloner() {
        static Cloner const singleton;
        return singleton;
      }
      Clonable* operator()(Clonable* c) const {
        // NOTE: if you get a compilation error/warning here
        // you tried to equip a Clonable with an incompatible
        // Cloner. You have been a very bad code monkey!
        return static_cast<Clonable*>(new T(*static_cast<T*>(c)));
      }
      Clonable& operator()(Clonable& c) const {
        return *((*this)(&c));
      }
  };

}

