#include "common.hh"

#include "xcb_connection.hh"

namespace xcb {

shared_conn make_shared_conn(xcb_connection_t* conn) {
  return std::shared_ptr<xcb_connection_t>(
      conn, internal::xcb_connection_deleter());
}

unique_conn make_unique_conn(xcb_connection_t* conn) {
  return unique_conn(conn);
}

xcb_screen_t* get_screen(xcb_connection_t* conn, int screen_index) {
  auto iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
  for (; iter.rem; --screen_index, xcb_screen_next(&iter)) {
    if (screen_index == 0) {
      return iter.data;
    }
  }
  return nullptr;
}

}  // namespace xcb
