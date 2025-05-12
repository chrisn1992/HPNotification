cmake_minimum_required(VERSION 3.29)

project(
  HPNotification
  VERSION 1.1
  LANGUAGES CXX
)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_library(minhook STATIC IMPORTED)
target_include_directories(minhook INTERFACE deps/minhook)
set_property(TARGET minhook PROPERTY IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/deps/minhook/libMinHook.x64.lib)

add_library(loader STATIC IMPORTED)
target_include_directories(loader INTERFACE deps/loader)
set_property(TARGET loader PROPERTY IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/deps/loader/loader.lib)

add_library(HPNotification SHARED src/dllmain.cpp "src/supplemental.cpp")

target_link_libraries(HPNotification PRIVATE minhook loader)
target_include_directories(HPNotification PRIVATE deps/json deps/loader deps/minhook)
target_compile_features(HPNotification PRIVATE cxx_std_20)
