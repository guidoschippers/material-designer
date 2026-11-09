<#
.SYNOPSIS
    Builds the material-designer solution with MSBuild and optionally runs the tests.

.EXAMPLE
    ./build.ps1                         # Release build
    ./build.ps1 -Configuration Debug
    ./build.ps1 -Test                   # Release build + run all tests
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Test,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found. Install Visual Studio 2026 with the C++ desktop workload.' }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild (Join-Path $root 'material-designer.slnx') /m /nologo /v:minimal "/t:$target" "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    & (Join-Path $root "bin\x64\$Configuration\material-designer-tests.exe")
    exit $LASTEXITCODE
}
