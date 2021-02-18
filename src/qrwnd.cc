#include "common.hh"

#include "args.hh"
#include "xcb_atoms.hh"
#include "xcb_connection.hh"
#include "xcb_event.hh"
#include "xcb_resource.hh"
#include "xcb_xkb.hh"

#include <algorithm>
#include <cairo-xcb.h>
#include <errno.h>
#include <iostream>
#include <limits>
#include <optional>
#include <qrencode.h>
#include <string.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xfixes.h>

#ifndef VERSION
# warning No version defined
# define VERSION
#endif

namespace {

constexpr char const kTitle[] = "QRwnd";
constexpr char const kClass[] = "org.the_jk.qrwnd";

struct QRcodeDeleter {
  void operator() (QRcode* qrcode) const {
    QRcode_free(qrcode);
  }
};

struct CairoDeleter {
  void operator() (cairo_t* cr) const {
    cairo_destroy(cr);
  }
};

struct CairoSurfaceDeleter {
  void operator() (cairo_surface_t* surface) const {
    cairo_surface_destroy(surface);
  }
};

bool looks_like_url(std::string_view str) {
  if (str.empty())
    return false;
  if (str.find(' ') != std::string::npos)
    return false;
  return str.find("://") != std::string::npos;
}

xcb_visualtype_t *find_visual(xcb_screen_t* screen, xcb_visualid_t visual) {
  auto depth_iter = xcb_screen_allowed_depths_iterator(screen);
  for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
    auto visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
    for (; visual_iter.rem; xcb_visualtype_next(&visual_iter))
      if (visual == visual_iter.data->visual_id)
        return visual_iter.data;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  auto args = Args::create();
  auto* help = args->add_option('h', "help", "display this text and exit.");
  auto* version = args->add_option('V', "version", "display version and exit.");
  auto* everything = args->add_option(
      'E', "everything",
      "show QR code for all selection content, not just URLs.");
  auto* display = args->add_option_with_arg(
      'D', "display", "connect to DISPLAY instead of default.", "DISPLAY");
  std::vector<std::string> arguments;
  if (!args->run(argc, argv, "qrwnd", std::cerr, &arguments)) {
    std::cerr << "Try `qrwnd --help` for usage." << std::endl;
    return EXIT_FAILURE;
  }
  if (help->is_set()) {
    std::cout << "Usage: `qrwnd [OPTIONS]`\n"
              << "Displays a QR code for URL that is currently in"
              << " primary selection.\n"
              << "\n";
    args->print_descriptions(std::cout, 80);
    return EXIT_SUCCESS;
  }
  if (version->is_set()) {
    std::cout << "QRwnd " VERSION " written by "
              << "Joel Klinghed <the_jk@spawned.biz>" << std::endl;
    return EXIT_SUCCESS;
  }
  if (!arguments.empty()) {
    std::cerr << "Unexpected arguments after options.\n"
              << "Try `qrwnd --help` for usage." << std::endl;
    return EXIT_FAILURE;
  }

  xcb::shared_conn conn;
  int screen_index = 0;
  if (display->is_set()) {
    conn = xcb::make_shared_conn(xcb_connect(display->arg().c_str(),
                                             &screen_index));
  } else {
    conn = xcb::make_shared_conn(xcb_connect(nullptr, &screen_index));
  }

  {
    auto err = xcb_connection_has_error(conn.get());
    if (err) {
      std::cerr << "Unable to connect to X display: " << err << std::endl;
      return EXIT_FAILURE;
    }
  }

  auto atoms = xcb::Atoms::create(conn);
  auto primary = atoms->get("PRIMARY");
  auto target_property = atoms->get("QRWND_DATA");
  auto utf8_string = atoms->get("UTF8_STRING");
  auto string_atom = atoms->get("STRING");
  auto incr = atoms->get("INCR");
  auto wm_protocols = atoms->get("WM_PROTOCOLS");
  auto wm_delete_window = atoms->get("WM_DELETE_WINDOW");
  xcb_prefetch_extension_data(conn.get(), &xcb_xfixes_id);

  auto* screen = xcb::get_screen(conn.get(), screen_index);
  assert(screen);

  if (!atoms->sync()) {
    std::cerr << "Failed to get X atoms." << std::endl;
    return EXIT_FAILURE;
  }

  auto* xfixes_reply = xcb_get_extension_data(conn.get(), &xcb_xfixes_id);
  if (!xfixes_reply->present) {
    std::cerr << "No XFixes extension, needed to monitor selection."
              << std::endl;
    return EXIT_FAILURE;
  }

  auto keyboard = xcb::Keyboard::create(conn.get());
  if (!keyboard) {
    std::cerr << "Failed to initialize XKB." << std::endl;
    return EXIT_FAILURE;
  }

  auto selection = primary.get();

  xcb_xfixes_query_version(conn.get(), XCB_XFIXES_MAJOR_VERSION,
                           XCB_XFIXES_MINOR_VERSION);

  xcb_xfixes_select_selection_input(
      conn.get(),
      screen->root,
      selection,
      XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER);

  auto wnd = xcb::make_unique_wnd(conn);

  uint16_t wnd_width = 175;
  uint16_t wnd_height = 175;

  uint32_t value_list[3];
  uint32_t value_mask = 0;
  value_mask |= XCB_CW_BACK_PIXEL;
  value_list[0] = screen->white_pixel;
  value_mask |= XCB_CW_EVENT_MASK;
  value_list[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
  xcb_create_window(conn.get(), XCB_COPY_FROM_PARENT, wnd->id(), screen->root,
                    0, 0, wnd_width, wnd_height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual, value_mask, value_list);

  auto* visual = find_visual(screen, screen->root_visual);
  if (!visual) {
    std::cerr << "Unable to find a matching visual." << std::endl;
    return EXIT_FAILURE;
  }

  xcb_icccm_set_wm_name(conn.get(), wnd->id(), string_atom.get(),
                        8, sizeof(kTitle) - 1, kTitle);
  xcb_icccm_set_wm_class(conn.get(), wnd->id(), sizeof(kClass) - 1, kClass);
  xcb_atom_t atom_list[1];
  atom_list[0] = wm_delete_window.get();
  xcb_icccm_set_wm_protocols(conn.get(), wnd->id(),
                             wm_protocols.get(), 1, atom_list);

  std::unique_ptr<cairo_surface_t, CairoSurfaceDeleter> surface(
      cairo_xcb_surface_create(conn.get(), wnd->id(), visual,
                               wnd_width, wnd_height));
  std::unique_ptr<cairo_t, CairoDeleter> cr(cairo_create(surface.get()));

  xcb_map_window(conn.get(), wnd->id());
  // No xcb_flush needed here as request_queued and invalidate will xcb_flush

  // Do not send any new convert selection requests while one is active.
  // As they all (currently) write to the same property that is just
  // a bad idea.
  bool request_active = false;
  bool request_queued = true;
  xcb_window_t incr_requestor = XCB_NONE;
  xcb_window_t incr_property = XCB_NONE;
  auto request_type = utf8_string.get();

  bool update_code = false;
  std::string current_data;
  std::string incr_data;
  std::unique_ptr<cairo_surface_t, CairoSurfaceDeleter> current;

  bool invalidate = true;
  xcb_rectangle_t invalidate_rect{0, 0, wnd_width, wnd_height};

  while (true) {
    bool flush = false;
    if (request_queued && !request_active) {
      request_queued = false;
      request_active = true;
      xcb_convert_selection(conn.get(), wnd->id(), selection,
                            request_type,
                            target_property.get(),
                            XCB_CURRENT_TIME);
      flush = true;
    }

    if (update_code) {
      update_code = false;
      if (everything->is_set() || looks_like_url(current_data)) {
        auto qrcode = std::unique_ptr<QRcode, QRcodeDeleter>(
            QRcode_encodeString8bit(
                current_data.c_str(),
                0 /* autoselect version */,
                /* showing on screen so low ec */
                QR_ECLEVEL_L));
        if (qrcode) {
          current.reset(cairo_image_surface_create(
                            CAIRO_FORMAT_RGB24,
                            qrcode->width,
                            qrcode->width));
          auto stride = cairo_image_surface_get_stride(current.get());
          cairo_surface_flush(current.get());
          auto* data = cairo_image_surface_get_data(current.get());
          if (data) {
            for (int y = 0; y < qrcode->width; ++y) {
              auto* out_row = data + y * stride;
              auto* in_row = qrcode->data + y * qrcode->width;
              for (int x = 0; x < qrcode->width; ++x) {
                auto c = (*in_row & 1) ? 0 : 0xff;
                std::fill_n(out_row, 4, c);
                ++in_row;
                out_row += 4;
              }
            }
          }
          cairo_surface_mark_dirty(current.get());
        } else {
          std::cerr << "Failed to generate QR code: "
                    << strerror(errno) << std::endl;
          current.reset();
        }
      } else {
        current.reset();
      }

      invalidate = true;
      // Force redraw of all
      invalidate_rect = { 0, 0, wnd_width, wnd_height };
    }

    if (invalidate) {
      invalidate = false;
      cairo_rectangle(cr.get(), invalidate_rect.x, invalidate_rect.y,
                      invalidate_rect.width, invalidate_rect.height);
      if (current) {
        cairo_save(cr.get());
        cairo_clip(cr.get());
        auto org_w = cairo_image_surface_get_width(current.get());
        auto org_h = cairo_image_surface_get_height(current.get());
        auto w = org_w;
        auto h = org_h;
        while (true) {
          if (w * 2 > wnd_width || h * 2 > wnd_height)
            break;
          w *= 2;
          h *= 2;
        }
        auto x = (wnd_width - w) / 2;
        auto y = (wnd_height - h) / 2;
        if (x > 0) {
          cairo_rectangle(cr.get(), 0, 0, x, wnd_height);
          cairo_rectangle(cr.get(), x + w, 0, wnd_width - (x + w), wnd_height);
        }
        if (y > 0) {
          cairo_rectangle(cr.get(), x, 0, w, y);
          cairo_rectangle(cr.get(), x, y + h, w, wnd_height - (y + h));
        }
        cairo_set_source_rgb(cr.get(), 1.0, 1.0, 1.0);
        cairo_fill(cr.get());
        cairo_translate(cr.get(), x, y);
        cairo_scale(cr.get(), static_cast<double>(w) / org_w,
                    static_cast<double>(h) / org_h);
        cairo_set_source_surface(cr.get(), current.get(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr.get()),
                                 CAIRO_FILTER_NEAREST);
        cairo_paint(cr.get());
        cairo_restore(cr.get());
      } else {
        cairo_set_source_rgb(cr.get(), 1.0, 1.0, 1.0);
        cairo_fill(cr.get());
      }
      cairo_surface_flush(surface.get());
      flush = true;
    }

    if (flush)
      xcb_flush(conn.get());

    xcb::generic_event event(xcb_wait_for_event(conn.get()));
    if (!event) {
      auto err = xcb_connection_has_error(conn.get());
      if (err) {
        std::cerr << "X connection had fatal error: " << err << std::endl;
      } else {
        std::cerr << "X connection had fatal I/O error." << std::endl;
      }
      return EXIT_FAILURE;
    }
    auto response_type = XCB_EVENT_RESPONSE_TYPE(event.get());
    if (response_type == XCB_SELECTION_NOTIFY) {
      auto* e = reinterpret_cast<xcb_selection_notify_event_t*>(event.get());
      if (e->selection == selection) {
        assert(request_active);
        request_active = false;
        if (e->property) {
          auto cookie = xcb_get_property(
              conn.get(), 1 /* delete */, e->requestor, e->property,
              XCB_GET_PROPERTY_TYPE_ANY,
              0, std::numeric_limits<uint32_t>::max() / 4);
          xcb_generic_error_t* err = nullptr;
          xcb::reply<xcb_get_property_reply_t> reply(
              xcb_get_property_reply(conn.get(), cookie, &err));
          if (reply) {
            if (reply->type == utf8_string.get() ||
                reply->type == string_atom.get()) {
              std::string_view data(
                  reinterpret_cast<char*>(xcb_get_property_value(reply.get())),
                  xcb_get_property_value_length(reply.get()));
              if (data != current_data) {
                current_data = data;
                update_code = true;
              }
            } else if (reply->type == incr.get()) {
              incr_requestor = e->requestor;
              incr_property = e->property;
              auto len = xcb_get_property_value_length(reply.get());
              if (len == 4) {
                auto size = *reinterpret_cast<int32_t*>(
                    xcb_get_property_value(reply.get()));
                incr_data.reserve(size);
              }
              incr_data.clear();
            } else {
              std::cerr << "Unsupported selection property type: "
                        << reply->type << std::endl;
            }
          } else {
            std::cerr << "Error getting property: " <<
              xcb_event_get_error_label(err->error_code) << std::endl;
            free(err);
          }
        } else {
          // Target format not supported, try with STRING if using UTF8_STRING
          if (e->target == utf8_string.get()) {
            request_queued = true;
            request_type = string_atom.get();
          }
        }
      }
      continue;
    } else if (response_type == XCB_PROPERTY_NOTIFY) {
      auto* e = reinterpret_cast<xcb_property_notify_event_t*>(event.get());
      if (e->window == incr_requestor && e->atom == incr_property) {
        if (e->state == XCB_PROPERTY_NEW_VALUE) {
          auto cookie = xcb_get_property(
              conn.get(), 1 /* delete */, incr_requestor, incr_property,
              XCB_GET_PROPERTY_TYPE_ANY,
              0, std::numeric_limits<uint32_t>::max() / 4);
          xcb_generic_error_t* err = nullptr;
          xcb::reply<xcb_get_property_reply_t> reply(
              xcb_get_property_reply(conn.get(), cookie, &err));
          if (reply) {
            auto len = xcb_get_property_value_length(reply.get());
            if (len == 0) {
              if (incr_data != current_data) {
                current_data = incr_data;
                update_code = true;
              }
              incr_data.clear();
              incr_requestor = XCB_NONE;
              incr_property = XCB_NONE;
            } else {
              if (reply->type == utf8_string.get() ||
                  reply->type == string_atom.get()) {
                incr_data.append(reinterpret_cast<char*>(
                                     xcb_get_property_value(reply.get())),
                                 len);
              } else {
                std::cerr << "Unsupported property notify type: "
                          << reply->type << std::endl;
                // Even if we don't understand the type we need to continue
                // to delete the property or the owner will hang waiting for
                // us.
              }
            }
          } else {
            std::cerr << "Error getting property: " <<
              xcb_event_get_error_label(err->error_code) << std::endl;
            free(err);
          }
        }
      }
      continue;
    } else if (response_type ==
               xfixes_reply->first_event + XCB_XFIXES_SELECTION_NOTIFY) {
      auto* e = reinterpret_cast<xcb_xfixes_selection_notify_event_t*>(
          event.get());
      if (e->selection == selection) {
        request_queued = true;
        request_type = utf8_string.get();
      }
      continue;
    } else if (response_type == XCB_EXPOSE) {
      auto* e = reinterpret_cast<xcb_expose_event_t*>(event.get());
      if (e->window == wnd->id()) {
        invalidate = true;
        invalidate_rect.x = e->x;
        invalidate_rect.y = e->y;
        invalidate_rect.width = e->width;
        invalidate_rect.height = e->height;
      }
      continue;
    } else if (response_type == XCB_KEY_PRESS) {
      auto* e = reinterpret_cast<xcb_key_press_event_t*>(event.get());
      if (e->event == wnd->id()) {
        auto str = keyboard->get_utf8(e);
        if (str == "q" || str == "\x1b" /* Escape */) {
          // Quit
          break;
        }
      }
      continue;
    } else if (response_type == XCB_CONFIGURE_NOTIFY) {
      auto* e = reinterpret_cast<xcb_configure_notify_event_t*>(event.get());
      if (e->window == wnd->id()) {
        wnd_width = e->width;
        wnd_height = e->height;
        cairo_xcb_surface_set_size(surface.get(), e->width, e->height);
      }
      continue;
    } else if (response_type == XCB_REPARENT_NOTIFY) {
      // Ignored, part of XCB_EVENT_MASK_STRUCTURE_NOTIFY
      continue;
    } else if (response_type == XCB_MAP_NOTIFY) {
      // Ignored, part of XCB_EVENT_MASK_STRUCTURE_NOTIFY
      continue;
    } else if (keyboard->handle_event(conn.get(), event.get())) {
      continue;
    } else if (response_type == XCB_CLIENT_MESSAGE) {
      auto* e = reinterpret_cast<xcb_client_message_event_t*>(event.get());
      if (e->window == wnd->id() && e->type == wm_protocols.get() &&
          e->format == 32) {
        if (e->data.data32[0] == wm_delete_window.get()) {
          // Quit
          break;
        }
      }
      continue;
    }

#ifndef NDEBUG
    if (response_type == 0) {
      auto* e = reinterpret_cast<xcb_generic_error_t*>(event.get());
      std::cout << "Unhandled error: "
                << xcb_event_get_error_label(e->error_code) << std::endl;
    } else {
      std::cout << "Unhandled event: " << xcb_event_get_label(response_type)
                << std::endl;
    }
#endif
  }

  return EXIT_SUCCESS;
}
