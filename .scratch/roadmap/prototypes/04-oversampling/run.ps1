# PROTOTYPE — throwaway. One command to run the ticket-04 oversampling probe.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .scratch/roadmap/prototypes/04-oversampling/run.ps1
#
# Compiles the probe together with the shipped kernel sources (kernel/src/*.cpp)
# with MSVC from Visual Studio 2022 Build Tools, into a temporary directory,
# and runs it. Outside the CMake build on purpose: nothing here is a target.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..\..\..')).Path

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$out = Join-Path ([IO.Path]::GetTempPath()) ('modular-probe-04-' + [Guid]::NewGuid())
New-Item -ItemType Directory $out | Out-Null

$sources = @(Join-Path $here 'PROTOTYPE_probe.cpp') +
  (Get-ChildItem (Join-Path $root 'kernel\src\*.cpp')).FullName
$quoted = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$cl = "cl /nologo /std:c++20 /EHsc /O2 /utf-8 " +
  "/I`"$root\kernel\include`" /I`"$root\kernel\src`" /I`"$root\nodes\include`" " +
  "/Fo`"$out\\`" /Fe`"$out\probe.exe`" $quoted"

cmd /c "`"$vcvars`" >nul 2>&1 && $cl >`"$out\build.log`" 2>&1"
if ($LASTEXITCODE -ne 0) {
  Get-Content (Join-Path $out 'build.log')
  throw 'probe build failed'
}
& (Join-Path $out 'probe.exe')
if ($LASTEXITCODE -ne 0) { throw "probe exited with $LASTEXITCODE" }
