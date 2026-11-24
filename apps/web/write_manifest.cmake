cmake_minimum_required(VERSION 3.20)

# Writes the web module's manifest, modular.manifest.json, beside the files a client takes,
# once they are built. The modular_web_manifest target runs it (CMakeLists.txt):
#
#   cmake -DDIST=<dist> -DVERSION_FILE=<WebVersion.cmake> -DCOMPILE_FLAGS=<flags>
#         -DLINK_FLAGS=<flags> -P write_manifest.cmake
#
# It records what the module reports as its version, the flags it was built with, and each
# shipped file's name, size and SHA-256. It holds no date and no path, so the same commit
# and toolchain write the same manifest. docs/web.md describes it.

foreach(required DIST VERSION_FILE)
  if("${${required}}" STREQUAL "")
    message(FATAL_ERROR "web manifest: ${required} is not set")
  endif()
endforeach()
include("${VERSION_FILE}")

# A JSON string's contents: backslashes and quotes escaped, runs of spaces made one.
function(json_text out value)
  string(STRIP "${value}" value)
  string(REGEX REPLACE "[ \t\r\n]+" " " value "${value}")
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

json_text(compile_flags "${COMPILE_FLAGS}")
json_text(link_flags "${LINK_FLAGS}")

set(entries "")
foreach(name modular.d.mts modular.mjs modular_web.mjs modular_web.wasm)
  if(NOT EXISTS "${DIST}/${name}")
    message(FATAL_ERROR "web manifest: ${DIST}/${name} was not built")
  endif()
  file(SIZE "${DIST}/${name}" size)
  file(SHA256 "${DIST}/${name}" sha256)
  list(APPEND entries
    "    { \"name\": \"${name}\", \"size\": ${size}, \"sha256\": \"${sha256}\" }")
endforeach()
list(JOIN entries ",\n" entries)

file(WRITE "${DIST}/modular.manifest.json" "{
  \"commit\": \"${MODULAR_WEB_COMMIT}\",
  \"dirty\": ${MODULAR_WEB_DIRTY},
  \"emscripten\": \"${MODULAR_WEB_EMSCRIPTEN}\",
  \"flags\": {
    \"compile\": \"${compile_flags}\",
    \"link\": \"${link_flags}\"
  },
  \"files\": [
${entries}
  ]
}
")
