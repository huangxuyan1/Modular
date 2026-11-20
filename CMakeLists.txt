cmake_minimum_required(VERSION 3.20)
project(Modular CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources contain UTF-8 comments; use the same encoding on every Windows locale.
if(MSVC)
  add_compile_options(/utf-8)
endif()

# --- kernel: pure graph runtime, no framework dependencies ---
add_library(modular_kernel
  kernel/src/Node.cpp
  kernel/src/Graph.cpp
  kernel/src/Engine.cpp
  kernel/src/Supernode.cpp
  kernel/src/Flatten.cpp
)
target_include_directories(modular_kernel PUBLIC kernel/include)

# --- nodes: the beginning of a standard library; user-space, header-only ---
add_library(modular_nodes INTERFACE)
target_include_directories(modular_nodes INTERFACE nodes/include)
target_link_libraries(modular_nodes INTERFACE modular_kernel)

add_library(modular_recorder nodes/src/WavRecorderNode.cpp)
target_link_libraries(modular_recorder PUBLIC modular_nodes)

# Shared authoring and presentation, with no GUI dependency.
add_library(modular_workspace workspace/src/GraphPersistence.cpp workspace/src/GraphFiles.cpp)
target_include_directories(modular_workspace PUBLIC workspace/include)
target_link_libraries(modular_workspace PUBLIC modular_kernel)

add_library(modular_primitive_persistence nodes/src/PrimitivePersistence.cpp)
target_link_libraries(modular_primitive_persistence PUBLIC modular_nodes modular_workspace
  modular_recorder)

# File parsing and hardware adapters stay in the node library, outside the kernel.
add_library(modular_midi nodes/src/MidiFile.cpp)
target_link_libraries(modular_midi PUBLIC modular_nodes)
target_link_libraries(modular_primitive_persistence PUBLIC modular_midi)

if(WIN32)
  add_library(modular_windows_audio nodes/src/AudioOutputNode.cpp)
  target_link_libraries(modular_windows_audio PUBLIC modular_nodes PRIVATE winmm)
  target_link_libraries(modular_primitive_persistence PUBLIC modular_windows_audio)
endif()

# --- milestone 1: offline render CLI ---
add_executable(render_cli apps/render_cli/main.cpp)
target_link_libraries(render_cli PRIVATE modular_nodes)

add_executable(midi_cli apps/midi_cli/main.cpp)
target_link_libraries(midi_cli PRIVATE modular_midi)
if(WIN32)
  target_link_libraries(midi_cli PRIVATE modular_windows_audio)

  add_executable(graph_editor WIN32
    apps/graph_editor/main.cpp
    apps/graph_editor/EditorWindow.cpp
    apps/graph_editor/PrimitivePalette.cpp)
  target_link_libraries(graph_editor PRIVATE modular_workspace modular_midi
    modular_windows_audio modular_primitive_persistence user32 gdi32 comdlg32 comctl32)
  target_compile_definitions(graph_editor PRIVATE UNICODE _UNICODE)
endif()

# --- tests ---
enable_testing()
add_executable(kernel_tests tests/test_kernel.cpp)
target_include_directories(kernel_tests PRIVATE third_party/doctest)
target_link_libraries(kernel_tests PRIVATE modular_nodes)
add_test(NAME kernel_tests COMMAND kernel_tests)

add_executable(node_tests tests/test_nodes.cpp)
target_include_directories(node_tests PRIVATE third_party/doctest)
target_link_libraries(node_tests PRIVATE modular_nodes modular_recorder)
add_test(NAME node_tests COMMAND node_tests)

add_executable(authoring_tests tests/test_authoring.cpp)
target_include_directories(authoring_tests PRIVATE third_party/doctest)
target_link_libraries(authoring_tests PRIVATE modular_nodes modular_workspace
  modular_primitive_persistence modular_midi)
target_compile_definitions(authoring_tests PRIVATE
  MODULAR_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
if(WIN32)
  target_link_libraries(authoring_tests PRIVATE modular_windows_audio)
endif()
add_test(NAME authoring_tests COMMAND authoring_tests)

if(WIN32)
  add_executable(graph_editor_ui_tests tests/test_graph_editor_app.cpp)
  target_link_libraries(graph_editor_ui_tests PRIVATE user32 gdi32 ole32)
  target_compile_definitions(graph_editor_ui_tests PRIVATE
    MODULAR_TEST_MIDI=L"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/one-note.mid")
  add_dependencies(graph_editor_ui_tests graph_editor)
  add_test(NAME graph_editor_app COMMAND graph_editor_ui_tests
    "$<TARGET_FILE:graph_editor>"
    "${CMAKE_CURRENT_BINARY_DIR}/graph-editor-$<CONFIG>.bmp")
  set_tests_properties(graph_editor_app PROPERTIES TIMEOUT 30)
  add_test(NAME graph_editor_gesture COMMAND graph_editor_ui_tests
    "$<TARGET_FILE:graph_editor>" --gesture)
  set_tests_properties(graph_editor_gesture PROPERTIES TIMEOUT 10)
  # Both open a real window: hosted CI excludes them with --label-exclude desktop.
  set_tests_properties(graph_editor_app graph_editor_gesture PROPERTIES LABELS desktop)
endif()

add_executable(midi_file_tests tests/test_midi_file.cpp)
target_include_directories(midi_file_tests PRIVATE third_party/doctest)
target_link_libraries(midi_file_tests PRIVATE modular_midi)
target_compile_definitions(midi_file_tests PRIVATE
  MODULAR_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME midi_file_tests COMMAND midi_file_tests)

add_test(NAME midi_cli_smoke COMMAND "${CMAKE_COMMAND}"
  "-DCLI=$<TARGET_FILE:midi_cli>"
  "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/one-note.mid"
  "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/midi-cli-tests"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_midi_cli.cmake")

# The layering rule the kernel exists to keep: it names no media concept.
# Checked on the source, so it is a script rather than a doctest case.
add_test(NAME kernel_purity
         COMMAND "${CMAKE_COMMAND}" -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_kernel_purity.cmake")
