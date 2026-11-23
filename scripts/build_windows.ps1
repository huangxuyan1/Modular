param(
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
  [string]$Configuration = 'Debug',
  [string]$Target = 'ALL_BUILD',
  [string]$TestRegex = '',
  # Tests labelled "desktop" open real windows on the user's screen; opt in.
  [switch]$IncludeDesktop
)

$ErrorActionPreference = 'Stop'

# Some agent shells inherit both Path and PATH. MSBuild's case-insensitive
# environment dictionary rejects that pair before it can launch the compiler.
$buildSearchPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $buildSearchPath, 'Process')

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
  & cmake --preset windows
  if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
  & cmake --build --preset windows --config $Configuration --target $Target --parallel
  if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
  $ctestArguments = @('--preset', 'windows', '--build-config', $Configuration)
  if ($TestRegex) { $ctestArguments += @('-R', $TestRegex) }
  if (-not $IncludeDesktop) { $ctestArguments += @('--label-exclude', 'desktop') }
  & ctest @ctestArguments
  if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
} finally {
  Pop-Location
}
