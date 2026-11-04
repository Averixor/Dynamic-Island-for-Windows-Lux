cmake_minimum_required(VERSION 3.20)
project(DynamicIslandLux VERSION 1.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(DynamicIslandLux WIN32
  main.cpp
  logging.cpp
  settings_store.cpp
  settings_dialog.cpp
  autostart.cpp
  lux_overlay.cpp
  # Step A: legacy body linked for symbol resolution; full Init wired in Step B.
  "${CMAKE_CURRENT_SOURCE_DIR}/../Dynamic Island for Windows Lux.wh.cpp"
  resources.rc
)

target_include_directories(DynamicIslandLux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(DynamicIslandLux PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  LUX_APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  LUX_APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  LUX_APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  "LUX_APP_VERSION_STR=\"${PROJECT_VERSION}\""
  LUX_STANDALONE=1
)

if(MSVC)
  target_compile_options(DynamicIslandLux PRIVATE
    /utf-8
    /permissive-
    /W4
    /EHsc
    $<$<CONFIG:Release>:/O2>
  )
  # Phase 1: isolate legacy overlay warnings.
  # TODO: raise lux_overlay.cpp / .wh.cpp to /W4 after port cleanup (build-smoke follow-up).
  set_source_files_properties(
    lux_overlay.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../Dynamic Island for Windows Lux.wh.cpp"
    PROPERTIES COMPILE_OPTIONS "/W3"
  )
  set_property(TARGET DynamicIslandLux PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(DynamicIslandLux PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(DynamicIslandLux PRIVATE
  ole32
  oleaut32
  shcore
  d2d1
  dwrite
  dwmapi
  gdi32
  user32
  shell32
  runtimeobject
  windowscodecs
  setupapi
  winhttp
  pdh
  winmm
  propsys
  comctl32
  uxtheme
  advapi32
  comdlg32
)

set_target_properties(DynamicIslandLux PROPERTIES
  OUTPUT_NAME "DynamicIslandLux"
  WIN32_EXECUTABLE TRUE
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.gen.rc
  @ONLY
)
target_sources(DynamicIslandLux PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.gen.rc)
