# PROTOTYPE, throwaway: builds midishare-playback ticket 02's probe. Outside the CMake build
# on purpose: nothing here is a target, and no file outside .scratch/ changes.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Out <folder> [-Patch]
#
# Needs Emscripten 6.0.13 (ticket 01's emsdk folder in the user's home folder) and Visual
# Studio 2022 Build Tools. Writes into -Out, which must be outside the repository:
#
#   clips\        the test clips, from PROTOTYPE_make_clips.py
#   <variant>\    render.mjs + render.wasm (+ render.js.symbols) for each exception mode and
#                 optimisation level
#   native\       render_native.exe, the same wrapper built with MSVC as CMake's Release does
#
# -Patch builds from a copy of kernel\ and nodes\ with each patches\*.patch applied, made in
# <Out>\patched-src, instead of from the checkout. No patch is needed to compile; the one
# there is optional (see results.md).
param(
  [Parameter(Mandatory = $true)][string]$Out,
  [switch]$Patch
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..\..\..')).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

python (Join-Path $here 'PROTOTYPE_make_clips.py') (Join-Path $Out 'clips')
if ($LASTEXITCODE -ne 0) { throw 'clip generation failed' }

$srcRoot = $root
if ($Patch) {
  # A temporary copy of the three source folders the probe compiles, patched there.
  $srcRoot = Join-Path $Out 'patched-src'
  if (Test-Path $srcRoot) { Remove-Item -Recurse -Force $srcRoot }
  foreach ($dir in 'kernel', 'nodes') {
    Copy-Item -Recurse (Join-Path $root $dir) (Join-Path $srcRoot $dir)
  }
  # GNU patch from Git for Windows; -l tolerates the checkout's CRLF line endings.
  $patchExe = Join-Path (Split-Path (Split-Path (Get-Command git).Source)) 'usr\bin\patch.exe'
  foreach ($p in Get-ChildItem (Join-Path $here 'patches\*.patch') | Sort-Object Name) {
    & $patchExe -p1 -l -d $srcRoot -i $p.FullName
    if ($LASTEXITCODE -ne 0) { throw "patch failed: $($p.Name)" }
  }
}

$sources = @(Join-Path $here 'PROTOTYPE_render.cpp') +
  (Get-ChildItem (Join-Path $srcRoot 'kernel\src\*.cpp')).FullName +
  @(Join-Path $srcRoot 'nodes\src\MidiFile.cpp')
$includes = @("-I$srcRoot\kernel\include", "-I$srcRoot\nodes\include")

# --- WebAssembly -----------------------------------------------------------------------
$env:EMSDK_QUIET = '1'
$ErrorActionPreference = 'Continue' # emsdk_env.ps1 reports on stderr
. (Join-Path $env:USERPROFILE 'emsdk\emsdk_env.ps1') *> $null
$ErrorActionPreference = 'Stop'
$common = @('-std=c++20', '-DNDEBUG', '-Wall', '-sMODULARIZE=1', '-sEXPORT_ES6=1',
  '-sENVIRONMENT=web,node', '-sALLOW_MEMORY_GROWTH=1', '--emit-symbol-map',
  '-sEXPORTED_FUNCTIONS=_malloc,_free')
$runtime = '-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,UTF8ToString'
# getExceptionMessage reads an uncaught C++ exception's type and what() from JavaScript; it
# links the C++ demangler, so the "-msg" variants show its cost separately.
$helpers = '-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,UTF8ToString,getExceptionMessage,decrementExceptionRefcount'
$modes = [ordered]@{
  'noexc'       = @($runtime)
  'jsexc'       = @('-fexceptions', $runtime)
  'wasmexc'     = @('-fwasm-exceptions', $runtime)
  'exnref'      = @('-fwasm-exceptions', '-sWASM_LEGACY_EXCEPTIONS=0', $runtime)
  'jsexc-msg'   = @('-fexceptions', $helpers)
  'wasmexc-msg' = @('-fwasm-exceptions', $helpers)
}
foreach ($opt in '-O2', '-Oz') {
  foreach ($mode in $modes.Keys) {
    if ($opt -eq '-Oz' -and $mode -like '*-msg') { continue }
    $name = $mode + $(if ($opt -eq '-Oz') { '-Oz' } else { '' })
    $dir = Join-Path $Out $name
    New-Item -ItemType Directory -Force $dir | Out-Null
    $log = Join-Path $dir 'build.log'
    $args = @($opt) + $common + $modes[$mode] + $includes + $sources + @('-o', (Join-Path $dir 'render.mjs'))
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $ErrorActionPreference = 'Continue' # warnings arrive on stderr; the exit code decides
    & em++ @args 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $log
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { Get-Content $log; throw "em++ failed for $name" }
    Write-Host ("{0,-11} built in {1,5:N1} s" -f $name, $timer.Elapsed.TotalSeconds)
  }
}
# The sine voice's maths alone, run under Node with the real file system for its dump.
$libm = Join-Path $Out 'libm'
New-Item -ItemType Directory -Force $libm | Out-Null
$ErrorActionPreference = 'Continue'
& em++ -std=c++20 -O2 -DNDEBUG -sNODERAWFS=1 (Join-Path $here 'PROTOTYPE_libm.cpp') `
  -o (Join-Path $libm 'libm.js') *> (Join-Path $libm 'build.log')
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'em++ failed for libm' }

# --- native reference, MSVC with CMake's Release flags ---------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$native = Join-Path $Out 'native'
New-Item -ItemType Directory -Force $native | Out-Null
$quoted = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$cl = "cl /nologo /std:c++20 /EHsc /GR /O2 /Ob2 /DNDEBUG /utf-8 " +
  "/I`"$srcRoot\kernel\include`" /I`"$srcRoot\nodes\include`" " +
  "/Fo`"$native\\`" /Fe`"$native\render_native.exe`" $quoted"
cmd /c "`"$vcvars`" >nul 2>&1 && $cl >`"$native\build.log`" 2>&1"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $native 'build.log'); throw 'native build failed' }
$cl = "cl /nologo /std:c++20 /EHsc /GR /O2 /Ob2 /DNDEBUG /utf-8 " +
  "/Fo`"$libm\\`" /Fe`"$libm\libm.exe`" `"$(Join-Path $here 'PROTOTYPE_libm.cpp')`""
cmd /c "`"$vcvars`" >nul 2>&1 && $cl >>`"$libm\build.log`" 2>&1"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $libm 'build.log'); throw 'native libm build failed' }
Write-Host 'native      built'
