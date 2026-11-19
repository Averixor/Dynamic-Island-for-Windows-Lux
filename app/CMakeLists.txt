cmake_minimum_required(VERSION 3.20)
project(DynamicIslandLux VERSION 1.0.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embed RT_MANIFEST + icon from generated paths (version follows project(VERSION)).
set(LUX_MANIFEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/DynamicIslandLux.manifest")
set(LUX_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.ico")
set(LUX_RESOURCE_H "${CMAKE_CURRENT_SOURCE_DIR}/resource.h")
file(TO_CMAKE_PATH "${LUX_MANIFEST_FILE}" LUX_MANIFEST_FILE)
file(TO_CMAKE_PATH "${LUX_ICON_FILE}" LUX_ICON_FILE)
file(TO_CMAKE_PATH "${LUX_RESOURCE_H}" LUX_RESOURCE_H)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/DynamicIslandLux.manifest.in
  ${CMAKE_CURRENT_BINARY_DIR}/DynamicIslandLux.manifest
  @ONLY
)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/resources.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/resources.gen.rc
  @ONLY
)

add_executable(DynamicIslandLux WIN32
  main.cpp
  logging.cpp
  settings_store.cpp
  settings_dialog.cpp
  menu_theme.cpp
  autostart.cpp
  lux_overlay.cpp
  # Island implementation TU (Windhawk archive + standalone body).
  "${CMAKE_CURRENT_SOURCE_DIR}/../Dynamic Island for Windows Lux.wh.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/resources.gen.rc"
)

target_include_directories(DynamicIslandLux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(DynamicIslandLux PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
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
    /guard:cf
    /sdl
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Zi>
  )
  # Legacy overlay TUs: keep /W3 until warning cleanup is complete.
  set_source_files_properties(
    lux_overlay.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../Dynamic Island for Windows Lux.wh.cpp"
    PROPERTIES COMPILE_OPTIONS "/W3"
  )
  set_property(TARGET DynamicIslandLux PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(DynamicIslandLux PRIVATE
    /MANIFEST:NO
    /GUARD:CF
    $<$<CONFIG:Release>:/DEBUG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
  )
endif()

target_link_libraries(DynamicIslandLux PRIVATE
  ole32
  oleaut32
  shcore
  d2d1
  dwrite
  dwmapi
  d3d11
  dxgi
  dcomp
  dxva2
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
  wbemuuid
  iphlpapi
  ws2_32
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
