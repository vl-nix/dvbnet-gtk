desktop_conf = configuration_data()

set_desktop = [
  ['NAME', 'DvbNet-Gtk'],
  ['COMMENT', 'DvbNet tool'],
  ['EXEC', meson.project_name() + '-pkexec'],
  ['ICON', 'display'],
  ['TERMINAL', 'false'],
  ['TYPE', 'Application'],
  ['CATEGORIES', 'GTK;GNOME;Network;'],
  ['VERSION', meson.project_version()]
]

foreach props: set_desktop
  desktop_conf.set(props[0], props[1])
endforeach

desktop = meson.project_name() + '.desktop'

configure_file(
  input: 'desktop',
  output: desktop,
  configuration: desktop_conf,
  install: true,
  install_dir: join_paths('share', 'applications')
)

bindir = join_paths(get_option('prefix'), get_option('bindir'))
pkexec_sh = configure_file(input: 'dvbnet-gtk-pkexec', output: 'dvbnet-gtk-pkexec', copy: true)
install_data(pkexec_sh, install_dir: bindir)
