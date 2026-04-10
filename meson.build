project('suterusu', 'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'buildtype=release'
  ]
)

meson.add_install_script('scripts/copy_deps.py')

# Find dependencies
boost_dep = dependency('boost', modules : ['beast', 'asio'], static : true)
curl_dep = dependency('libcurl', required: true)
nlohmann_json_dep = dependency('nlohmann_json', required: true)

# Windows-specific libraries
windows_libs = []
if host_machine.system() == 'windows'
  windows_libs = [
    meson.get_compiler('cpp').find_library('ws2_32'),
  ]
endif

# Source files
sources = files('main.cpp', 'overlay.cpp')

# Build CDP injector as shared library
browser_injector_lib = shared_library(
  'browser_injector',
  'browser_injector.cpp',
  'cdp_connection.cpp',
  'browser_session.cpp',
  'browser_manager.cpp',
  dependencies: [boost_dep, nlohmann_json_dep, windows_libs],
  install_dir: meson.current_source_dir() + '/bin',
  install: true
)

# Build executable
executable(
  'suterusu',
  sources,
  dependencies: [curl_dep, nlohmann_json_dep],
  link_with: browser_injector_lib,
  win_subsystem: 'windows',  # Hide console by default (use --debug to show)
  install_dir: meson.current_source_dir() + '/bin',
  install: true
)

# Install config example
# Install JS folder
install_subdir('js', install_dir: meson.current_source_dir() + '/bin')

# Install config files only if they don't exist
meson.add_install_script(
  'scripts/install_configs.py',
  meson.current_source_dir() / 'bin', # Destination directory
  meson.current_source_dir() / 'config.json',
  meson.current_source_dir() / 'system_prompt.md'
)
