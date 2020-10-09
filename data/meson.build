exec_path = bindir + '/' + meson.project_name()

desktop_conf = configuration_data()

set_desktop = [
  ['NAME', 'DvbNet-Gtk'],
  ['COMMENT', 'DvbNet tool'],
  ['EXEC', exec_path],
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
