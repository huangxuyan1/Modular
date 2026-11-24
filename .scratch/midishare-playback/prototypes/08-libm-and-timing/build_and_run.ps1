# PROTOTYPE, throwaway: midishare-playback ticket 08. Builds the two probes with MSVC (CMake's
# Release flags) and with em++ (-O2, as ticket 02's module), runs them, and compares.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File build_and_run.ps1 -Out <folder>
#
# Needs Emscripten 6.0.13 (ticket 01's emsdk folder in the user's home folder), Visual Studio
# 2022 Build Tools and Python 3. -Out must be outside the repository; nothing is written here.
param([Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$sweep = Join-Path $here 'PROTOTYPE_libm_sweep.cpp'
$frames = Join-Path $here 'PROTOTYPE_frames.cpp'

# --- WebAssembly, run under Node with the real file system for the sweep's output -------
$env:EMSDK_QUIET = '1'
$ErrorActionPreference = 'Continue' # emsdk_env.ps1 reports on stderr
. (Join-Path $env:USERPROFILE 'emsdk\emsdk_env.ps1') *> $null
$ErrorActionPreference = 'Stop'
$wasm = Join-Path $Out 'wasm'
New-Item -ItemType Directory -Force $wasm | Out-Null
foreach ($src in $sweep, $frames) {
  $name = [IO.Path]::GetFileNameWithoutExtension($src)
  $ErrorActionPreference = 'Continue'
  & em++ -std=c++20 -O2 -DNDEBUG -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 $src `
    -o (Join-Path $wasm "$name.js") *> (Join-Path $wasm "$name.build.log")
  $ErrorActionPreference = 'Stop'
  if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $wasm "$name.build.log"); throw "em++ failed: $name" }
}

# --- native, MSVC with CMake's Release flags -------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$native = Join-Path $Out 'native'
New-Item -ItemType Directory -Force $native | Out-Null
foreach ($src in $sweep, $frames) {
  $name = [IO.Path]::GetFileNameWithoutExtension($src)
  $cl = "cl /nologo /std:c++20 /EHsc /O2 /Ob2 /DNDEBUG /utf-8 " +
    "/Fo`"$native\\`" /Fe`"$native\$name.exe`" `"$src`""
  cmd /c "`"$vcvars`" >nul 2>&1 && $cl >`"$native\$name.build.log`" 2>&1"
  if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $native "$name.build.log"); throw "cl failed: $name" }
}

# --- run ----------------------------------------------------------------------------------
foreach ($run in 'msvc', 'msvc-nofma3', 'wasm') {
  New-Item -ItemType Directory -Force (Join-Path $Out $run) | Out-Null
}
& (Join-Path $native 'PROTOTYPE_libm_sweep.exe') (Join-Path $Out 'msvc') > (Join-Path $Out 'msvc\sweep.txt')
& (Join-Path $native 'PROTOTYPE_libm_sweep.exe') (Join-Path $Out 'msvc-nofma3') nofma3 > (Join-Path $Out 'msvc-nofma3\sweep.txt')
& $env:EMSDK_NODE (Join-Path $wasm 'PROTOTYPE_libm_sweep.js') (Join-Path $Out 'wasm') > (Join-Path $Out 'wasm\sweep.txt')
& (Join-Path $native 'PROTOTYPE_frames.exe') > (Join-Path $Out 'msvc\frames.txt')
& $env:EMSDK_NODE (Join-Path $wasm 'PROTOTYPE_frames.js') > (Join-Path $Out 'wasm\frames.txt')
& $env:EMSDK_NODE --version
python -I (Join-Path $here 'compare.py') $Out
