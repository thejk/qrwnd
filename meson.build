project('qrwnd', 'cpp',
        version: '0.1',
        default_options: [
          'warning_level=3',
          'cpp_std=c++17',
          'cpp_rtti=false'
        ])

cpp_optional_flags = [
  '-fno-exceptions',
  '-fvisibility=hidden',
  # Redefining explicit to be able to include xkb.h
  '-Wno-keyword-macro',
]
cpp_flags = [
  '-DVERSION="' + meson.project_version() + '"'
]
if get_option('buildtype') == 'release'
  cpp_flags += '-DNDEBUG'
endif

cpp = meson.get_compiler('cpp')
cpp_flags += cpp.get_supported_arguments(cpp_optional_flags)
add_project_arguments(cpp_flags, language: 'cpp')

cairo_dep = dependency('cairo-xcb', version: '>= 1.17.4')

qrencode_dep = dependency('libqrencode', version: '>= 4.1.1')

xcb_dep = [dependency('xcb', version: '>= 1.14'),
           dependency('xcb-xkb', version: '>= 1.14'),
           dependency('xcb-xfixes', version: '>= 1.14'),
           dependency('xcb-event', version: '>= 0.4.0'),
           dependency('xcb-icccm', version: '>= 0.4.1'),
           dependency('xcb-keysyms', version: '>= 0.4.0'),
           dependency('xkbcommon-x11', version: '>= 1.0.3')]

exe = executable('qrwnd',
                 sources: [
                   'src/args.cc',
                   'src/qrwnd.cc',
                   'src/xcb_atoms.cc',
                   'src/xcb_connection.cc',
                   'src/xcb_resource.cc',
                   'src/xcb_xkb.cc',
                 ],
                 dependencies: [cairo_dep, qrencode_dep, xcb_dep],
                 install: true)

xdg_desktop_menu = find_program('xdg-desktop-menu', required: false,
                                native: true)
if xdg_desktop_menu.found()
  meson.add_install_script(xdg_desktop_menu, 'install', '--novendor',
                           files('data/org.the_jk.qrwnd.desktop'))
else
  desktop_path = get_option('datadir') / 'applications'

  install_data('data/org.the_jk.qrwnd.desktop', install_dir : desktop_path)
endif

xdg_icon_resource = find_program('xdg-icon-resource', required: false,
                                 native: true)
if xdg_icon_resource.found()
  meson.add_install_script(xdg_icon_resource, 'install', '--novendor',
                           '--size', '24', files('data/org.the_jk.qrwnd.png'))
else
  icon_path = get_option('datadir') / 'icons' / 'hicolor' / '24x24' / 'apps'
  install_data('data/org.the_jk.qrwnd.png', install_dir : icon_path)

  gtk_update_icon_cache = find_program('gtk-update-icon-cache',
                                       required: false, native: true)
  if gtk_update_icon_cache.found()
    meson.add_install_script(gtk_update_icon_cache, icon_path)
  endif
endif
