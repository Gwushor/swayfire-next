project('swayfire-next', 'cpp',
  version: '0.1.0',
  default_options: ['cpp_std=c++17', 'warning_level=3']
)

wayfire_dep = dependency('wayfire')

swayfire_sources = files(
  'src/tree.cpp',
  'src/main.cpp'
)

shared_module('swayfire-next',
  swayfire_sources,
  dependencies: [wayfire_dep],
  install: true,
  install_dir: wayfire_dep.get_variable('plugindir')
)

install_data(
  'metadata/swayfire-next.xml',
  install_dir: wayfire_dep.get_variable('metadatadir')
)
