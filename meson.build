project('dvbnet-gtk', 'c', version: '1.1.2', meson_version: '>= 0.50.0')

subdir('data')

c = run_command('sh', '-c', 'for file in src/*.c; do echo $file; done')
dvbnet_src = c.stdout().strip().split('\n')

dvbnet_deps = [dependency('gtk+-3.0', version: '>= 3.22')]

executable(meson.project_name(), dvbnet_src, dependencies: dvbnet_deps, install: true)
