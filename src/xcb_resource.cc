#include "common.hh"

#include "xcb_resource.hh"

namespace xcb {

unique_wnd make_unique_wnd(shared_conn conn) {
  return std::make_unique<xcb_resource<xcb_window_t,
                                       internal::WndDeleter>>(conn);
}

shared_wnd make_shared_wnd(shared_conn conn) {
  return std::make_shared<xcb_resource<xcb_window_t,
                                       internal::WndDeleter>>(conn);
}

unique_gc make_unique_gc(shared_conn conn) {
  return std::make_unique<xcb_resource<xcb_gcontext_t,
                                       internal::GCDeleter>>(conn);
}

shared_gc make_shared_gc(shared_conn conn) {
  return std::make_shared<xcb_resource<xcb_gcontext_t,
                                       internal::GCDeleter>>(conn);
}

}  // namespace xcb
