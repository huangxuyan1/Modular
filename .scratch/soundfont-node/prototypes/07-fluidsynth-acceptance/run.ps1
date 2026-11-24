# PROTOTYPE, throwaway: soundfont-node ticket 07. Renders the comparison clips through the
# soundfont node and through FluidSynth at 44.1 and 48 kHz, compares them, and times the
# render surface natively. Outside the CMake build: nothing here is a target, and no file
# outside .scratch/ changes.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File run.ps1 -Out <folder>
#     -FluidSynth <folder holding fluidsynth.exe> -SoundFont <FluidR3_GM.sf2>
#     [-Extract <single-preset extract.sf2>] [-Patch <a.patch>,<b.patch>...]
#
# Needs Visual Studio 2022 Build Tools, Python 3 with numpy, and the FluidSynth 2.x Windows
# release named in the ticket. -Out must be outside the repository: the clips, a modified copy
# of the soundfont, every build's renders and the timings go there. Rendered audio, FluidSynth
# and the soundfonts are never committed. -Extract adds the timings, which load the extract as
# midishare will. Each -Patch is a diagnosis, not a change: the harness is built again from a
# copy of the sources with that patch applied, renders the clips and is compared too. The
# checkout is not touched.
#
# Comparisons, each written to compare-<variant><suffix>.md:
#   (none)  clip.mid (velocities 64 to 127) against FluidSynth on the soundfont as it is;
#   -mod    clip.mid against FluidSynth on the copy whose file modulator makes FluidSynth apply
#           the node's velocity-to-cutoff curve (PROTOTYPE_add_modulator.py);
#   -low    low.mid (velocities 16 to 63) against FluidSynth on that copy;
#   -onset  onset.mid (events off FluidSynth's block grid) against FluidSynth on the file.
param(
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$FluidSynth,
  [Parameter(Mandatory = $true)][string]$SoundFont,
  [string]$Extract,
  [string]$Patch = '' # comma-separated, since -File passes an array as one string
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..\..\..')).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$fluid = Join-Path $FluidSynth 'fluidsynth.exe'
$rates = 44100, 48000

# --- clips ------------------------------------------------------------------------------
$clips = Join-Path $Out 'clips'
python -I (Join-Path $here 'PROTOTYPE_make_clips.py') $clips
if ($LASTEXITCODE -ne 0) { throw 'clip generation failed' }
python -I (Join-Path $root '.scratch\midishare-playback\prototypes\02-wasm-render\PROTOTYPE_make_clips.py') $clips | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'worst-case clip generation failed' }

# --- the harness, with MSVC and CMake's Release flags -------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

function Build-Harness([string]$srcRoot, [string]$dir) {
  New-Item -ItemType Directory -Force $dir | Out-Null
  $sources = @(Join-Path $here 'PROTOTYPE_render.cpp') +
    (Get-ChildItem (Join-Path $srcRoot 'kernel\src\*.cpp')).FullName +
    @('MidiFile.cpp', 'SoundFont.cpp', 'SoundFontNode.cpp' |
      ForEach-Object { Join-Path $srcRoot "nodes\src\$_" }) +
    @(Join-Path $srcRoot 'apps\web\Instrument.cpp')
  $includes = 'kernel\include', 'nodes\include', 'apps\web' |
    ForEach-Object { "/I`"$(Join-Path $srcRoot $_)`"" }
  $cl = "cl /nologo /std:c++20 /EHsc /GR /O2 /Ob2 /DNDEBUG /MD /utf-8 " +
    ($includes -join ' ') + " /Fo`"$dir\\`" /Fe`"$dir\PROTOTYPE_render.exe`" " +
    (($sources | ForEach-Object { "`"$_`"" }) -join ' ')
  cmd /c "`"$vcvars`" >nul 2>&1 && $cl >`"$dir\build.log`" 2>&1"
  if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $dir 'build.log'); throw "cl failed: $dir" }
  Join-Path $dir 'PROTOTYPE_render.exe'
}

$harness = Build-Harness $root (Join-Path $Out 'native')
$variants = [ordered]@{ node = $harness }
foreach ($file in ($Patch -split ',' | Where-Object { $_ })) {
  # A copy of the three source folders the harness compiles, patched there.
  $name = [IO.Path]::GetFileNameWithoutExtension($file)
  $patched = Join-Path $Out "src-$name"
  if (Test-Path $patched) { Remove-Item -Recurse -Force $patched }
  foreach ($dir in 'kernel', 'nodes', 'apps') {
    Copy-Item -Recurse (Join-Path $root $dir) (Join-Path $patched $dir)
  }
  # GNU patch from Git for Windows; -l tolerates the checkout's CRLF line endings.
  $patchExe = Join-Path (Split-Path (Split-Path (Get-Command git).Source)) 'usr\bin\patch.exe'
  & $patchExe -p1 -l -d $patched -i (Resolve-Path $file).Path
  if ($LASTEXITCODE -ne 0) { throw "patch failed: $file" }
  $variants[$name] = Build-Harness $patched (Join-Path $Out "native-$name")
}

# --- renders ----------------------------------------------------------------------------
& $variants['node'] ignored $SoundFont
$modulated = Join-Path $Out 'modulated.sf2'
python -I (Join-Path $here 'PROTOTYPE_add_modulator.py') $SoundFont $modulated 0 0
if ($LASTEXITCODE -ne 0) { throw 'modulated copy failed' }
& $fluid --version | Select-Object -First 1
# Each clip's node renders are <variant><suffix>-<rate>.wav.
$clipSuffix = [ordered]@{ clip = ''; low = '-low'; onset = '-onset' }
# FluidSynth's renders: its output name, the soundfont it plays and the clip.
$fluidJobs = @(
  @{ Name = 'fluidsynth'; Font = $SoundFont; Clip = 'clip' },
  @{ Name = 'fluidsynth-mod'; Font = $modulated; Clip = 'clip' },
  @{ Name = 'fluidsynth-mod-low'; Font = $modulated; Clip = 'low' },
  @{ Name = 'fluidsynth-onset'; Font = $SoundFont; Clip = 'onset' })
foreach ($rate in $rates) {
  foreach ($clip in $clipSuffix.Keys) {
    foreach ($name in $variants.Keys) {
      & $variants[$name] render $SoundFont 0 0 (Join-Path $clips "$clip.mid") $rate 1 `
        (Join-Path $Out "$name$($clipSuffix[$clip])-$rate.wav")
      if ($LASTEXITCODE -ne 0) { throw "render failed: $name $clip $rate" }
    }
  }
  # Offline (-F), no MIDI driver (-n), no shell (-i), reverb and chorus off, 32-bit float.
  foreach ($job in $fluidJobs) {
    & $fluid -n -i -R 0 -C 0 -r $rate -O float -T wav -o synth.lock-memory=0 `
      -F (Join-Path $Out "$($job.Name)-$rate.wav") $job.Font (Join-Path $clips "$($job.Clip).mid")
    if ($LASTEXITCODE -ne 0) { throw "fluidsynth failed: $($job.Name) $rate" }
  }
}

# --- comparison ---------------------------------------------------------------------------
# Each report's suffix, the clip compared and the FluidSynth render it is compared with.
$compareJobs = @(
  @{ Report = ''; Clip = 'clip'; Fluid = 'fluidsynth' },
  @{ Report = '-mod'; Clip = 'clip'; Fluid = 'fluidsynth-mod' },
  @{ Report = '-low'; Clip = 'low'; Fluid = 'fluidsynth-mod-low' },
  @{ Report = '-onset'; Clip = 'onset'; Fluid = 'fluidsynth-onset' })
foreach ($name in $variants.Keys) {
  foreach ($job in $compareJobs) {
    $report = Join-Path $Out "compare-$name$($job.Report).md"
    python -I (Join-Path $here 'PROTOTYPE_compare.py') (Join-Path $clips "$($job.Clip).json") `
      (Join-Path $Out "$name$($clipSuffix[$job.Clip])") (Join-Path $Out $job.Fluid) > $report
    if ($LASTEXITCODE -ne 0) { Get-Content $report; throw "compare failed: $name$($job.Report)" }
    "== compare-$name$($job.Report)"
    Get-Content $report | Where-Object { $_ -match '^(Global|\| (44100|48000) \||- )' }
  }
}

# --- timings ------------------------------------------------------------------------------
# The preview, then map ticket 02's short, worst-case and ten-minute clips.
if ($Extract) {
  & $harness time $SoundFont $Extract (Join-Path $clips 'preview.mid') `
    ('short', 'worst', 'long' | ForEach-Object { Join-Path $clips "$_.mid" })
  if ($LASTEXITCODE -ne 0) { throw 'timings failed' }
}
