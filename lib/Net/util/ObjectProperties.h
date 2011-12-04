#pragma once

namespace net {
  namespace util {
    template <typename T> bool isOnStack(T const* object) {
      unsigned const probe = 0;
      // XXX yack, this is highly platform dependant, but thanks to the great idea of somebody to
      // put fake objects on the stack (for performance, I guess) we have to be able to tell.
      return static_cast<void const*>(&probe) < static_cast<void const*>(object);
    }
  }
}
