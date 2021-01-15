#ifndef XCB_XKB_HH
#define XCB_XKB_HH

#include <memory>
#include <string>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

namespace xcb {

class Keyboard {
public:
  virtual ~Keyboard() = default;

  virtual bool handle_event(xcb_connection_t* conn,
                            xcb_generic_event_t* event) = 0;

  virtual std::string get_utf8(xcb_key_press_event_t* event) = 0;

  static std::unique_ptr<Keyboard> create(xcb_connection_t* conn);

protected:
  Keyboard() = default;
  Keyboard(Keyboard const&) = delete;
  Keyboard& operator=(Keyboard const&) = delete;
};

}  // namespace xcb

#endif  // XCB_XKB_HH
