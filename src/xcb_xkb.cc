#include "common.hh"

#include "xcb_event.hh"
#include "xcb_xkb.hh"

#define explicit dont_use_cxx_explicit
#include <xcb/xkb.h>
#undef explicit
#include <xkbcommon/xkbcommon-x11.h>

namespace xcb {

namespace {

struct KeymapDeleter {
  void operator() (xkb_keymap* keymap) const {
    if (keymap)
      xkb_keymap_unref(keymap);
  }
};

struct StateDeleter {
  void operator() (xkb_state* state) const {
    if (state)
      xkb_state_unref(state);
  }
};

struct ContextDeleter {
  void operator() (xkb_context* context) const {
    if (context)
      xkb_context_unref(context);
  }
};

class KeyboardImpl : public Keyboard {
public:
  KeyboardImpl() = default;

  bool init(xcb_connection_t* conn) {
    if (!xkb_x11_setup_xkb_extension(conn,
                                     XKB_X11_MIN_MAJOR_XKB_VERSION,
                                     XKB_X11_MIN_MINOR_XKB_VERSION,
                                     XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                     NULL, NULL, &first_xkb_event_, NULL))
      return false;

    ctx_.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
    if (!ctx_)
      return false;
    device_id_ = xkb_x11_get_core_keyboard_device_id(conn);
    if (device_id_ == -1)
      return false;
    if (!update_keymap(conn))
      return false;
    select_events(conn);
    return true;
  }

  bool handle_event(xcb_connection_t* conn,
                    xcb_generic_event_t* event) override {
    if (XCB_EVENT_RESPONSE_TYPE(event) == first_xkb_event_) {
      auto* xkb_event = reinterpret_cast<xkb_generic_event_t*>(event);
      if (xkb_event->deviceID == device_id_) {
        switch (xkb_event->xkbType) {
        case XCB_XKB_NEW_KEYBOARD_NOTIFY: {
          auto* e =
            reinterpret_cast<xcb_xkb_new_keyboard_notify_event_t*>(event);
          if (e->changed & XCB_XKB_NKN_DETAIL_KEYCODES)
            update_keymap(conn);
          break;
        }
        case XCB_XKB_MAP_NOTIFY:
          update_keymap(conn);
          break;
        case XCB_XKB_STATE_NOTIFY: {
          auto* e =
            reinterpret_cast<xcb_xkb_state_notify_event_t*>(event);
          xkb_state_update_mask(state_.get(),
                                e->baseMods, e->latchedMods, e->lockedMods,
                                e->baseGroup, e->latchedGroup, e->lockedGroup);
          break;
        }
        }
      }
      return true;
    }
    return false;
  }

  std::string get_utf8(xcb_key_press_event_t* event) override {
    char tmp[16];
    xkb_state_key_get_utf8(state_.get(), event->detail, tmp, sizeof(tmp));
    return std::string(tmp);
  }

private:
  struct xkb_generic_event_t {
    uint8_t response_type;
    uint8_t xkbType;
    uint16_t sequence;
    xcb_timestamp_t time;
    uint8_t deviceID;
  };

  bool update_keymap(xcb_connection_t* conn) {
    auto* keymap = xkb_x11_keymap_new_from_device(ctx_.get(), conn, device_id_,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap)
      return false;
    auto* state = xkb_x11_state_new_from_device(keymap, conn, device_id_);
    if (!state) {
      xkb_keymap_unref(keymap);
      return false;
    }
    keymap_.reset(keymap);
    state_.reset(state);
    return true;
  }

  void select_events(xcb_connection_t *conn) {
    static const uint16_t new_keyboard_details = XCB_XKB_NKN_DETAIL_KEYCODES;
    static const uint16_t map_parts = XCB_XKB_MAP_PART_KEY_TYPES |
      XCB_XKB_MAP_PART_KEY_SYMS |
      XCB_XKB_MAP_PART_MODIFIER_MAP |
      XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
      XCB_XKB_MAP_PART_KEY_ACTIONS |
      XCB_XKB_MAP_PART_VIRTUAL_MODS |
      XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
    static const uint16_t state_details = XCB_XKB_STATE_PART_MODIFIER_BASE |
      XCB_XKB_STATE_PART_MODIFIER_LATCH |
      XCB_XKB_STATE_PART_MODIFIER_LOCK |
      XCB_XKB_STATE_PART_GROUP_BASE |
      XCB_XKB_STATE_PART_GROUP_LATCH |
      XCB_XKB_STATE_PART_GROUP_LOCK;

    xcb_xkb_select_events_details_t details = {};
    details.affectNewKeyboard = new_keyboard_details;
    details.newKeyboardDetails = new_keyboard_details;
    details.affectState = state_details;
    details.stateDetails = state_details;

    xcb_xkb_select_events_aux(conn,
                              device_id_,
                              XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
                              XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                              XCB_XKB_EVENT_TYPE_STATE_NOTIFY,
                              0,
                              0,
                              map_parts,
                              map_parts,
                              &details);
  }

  std::unique_ptr<xkb_context, ContextDeleter> ctx_;
  std::unique_ptr<xkb_keymap, KeymapDeleter> keymap_;
  std::unique_ptr<xkb_state, StateDeleter> state_;
  uint8_t first_xkb_event_;
  int32_t device_id_;
};

}  // namespace

std::unique_ptr<Keyboard> Keyboard::create(xcb_connection_t* conn) {
  auto ret = std::make_unique<KeyboardImpl>();
  if (ret->init(conn))
    return ret;
  return nullptr;
}

}  // namespace xcb
