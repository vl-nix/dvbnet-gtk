c = run_command('sh', '-c', 'for file in *.h *.c; do echo $file; done')
dvbnet_src = c.stdout().strip().split('\n')

dvbnet_deps = [dependency('gtk+-3.0', version: '>= 3.22')]

executable(meson.project_name(), dvbnet_src, dependencies: dvbnet_deps, install: true)
