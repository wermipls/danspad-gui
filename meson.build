project('danspad-gui', 'c',
  version: '0.1',
  default_options: [
    'warning_level=1',
    'werror=true',
    'prefer_static=true',
    'buildtype=minsize',
  ])

deps = [
  dependency('libserialport'),
  dependency('sdl2')
]

exe = executable('danspad-gui',
  'main.c',
  dependencies: deps,
  install: true)
