#ifndef XCB_EVENT_HH
#define XCB_EVENT_HH

#include <memory>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

namespace xcb {

namespace internal {

struct FreeDeleter {
  void operator() (void* ptr) {
    free(ptr);
  }
};

}  // namespace internal

typedef std::unique_ptr<xcb_generic_event_t,
                        internal::FreeDeleter> generic_event;

template<typename T>
using reply = std::unique_ptr<T, internal::FreeDeleter>;

}  // namespace xcb

#endif  // XCB_EVENT_HH
