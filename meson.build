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

model_include_directories = include_directories('include')
model_library = static_library(
  'plotcore-model',
  'src/model/loaded_file.cpp',
  include_directories : model_include_directories,
  install : false,
)
model_dep = declare_dependency(
  include_directories : model_include_directories,
  link_with : model_library,
)

analysis_library = static_library(
  'plotcore-analysis',
  [
    'src/analysis/common_time_range.cpp',
    'src/analysis/common_time_index.cpp',
    'src/analysis/coordinates.cpp',
    'src/analysis/enu.cpp',
    'src/analysis/gps_time.cpp',
    'src/analysis/reference_matching.cpp',
    'src/analysis/sample_rate.cpp',
    'src/analysis/statistics.cpp',
  ],
  dependencies : model_dep,
  install : false,
)
analysis_dep = declare_dependency(
  dependencies : model_dep,
  link_with : analysis_library,
)

io_library = static_library(
  'plotcore-io',
  [
    'src/io/nmea_parser.cpp',
    'src/io/pos_parser.cpp',
  ],
  dependencies : analysis_dep,
  install : false,
)
io_dep = declare_dependency(
  dependencies : analysis_dep,
  link_with : io_library,
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
  dependencies : [analysis_dep, sdl3_dep, opengl_dep],
  link_with : gui_dependencies,
  install : true,
)

model_test = executable(
  'plotcore-model-test',
  'tests/model.cpp',
  dependencies : model_dep,
  install : false,
)

test('plotcore-model', model_test)

coordinates_test = executable(
  'plotcore-coordinates-test',
  'tests/coordinates.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-coordinates', coordinates_test)

enu_test = executable(
  'plotcore-enu-test',
  'tests/enu.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-enu', enu_test)

sample_rate_test = executable(
  'plotcore-sample-rate-test',
  'tests/sample_rate.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-sample-rate', sample_rate_test)

gps_time_test = executable(
  'plotcore-gps-time-test',
  'tests/gps_time.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-gps-time', gps_time_test)

common_time_range_test = executable(
  'plotcore-common-time-range-test',
  'tests/common_time_range.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-common-time-range', common_time_range_test)

reference_matching_test = executable(
  'plotcore-reference-matching-test',
  'tests/reference_matching.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-reference-matching', reference_matching_test)

statistics_test = executable(
  'plotcore-statistics-test',
  'tests/statistics.cpp',
  dependencies : analysis_dep,
  install : false,
)

test('plotcore-statistics', statistics_test)

pos_parser_test = executable(
  'plotcore-pos-parser-test',
  'tests/pos_parser.cpp',
  dependencies : io_dep,
  install : false,
)

test(
  'plotcore-pos-parser',
  pos_parser_test,
  args : join_paths(meson.project_source_root(), 'tests', 'data', 'fictional.pos'),
)

nmea_parser_test = executable(
  'plotcore-nmea-parser-test',
  'tests/nmea_parser.cpp',
  dependencies : io_dep,
  install : false,
)

test('plotcore-nmea-parser', nmea_parser_test)
