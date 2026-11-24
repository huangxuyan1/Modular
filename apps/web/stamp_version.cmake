cmake_minimum_required(VERSION 3.20)

# Reads what the web module is built from, each time it is built rather than only when
# CMake configures, and writes it for the build. The modular_web_version target runs it on
# every build of the web preset (CMakeLists.txt):
#
#   cmake -DGIT=<git> -DSOURCE_DIR=<checkout> -DEMSCRIPTEN_VERSION=<version>
#         -DOUTPUT_DIR=<directory> -P stamp_version.cmake
#
# It writes two files, each only when what it records changes, so a build of an unchanged
# tree compiles and links nothing again:
# - WebVersion.h, which apps/web/Version.cpp compiles into the module;
# - WebVersion.cmake, which write_manifest.cmake reads, so the manifest says what the
#   module says.
#
# The tree is dirty when a tracked file has uncommitted changes, as the soundfont extract
# tool counts it. Untracked files are left out: the build names every source it compiles.
# With no commit to read, the build stops: a module must say what it was built from.

foreach(required GIT SOURCE_DIR EMSCRIPTEN_VERSION OUTPUT_DIR)
  if("${${required}}" STREQUAL "")
    message(FATAL_ERROR "web version: ${required} is not set")
  endif()
endforeach()
if(NOT EMSCRIPTEN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "web version: unexpected Emscripten version '${EMSCRIPTEN_VERSION}'")
endif()

execute_process(COMMAND "${GIT}" -C "${SOURCE_DIR}" rev-parse --verify HEAD
  RESULT_VARIABLE result OUTPUT_VARIABLE commit ERROR_VARIABLE error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT result EQUAL 0 OR NOT commit MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "web version: cannot read the commit the module is built from, "
    "so build it from a Git checkout: ${error}")
endif()

# --no-optional-locks: a build only reads the index, so it never holds Git's lock.
execute_process(
  COMMAND "${GIT}" --no-optional-locks -C "${SOURCE_DIR}" status --porcelain
    --untracked-files=no
  RESULT_VARIABLE result OUTPUT_VARIABLE changes ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web version: cannot read whether the tree has changes: ${error}")
endif()
if(changes STREQUAL "")
  set(dirty false)
else()
  set(dirty true)
endif()

function(write_if_changed path content)
  if(EXISTS "${path}")
    file(READ "${path}" current)
    if(current STREQUAL content)
      return()
    endif()
  endif()
  file(WRITE "${path}" "${content}")
endfunction()

write_if_changed("${OUTPUT_DIR}/WebVersion.h"
"// Written by apps/web/stamp_version.cmake each time the web module is built.
#pragma once

namespace modular::web::version {
inline constexpr char kCommit[] = \"${commit}\";
inline constexpr bool kDirty = ${dirty};
inline constexpr char kEmscripten[] = \"${EMSCRIPTEN_VERSION}\";
} // namespace modular::web::version
")
write_if_changed("${OUTPUT_DIR}/WebVersion.cmake"
"# Written by apps/web/stamp_version.cmake each time the web module is built.
set(MODULAR_WEB_COMMIT \"${commit}\")
set(MODULAR_WEB_DIRTY ${dirty})
set(MODULAR_WEB_EMSCRIPTEN \"${EMSCRIPTEN_VERSION}\")
")
