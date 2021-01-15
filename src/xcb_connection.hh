#ifndef XCB_CONNECTION_HH
#define XCB_CONNECTION_HH

#include <memory>
#include <xcb/xcb.h>

namespace xcb {

namespace internal {

struct xcb_connection_deleter {
  void operator() (xcb_connection_t* ptr) {
    xcb_disconnect(ptr);
  }
};

}  // namespace internal

typedef std::shared_ptr<xcb_connection_t> shared_conn;
typedef std::unique_ptr<xcb_connection_t,
                        internal::xcb_connection_deleter> unique_conn;

shared_conn make_shared_conn(xcb_connection_t* conn);
unique_conn make_unique_conn(xcb_connection_t* conn);

xcb_screen_t* get_screen(xcb_connection_t* conn, int screen_index);

}  // namespace xcb

#endif  // XCB_CONNECTION_HH
