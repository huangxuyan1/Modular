# PROTOTYPE, throwaway: web build ticket 08's native harness. A project of its own, outside
# Modular's build: it builds Modular's render core with Modular's own settings, from the
# checkout two folders up, and links the harness to it. Configure it into a folder outside
# the repository:
#
#   cmake -S .scratch/web-build/prototypes/08-default-piano -B <folder> \
#     -G "Visual Studio 17 2022" -A x64
#   cmake --build <folder> --config Release --target piano_native
#
# Then, outside the repository: piano_native clip clip.mid; piano_native render <extract>
# clip.mid native.json 44100 48000; and node PROTOTYPE_piano_node.mjs with the built
# dist/modular.mjs, the extract, clip.mid and native.json.
cmake_minimum_required(VERSION 3.20)
project(PianoAcceptance CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

get_filename_component(MODULAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
add_subdirectory("${MODULAR_ROOT}" modular EXCLUDE_FROM_ALL)

add_executable(piano_native PROTOTYPE_piano_native.cpp)
target_include_directories(piano_native PRIVATE "${MODULAR_ROOT}/tests")
target_link_libraries(piano_native PRIVATE modular_web_core)
