# The soundfont extract tool (scripts/soundfont_extract.py), through its command line.
# The native helper writes self-made soundfonts and renders through SoundFontNode, so
# no licensed file enters the tests. Inputs: PYTHON, TOOL, HELPER and WORK_ROOT.
if(NOT PYTHON)
  message(FATAL_ERROR "CMake found no Python 3 interpreter, which this test needs")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(work "${WORK_ROOT}/${run_id}")
file(MAKE_DIRECTORY "${work}")

function(run_tool result output error)
  execute_process(COMMAND "${PYTHON}" -I "${TOOL}" ${ARGN}
    RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e ENCODING UTF-8)
  set(${result} "${r}" PARENT_SCOPE)
  set(${output} "${o}" PARENT_SCOPE)
  set(${error} "${e}" PARENT_SCOPE)
endfunction()

# The tool's `preset` or `contents` printout of one file, which must succeed.
function(read_with_tool output)
  run_tool(result text error ${ARGN})
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "The tool could not read ${ARGN} (${result}): ${error}")
  endif()
  set(${output} "${text}" PARENT_SCOPE)
endfunction()

function(require_match text pattern what)
  if(NOT text MATCHES "${pattern}")
    message(FATAL_ERROR "${what}: no match for '${pattern}' in:\n${text}")
  endif()
endfunction()

execute_process(COMMAND "${HELPER}" write "${work}" RESULT_VARIABLE result
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "The helper could not write the soundfonts (${result}): ${error}")
endif()
set(notice_text "Copyright (c) 2026 A Test Author\n\nPermission is granted to test.\n")
file(WRITE "${work}/notice.txt" "${notice_text}")
set(url "https://example.invalid/source.sf2")
set(common --notice "${work}/notice.txt" --source-url "${url}" --source-version "test 1")

# Preset 1:5 is not the file's lowest: its bags, generators, modulators,
# instruments and samples all sit after another preset's.
run_tool(result output error extract "${work}/source.sf2" --preset 1:5 ${common}
  --output "${work}/extract.sf2")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "The tool refused the source (${result}): ${output}${error}")
endif()
file(SHA256 "${work}/extract.sf2" extract_hash)
require_match("${output}" "SHA-256 ${extract_hash}\n" "The tool's printed SHA-256")

# The node renders the preset from both files bit for bit, at 44.1 and 48 kHz.
execute_process(COMMAND "${HELPER}" compare "${work}/source.sf2" 1 5 "${work}/extract.sf2"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "The extract renders differently (${result}):\n${output}${error}")
endif()
message(STATUS "Renders:\n${output}")

# The tool's reader prints the preset's zones, generators, modulators and
# sample points with every index resolved: the same in both files.
read_with_tool(source_preset preset "${work}/source.sf2" 1:5)
read_with_tool(extract_preset preset "${work}/extract.sf2" 1:5)
if(NOT source_preset STREQUAL extract_preset)
  message(FATAL_ERROR
    "The preset differs.\nSource:\n${source_preset}\nExtract:\n${extract_preset}")
endif()
require_match("${source_preset}" "instrument\\[1\\] \"inst-stereo\"" "The preset")
require_match("${source_preset}"
  "sample\\[3\\] \"right\" points=2503 sha256=[0-9a-f]+ loop=\\+110\\.\\.\\+818"
  "The preset's samples")
require_match("${source_preset}"
  "generators 48=0x0028 8=0xfed4 16=0x0064; modulators 02053000"
  "The preset's global zone")

# One preset, the two instruments it names and the four samples they play:
# 3001 + 2503 + 2503 + 1801 points, each followed by 46 zero points. The pair
# stays linked; the link to a sample left out is cleared. The comment field
# holds the notice, then the provenance in order, naming the tool's commit.
find_program(git_command git REQUIRED)
get_filename_component(tool_dir "${TOOL}" DIRECTORY)
execute_process(COMMAND "${git_command}" -C "${tool_dir}" rev-parse HEAD
  OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE)
file(SHA256 "${work}/source.sf2" source_hash)
read_with_tool(contents contents "${work}/extract.sf2")
string(CONCAT expected
  "^version 2\\.04\n"
  "presets 1, instruments 2, samples 4, sample points 9992\n"
  "isng \\([0-9]+ bytes\\):\nTest Engine\n"
  "INAM \\([0-9]+ bytes\\):\ntarget\n"
  "ICOP \\([0-9]+ bytes\\):\nA Bank Author 2026\n"
  "ICMT \\([0-9]+ bytes\\):\n"
  "Copyright \\(c\\) 2026 A Test Author\n\nPermission is granted to test\\.\n\n"
  "This file holds one preset of the source below, copied by Modular's\n"
  "scripts/soundfont_extract\\.py\\.\n"
  "Source URL: https://example\\.invalid/source\\.sf2\n"
  "Source version: test 1\n"
  "Source SHA-256: ${source_hash}\n"
  "Preset: bank 1, program 5, \"target\"\n"
  "Modular commit: ${head}( \\(dirty\\))?\n\n"
  "preset 1:5 \"target\"\n"
  "sample 0 \"shared\" type=0x0001 link=0\n"
  "sample 1 \"left\" type=0x0004 link=2\n"
  "sample 2 \"right\" type=0x0002 link=1\n"
  "sample 3 \"linked-away\" type=0x0004 link=0\n$")
require_match("${contents}" "${expected}" "The extract's contents")

# A source with no copyright line or sound engine of its own takes the
# notice's first line and the specification's default engine.
run_tool(result output error extract "${work}/plain.sf2" --preset 1:5 ${common}
  --output "${work}/plain-extract.sf2")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "The tool refused the plain source (${result}): ${output}${error}")
endif()
read_with_tool(contents contents "${work}/plain-extract.sf2")
string(CONCAT expected
  "\nisng \\([0-9]+ bytes\\):\nEMU8000\nINAM \\([0-9]+ bytes\\):\ntarget\n"
  "ICOP \\([0-9]+ bytes\\):\nCopyright \\(c\\) 2026 A Test Author\n")
require_match("${contents}" "${expected}" "A plain source's extract")

# The same inputs give the same bytes; an existing output is never replaced.
run_tool(result output error extract "${work}/source.sf2" --preset 1:5 ${common}
  --output "${work}/extract-again.sf2")
file(SHA256 "${work}/extract-again.sf2" again_hash)
if(NOT result EQUAL 0 OR NOT again_hash STREQUAL extract_hash)
  message(FATAL_ERROR "A second run gave other bytes (${result}): ${output}${error}")
endif()
run_tool(result output error extract "${work}/source.sf2" --preset 1:5 ${common}
  --output "${work}/extract.sf2")
file(SHA256 "${work}/extract.sf2" after_hash)
if(result EQUAL 0 OR NOT error MATCHES "output already exists"
   OR NOT after_hash STREQUAL extract_hash)
  message(FATAL_ERROR "The tool must refuse to replace an existing output")
endif()

# Each refusal exits non-zero with its message and writes nothing.
string(REPEAT "x" 65536 long_notice)
file(WRITE "${work}/long-notice.txt" "${long_notice}")
set(long --notice "${work}/long-notice.txt" --source-url "${url}"
  --source-version "test 1")
foreach(case
    "sf3|1:5|common|a SoundFont 3 file"
    "compressed|1:5|common|a compressed sample"
    "sm24|1:5|common|24-bit sample data \\(sm24\\)"
    "source|3:3|common|no preset at bank 3 program 3"
    "truncated|1:5|common|truncated chunk"
    "bad-index|1:5|common|sample index out of range"
    "source|1:5|long|too long for the comment field \\(ICMT")
  string(REPLACE "|" ";" case "${case}")
  list(GET case 0 file)
  list(GET case 1 preset)
  list(GET case 2 notice)
  list(GET case 3 message)
  set(output_file "${work}/refused-${file}-${notice}.sf2")
  run_tool(result output error extract "${work}/${file}.sf2" --preset ${preset}
    ${${notice}} --output "${output_file}")
  if(result EQUAL 0 OR NOT error MATCHES "${message}" OR EXISTS "${output_file}")
    message(FATAL_ERROR
      "Expected ${file}.sf2 ${preset} to be refused with '${message}' and nothing "
      "written; got ${result}: ${output}${error}")
  endif()
endforeach()
