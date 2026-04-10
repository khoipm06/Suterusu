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
inc = include_directories('src/app', 'src/injector')

sources = files(
  'src/app/main.cpp', 
  'src/app/overlay.cpp', 
  'src/app/config_manager.cpp', 
  'src/app/sys_utils.cpp', 
  'src/app/api_client.cpp', 
  'src/app/app_process.cpp'
)

# Build CDP injector as shared library
browser_injector_lib = shared_library(
  'browser_injector',
  'src/injector/browser_injector.cpp',
  'src/injector/cdp_connection.cpp',
  'src/injector/browser_session.cpp',
  'src/injector/browser_manager.cpp',
  include_directories: inc,
  dependencies: [boost_dep, nlohmann_json_dep, windows_libs],
  install_dir: meson.current_source_dir() + '/bin',
  install: true
)

# Build executable
executable(
  'suterusu',
  sources,
  dependencies: [curl_dep, nlohmann_json_dep, boost_dep, windows_libs],
  include_directories: inc,
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
