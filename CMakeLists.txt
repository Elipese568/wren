# Wren —— 小型自绘 UI 框架。
#
# 单 HWND、retained-mode：控件自己画形状与文字，Host 负责焦点/键盘/DPI/MSAA/
# 呈现。默认 D2D 直接画进 DirectComposition 交换链（GPU），D2D 不可用时退回
# GDI+ 整帧 DIB。框架、demo、纯逻辑单测都可以独立配置构建，不依赖宿主工程。
#
# 独立构建：
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#   cmake --build build --config Debug
#
# 作为 submodule 接入宿主工程：
#   add_subdirectory(<wren 目录>)
#   target_link_libraries(<你的目标> PRIVATE Wren::wren)

cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(Wren VERSION 0.1.0 LANGUAGES CXX)
  set(WREN_TOPLEVEL ON)
else()
  set(WREN_TOPLEVEL OFF)
endif()

option(WREN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(WREN_BUILD_DEMO "Build the Wren demo application" ${WREN_TOPLEVEL})
option(WREN_BUILD_TESTS "Build the Wren unit tests" ${WREN_TOPLEVEL})

set(WREN_SOURCES
  canvas.cpp
  text_layout.cpp
  theme.cpp
  widget.cpp
  controls.cpp
  dwrite_text.cpp
  presenter.cpp
  gpu_presenter.cpp
  material.cpp
  host.cpp
  automation.cpp
)

add_library(wren STATIC ${WREN_SOURCES})
add_library(Wren::wren ALIAS wren)

target_include_directories(wren PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_compile_features(wren PUBLIC cxx_std_20)
target_compile_definitions(wren PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# 头文件里的注释是 UTF-8 中文：使用方也必须按 UTF-8 读源码，否则 MSVC 会按 ANSI
# 代码页误解注释、吞掉下一行，连 #include 都会丢。
target_compile_options(wren PUBLIC /utf-8)
target_compile_options(wren PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)

# gdiplus：帧与形状；d2d1/dwrite：灰度文字与 D2D 绘制；d3d11/dxgi/dcomp：组合交换链；
# dwmapi：材质/深色标题栏/圆角；oleacc：LresultFromObject；oleaut32：SysAllocString；
# advapi32：主题与强调色注册表。
target_link_libraries(wren PUBLIC
  gdiplus
  d2d1
  dwrite
  d3d11
  dcomp
  dxgi
  dwmapi
  oleacc
  oleaut32
  ole32
  user32
  gdi32
  advapi32
)

if(WREN_WARNINGS_AS_ERRORS)
  target_compile_options(wren PRIVATE /WX)
endif()

# ---- demo ----
if(WREN_BUILD_DEMO)
  add_executable(wren_demo WIN32 demo/wren_demo.cpp)
  target_link_libraries(wren_demo PRIVATE wren)
  target_compile_options(wren_demo PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
  set_target_properties(wren_demo PROPERTIES OUTPUT_NAME "wren-demo")

  if(WREN_WARNINGS_AS_ERRORS)
    target_compile_options(wren_demo PRIVATE /WX)
  endif()
endif()

# ---- 纯逻辑测试（不需要窗口） ----
if(WREN_BUILD_TESTS)
  add_executable(wren_tests tests/wren_tests.cpp)
  target_link_libraries(wren_tests PRIVATE wren)
  target_compile_options(wren_tests PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
  set_target_properties(wren_tests PROPERTIES OUTPUT_NAME "wren-tests")

  if(WREN_WARNINGS_AS_ERRORS)
    target_compile_options(wren_tests PRIVATE /WX)
  endif()

  if(WREN_TOPLEVEL)
    enable_testing()
    add_test(NAME wren COMMAND wren_tests)
  endif()
endif()
