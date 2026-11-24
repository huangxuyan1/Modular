cmake_minimum_required(VERSION 3.20)
project(Modular CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources contain UTF-8 comments; use the same encoding on every Windows locale.
if(MSVC)
  add_compile_options(/utf-8)
endif()

# No compiler fuses floating-point operations, so arithmetic gives the same bits in
# every build (docs/project-foundations.md). MSVC's default /fp:precise never contracts;
# GCC and every Clang, Emscripten's included, are told not to. clang-cl, which defaults
# to contracting, takes the option through /clang:.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(MSVC)
    add_compile_options(/clang:-ffp-contract=off)
  else()
    add_compile_options(-ffp-contract=off)
  endif()
endif()

# The web build (docs/web.md). Every target compiles and links with native WebAssembly
# exceptions, in the legacy encoding Emscripten 6.0.13 uses by default: MIDI parsing and the
# nodes throw inside the module, and its exports catch. Release optimizes at -O2, the level
# the WebAssembly probe measured, rather than CMake's -O3. No threads, fast maths or relaxed
# SIMD, all of which are off unless asked for.
if(EMSCRIPTEN)
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
  string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  # Node runs every test. The web preset names emsdk's own, from EMSDK_NODE.
  if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(FATAL_ERROR "No Node to run the tests: set up emsdk's environment, which sets "
      "EMSDK_NODE, then configure again")
  endif()
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

# SoundFont 2 instruments: the reader and the soundfont node. Only the header-only
# node library is linked, so a pipeline with no music in it links none of this.
add_library(modular_soundfont nodes/src/SoundFont.cpp nodes/src/SoundFontNode.cpp)
target_link_libraries(modular_soundfont PUBLIC modular_nodes)
# Graph persistence, which already carries the MIDI codecs, carries the soundfont
# codec too, behind soundFontCodecs() rather than primitiveCodecs().
target_link_libraries(modular_primitive_persistence PUBLIC modular_soundfont)

# --- web: the render surface's C++ core ---
# A client hands it a soundfont's bytes once and MIDI bytes per render. It builds
# on every platform, so the native suite tests the code the web module exports;
# the module itself, which needs Emscripten, is not built here.
add_library(modular_web_core apps/web/Instrument.cpp)
target_include_directories(modular_web_core PUBLIC apps/web)
target_link_libraries(modular_web_core PUBLIC modular_kernel modular_midi modular_soundfont)

# The web module and its loader, built only under Emscripten. A client takes the files in
# dist/: the hand-written loader and its declarations, the module's glue and .wasm, and the
# manifest the build writes beside them.
if(EMSCRIPTEN)
  set(MODULAR_WEB_DIST "${CMAKE_CURRENT_BINARY_DIR}/dist")
  # How the module links: an ES6 factory for pages and Node, whose memory grows as a render
  # needs. The test module below links the same way.
  set(MODULAR_WEB_LINK_OPTIONS
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,UTF8ToString)
  # The module's version: the commit, read each time the module is built, not only when
  # CMake configures, and the Emscripten version. The stamp rewrites its files only when
  # they change, so an unchanged tree relinks nothing.
  find_package(Git REQUIRED)
  set(MODULAR_WEB_VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/web-version")
  set(MODULAR_WEB_VERSION_FILE "${MODULAR_WEB_VERSION_DIR}/WebVersion.cmake")
  add_custom_target(modular_web_version
    COMMAND "${CMAKE_COMMAND}"
      "-DGIT=${GIT_EXECUTABLE}"
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DEMSCRIPTEN_VERSION=${EMSCRIPTEN_VERSION}"
      "-DOUTPUT_DIR=${MODULAR_WEB_VERSION_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/apps/web/stamp_version.cmake"
    BYPRODUCTS "${MODULAR_WEB_VERSION_DIR}/WebVersion.h" "${MODULAR_WEB_VERSION_FILE}"
    COMMENT "Reading the commit the web module is built from"
    VERBATIM)
  set(MODULAR_WEB_SOURCES apps/web/Exports.cpp apps/web/Version.cpp)
  add_executable(modular_web ${MODULAR_WEB_SOURCES})
  add_dependencies(modular_web modular_web_version)
  target_include_directories(modular_web PRIVATE "${MODULAR_WEB_VERSION_DIR}")
  target_link_libraries(modular_web PRIVATE modular_web_core)
  set_target_properties(modular_web PROPERTIES
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${MODULAR_WEB_DIST}")
  target_link_options(modular_web PRIVATE ${MODULAR_WEB_LINK_OPTIONS})
  configure_file(apps/web/modular.mjs "${MODULAR_WEB_DIST}/modular.mjs" COPYONLY)
  configure_file(apps/web/modular.d.mts "${MODULAR_WEB_DIST}/modular.d.mts" COPYONLY)

  # The manifest beside them, written by the build so a local build and CI's artifact carry
  # the same one. Its flags are those CMake passes, composed in the order CMake puts them on
  # the command line: every source in the module compiles with the first, and the module
  # links with the second. The project leaves C++ extensions on, CMake's default, so the
  # standard is the extension form, -std=gnu++20.
  string(TOUPPER "${CMAKE_BUILD_TYPE}" web_config)
  set(web_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${web_config}}")
  set(web_standard "${CMAKE_CXX${CMAKE_CXX_STANDARD}_EXTENSION_COMPILE_OPTION}")
  set(web_compile_options "$<JOIN:$<TARGET_PROPERTY:modular_web,COMPILE_OPTIONS>, >")
  set(web_linker_flags
    "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS_${web_config}}")
  set(web_link_options "$<JOIN:$<TARGET_PROPERTY:modular_web,LINK_OPTIONS>, >")
  set(MODULAR_WEB_MANIFEST "${MODULAR_WEB_DIST}/modular.manifest.json")
  add_custom_command(OUTPUT "${MODULAR_WEB_MANIFEST}"
    COMMAND "${CMAKE_COMMAND}"
      "-DDIST=${MODULAR_WEB_DIST}"
      "-DVERSION_FILE=${MODULAR_WEB_VERSION_FILE}"
      "-DCOMPILE_FLAGS=${web_flags} ${web_standard} ${web_compile_options}"
      "-DLINK_FLAGS=${web_flags} ${web_linker_flags} ${web_link_options}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/apps/web/write_manifest.cmake"
    DEPENDS modular_web "${MODULAR_WEB_DIST}/modular.mjs" "${MODULAR_WEB_DIST}/modular.d.mts"
      "${MODULAR_WEB_VERSION_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/apps/web/write_manifest.cmake"
    COMMENT "Writing the web module's manifest"
    VERBATIM)
  add_custom_target(modular_web_manifest ALL DEPENDS "${MODULAR_WEB_MANIFEST}")

  # The test module: the module's sources, version and link options, and its loader, built
  # for the smoke test only into its own folder, never into dist/, whose files the manifest
  # lists. tests/web_test_module.cpp traps on a huge allocation and reports the heap in use,
  # and tests/web_test_module.js hands each instance to the test.
  set(MODULAR_WEB_TEST_MODULE "${CMAKE_CURRENT_BINARY_DIR}/web-test-module")
  add_executable(modular_web_test ${MODULAR_WEB_SOURCES} tests/web_test_module.cpp)
  add_dependencies(modular_web_test modular_web_version)
  target_include_directories(modular_web_test PRIVATE "${MODULAR_WEB_VERSION_DIR}")
  target_link_libraries(modular_web_test PRIVATE modular_web_core)
  set_target_properties(modular_web_test PROPERTIES
    OUTPUT_NAME modular_web
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${MODULAR_WEB_TEST_MODULE}"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_test_module.js")
  target_link_options(modular_web_test PRIVATE ${MODULAR_WEB_LINK_OPTIONS}
    "SHELL:--post-js \"${CMAKE_CURRENT_SOURCE_DIR}/tests/web_test_module.js\"")
  configure_file(apps/web/modular.mjs "${MODULAR_WEB_TEST_MODULE}/modular.mjs" COPYONLY)
endif()

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

# Sets `out` to the arguments that leave the named doctest cases out under Node, where the
# web build cannot run them; natively it is empty. docs/web.md gives each case's reason.
function(modular_web_exclusion_arguments out)
  set(arguments "")
  if(EMSCRIPTEN AND ARGN)
    list(JOIN ARGN "," names)
    set(arguments "--test-case-exclude=${names}")
  endif()
  set(${out} "${arguments}" PARENT_SCOPE)
endfunction()

add_executable(kernel_tests tests/test_kernel.cpp)
target_include_directories(kernel_tests PRIVATE third_party/doctest)
target_link_libraries(kernel_tests PRIVATE modular_nodes)
add_test(NAME kernel_tests COMMAND kernel_tests)

add_executable(node_tests tests/test_nodes.cpp)
target_include_directories(node_tests PRIVATE third_party/doctest)
target_link_libraries(node_tests PRIVATE modular_nodes modular_recorder)
set(node_excluded_cases "")
if(CMAKE_HOST_WIN32)
  # Node on a Windows host cannot remove a directory through the C library's remove().
  # CI runs these on Linux, where they pass.
  set(node_excluded_cases
    "duplicate triggers and Stop ties preserve distinct numbered takes across blocks"
    "a partial external write is cleaned and Retry preserves existing output")
endif()
modular_web_exclusion_arguments(node_arguments ${node_excluded_cases})
add_test(NAME node_tests COMMAND node_tests ${node_arguments})

add_executable(authoring_tests tests/test_authoring.cpp)
target_include_directories(authoring_tests PRIVATE third_party/doctest)
target_link_libraries(authoring_tests PRIVATE modular_nodes modular_workspace
  modular_primitive_persistence modular_midi)
target_compile_definitions(authoring_tests PRIVATE
  MODULAR_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
if(WIN32)
  target_link_libraries(authoring_tests PRIVATE modular_windows_audio)
endif()
# The web build has no threads, so a graph cannot run on a worker.
set(authoring_excluded_cases
  "a client can run and release a graph on a worker before editing and running again"
  "a worker reports a processing boundary failure and releases execution")
if(CMAKE_HOST_WIN32)
  # As for node_tests: no directory removal under Node on a Windows host.
  list(APPEND authoring_excluded_cases
    "shared file save replaces complete graphs at Unicode paths and retains files on failure"
    "recording graphs reopen with owned MIDI and settings but no transient take")
endif()
modular_web_exclusion_arguments(authoring_arguments ${authoring_excluded_cases})
add_test(NAME authoring_tests COMMAND authoring_tests ${authoring_arguments})

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

add_executable(soundfont_tests tests/test_soundfont.cpp)
target_include_directories(soundfont_tests PRIVATE third_party/doctest)
target_link_libraries(soundfont_tests PRIVATE modular_soundfont)
add_test(NAME soundfont_tests COMMAND soundfont_tests)

# The soundfont extract tool, scripts/soundfont_extract.py, checked through its command
# line against renders by the node. Native only: the test's CMake script runs the
# helper executable directly, with no cross-compiling emulator.
if(NOT CMAKE_CROSSCOMPILING)
  find_package(Python3 3.8 COMPONENTS Interpreter)
  add_executable(soundfont_extract_helper tests/soundfont_extract_helper.cpp)
  target_link_libraries(soundfont_extract_helper PRIVATE modular_soundfont)
  add_test(NAME soundfont_extract COMMAND "${CMAKE_COMMAND}"
    "-DPYTHON=${Python3_EXECUTABLE}"
    "-DTOOL=${CMAKE_CURRENT_SOURCE_DIR}/scripts/soundfont_extract.py"
    "-DHELPER=$<TARGET_FILE:soundfont_extract_helper>"
    "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/soundfont-extract-tests"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_soundfont_extract.cmake")
endif()

add_executable(web_core_tests tests/test_web_core.cpp tests/test_web_golden.cpp)
target_include_directories(web_core_tests PRIVATE third_party/doctest)
target_link_libraries(web_core_tests PRIVATE modular_web_core)
add_test(NAME web_core_tests COMMAND web_core_tests)

# The web module's smoke test, the loader and module driven from Node, and the web check
# page, which a browser opens by hand (docs/web.md). The build writes their fixtures with
# the test-side builders: the smoke test's soundfont, and the golden preview's soundfont,
# MIDI bytes and golden values (WebGoldenClips.h) into the page's folder. The writer links
# Node's raw file system and runs under Node, the toolchain's emulator. The page's folder,
# web-check/, also takes a copy of the module's files from dist/, so it can be served alone.
if(EMSCRIPTEN)
  add_executable(web_smoke_fixture tests/web_smoke_fixture.cpp)
  target_link_options(web_smoke_fixture PRIVATE -sNODERAWFS=1)
  set(MODULAR_WEB_SMOKE_SOUNDFONT "${CMAKE_CURRENT_BINARY_DIR}/web-smoke.sf2")
  set(MODULAR_WEB_CHECK "${CMAKE_CURRENT_BINARY_DIR}/web-check")
  set(web_check_fixtures preview.sf2 preview.mid preview.json)
  list(TRANSFORM web_check_fixtures PREPEND "${MODULAR_WEB_CHECK}/")
  add_custom_command(OUTPUT "${MODULAR_WEB_SMOKE_SOUNDFONT}" ${web_check_fixtures}
    COMMAND web_smoke_fixture "${MODULAR_WEB_SMOKE_SOUNDFONT}" "${MODULAR_WEB_CHECK}"
    DEPENDS web_smoke_fixture
    COMMENT "Writing the web smoke test's and check page's fixtures"
    VERBATIM)
  foreach(page index.html check.mjs golden_hash.mjs)
    configure_file("tests/web/${page}" "${MODULAR_WEB_CHECK}/${page}" COPYONLY)
  endforeach()
  set(web_check_module_files
    modular.mjs modular.d.mts modular_web.mjs modular_web.wasm modular.manifest.json)
  set(web_check_module_sources ${web_check_module_files})
  list(TRANSFORM web_check_module_sources PREPEND "${MODULAR_WEB_DIST}/")
  list(TRANSFORM web_check_module_files PREPEND "${MODULAR_WEB_CHECK}/modular/")
  add_custom_command(OUTPUT ${web_check_module_files}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MODULAR_WEB_CHECK}/modular"
    COMMAND "${CMAKE_COMMAND}" -E copy ${web_check_module_sources}
      "${MODULAR_WEB_CHECK}/modular"
    DEPENDS "${MODULAR_WEB_MANIFEST}"
    COMMENT "Copying the web module beside the check page"
    VERBATIM)
  add_custom_target(modular_web_check ALL
    DEPENDS "${MODULAR_WEB_SMOKE_SOUNDFONT}" ${web_check_fixtures} ${web_check_module_files})
  add_dependencies(modular_web_check modular_web_manifest)

  add_test(NAME web_smoke COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_web_smoke.mjs"
    "${MODULAR_WEB_DIST}/modular.mjs" "${MODULAR_WEB_SMOKE_SOUNDFONT}"
    "${MODULAR_WEB_TEST_MODULE}/modular.mjs"
    "${MODULAR_WEB_CHECK}")
endif()

# Native only: the test's CMake script runs the CLI directly, with no cross-compiling
# emulator.
if(NOT CMAKE_CROSSCOMPILING)
  add_test(NAME midi_cli_smoke COMMAND "${CMAKE_COMMAND}"
    "-DCLI=$<TARGET_FILE:midi_cli>"
    "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/one-note.mid"
    "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/midi-cli-tests"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_midi_cli.cmake")
endif()

# Under Node, every doctest executable grows its memory as the module does, since the
# soundfont tests need more than the default 16 MiB. Those that read or write files also
# link Node's raw file system, so they see the host's files as the native tests do.
if(EMSCRIPTEN)
  foreach(target kernel_tests node_tests authoring_tests midi_file_tests soundfont_tests
                 web_core_tests)
    target_link_options(${target} PRIVATE -sALLOW_MEMORY_GROWTH=1)
  endforeach()
  foreach(target node_tests authoring_tests midi_file_tests soundfont_tests)
    target_link_options(${target} PRIVATE -sNODERAWFS=1)
  endforeach()
endif()

# The layering rule the kernel exists to keep: it names no media concept.
# Checked on the source, so it is a script rather than a doctest case.
add_test(NAME kernel_purity
         COMMAND "${CMAKE_COMMAND}" -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_kernel_purity.cmake")

# The soundfont node's portable maths: its sources call no maths-library
# transcendental, only the portable ones in nodes/src/SoundFontMaths.h, so native
# and WebAssembly builds render the same bits. Checked on the source, like kernel
# purity.
add_test(NAME soundfont_portable_maths
         COMMAND "${CMAKE_COMMAND}" -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_soundfont_maths.cmake")
