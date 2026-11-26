project('swayfire-next', 'cpp',
  version: '0.1.0',
  default_options: ['cpp_std=c++17', 'warning_level=3']
)

wayfire_dep = dependency('wayfire')

swayfire_sources = files(
  'src/main.cpp'
)

shared_module('swayfire-next',
  swayfire_sources,
  dependencies: [wayfire_dep],
  install: true,
  install_dir: get_option('libdir') / 'wayfire'
)
