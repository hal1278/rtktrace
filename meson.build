project(
  'plotcore',
  'cpp',
  default_options : [
    'cpp_std=c++20',
    'buildtype=debugoptimized',
    'warning_level=3',
    'auto_features=disabled',
    'wrap_mode=nofallback',
  ],
)

fs = import('fs')
cpp = meson.get_compiler('cpp')

imgui_source_dir = get_option('imgui_source_dir')
implot_source_dir = get_option('implot_source_dir')

if imgui_source_dir == ''
  error('imgui_source_dir must identify the Nix-pinned Dear ImGui source tree')
endif
if implot_source_dir == ''
  error('implot_source_dir must identify the Nix-pinned ImPlot source tree')
endif

imgui_sources = [
  join_paths(imgui_source_dir, 'imgui.cpp'),
  join_paths(imgui_source_dir, 'imgui_draw.cpp'),
  join_paths(imgui_source_dir, 'imgui_tables.cpp'),
  join_paths(imgui_source_dir, 'imgui_widgets.cpp'),
  join_paths(imgui_source_dir, 'backends', 'imgui_impl_sdl3.cpp'),
  join_paths(imgui_source_dir, 'backends', 'imgui_impl_opengl3.cpp'),
]

implot_sources = [
  join_paths(implot_source_dir, 'implot.cpp'),
  join_paths(implot_source_dir, 'implot_items.cpp'),
]

foreach source : imgui_sources + implot_sources
  if not fs.exists(source)
    error('Required pinned dependency source does not exist: ' + source)
  endif
endforeach

sdl3_dep = dependency(
  'sdl3',
  method : 'pkg-config',
  required : true,
)

if host_machine.system() == 'linux'
  opengl_dep = dependency(
    'gl',
    method : 'pkg-config',
    required : true,
  )
elif host_machine.system() == 'windows'
  opengl_dep = cpp.find_library('opengl32', required : true)
else
  error('Unsupported host system: ' + host_machine.system())
endif

gui_include_directories = include_directories(
  imgui_source_dir,
  join_paths(imgui_source_dir, 'backends'),
  implot_source_dir,
)

gui_dependencies = static_library(
  'plotcore-gui-dependencies',
  imgui_sources,
  implot_sources,
  include_directories : gui_include_directories,
  dependencies : [sdl3_dep, opengl_dep],
  install : false,
)

executable(
  'plotcore-light',
  'src/light/main.cpp',
  include_directories : gui_include_directories,
  dependencies : [sdl3_dep, opengl_dep],
  link_with : gui_dependencies,
  install : true,
)

smoke_test = executable(
  'plotcore-smoke-test',
  'tests/smoke.cpp',
  install : false,
)

test('plotcore-smoke', smoke_test)
