#ifndef XCB_RESOURCE_HH
#define XCB_RESOURCE_HH

#include "xcb_connection.hh"

#include <memory>
#include <xcb/xproto.h>

namespace xcb {

namespace internal {

struct WndDeleter {
  void operator() (xcb_connection_t* conn, xcb_window_t wnd) const {
    xcb_destroy_window(conn, wnd);
  }
};

struct GCDeleter {
  void operator() (xcb_connection_t* conn, xcb_gcontext_t gc) const {
    xcb_free_gc(conn, gc);
  }
};

}  // namespace internal

template<typename T, typename Deleter>
class xcb_resource {
public:
  explicit xcb_resource(shared_conn conn)
    : conn_(conn), id_(xcb_generate_id(conn_.get())) {}
  constexpr xcb_resource()
    : id_(XCB_NONE) {}
  xcb_resource(xcb_resource const& res) = delete;
  xcb_resource(xcb_resource&& res)
    : conn_(res.conn_), id_(res.release()) { }
  ~xcb_resource() {
    reset();
  }

  xcb_resource& operator=(xcb_resource const& res) = delete;
  xcb_resource& operator=(xcb_resource&& res) {
    reset();
    conn_ = res.conn_;
    id_ = res.release();
    return *this;
  }

  T id() const {
    return id_;
  }

  void reset() {
    if (id_ == XCB_NONE)
      return;
    deleter_(conn_.get(), id_);
    id_ = XCB_NONE;
  }

  T release() {
    auto ret = id_;
    id_ = XCB_NONE;
    conn_.reset();
    return ret;
  }

private:
  shared_conn conn_;
  T id_;
  Deleter const deleter_{};
};

typedef std::unique_ptr<xcb_resource<xcb_window_t,
                                     internal::WndDeleter>> unique_wnd;
typedef std::shared_ptr<xcb_resource<xcb_window_t,
                                     internal::WndDeleter>> shared_wnd;

unique_wnd make_unique_wnd(shared_conn conn);
shared_wnd make_shared_wnd(shared_conn conn);

typedef std::unique_ptr<xcb_resource<xcb_gcontext_t,
                                     internal::GCDeleter>> unique_gc;
typedef std::shared_ptr<xcb_resource<xcb_gcontext_t,
                                     internal::GCDeleter>> shared_gc;

unique_gc make_unique_gc(shared_conn conn);
shared_gc make_shared_gc(shared_conn conn);

}  // namespace xcb

#endif  // XCB_RESOURCE_HH
