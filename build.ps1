# build.ps1 — Configure et compile le trainer HDPhase1 (Win32/x86 uniquement).
#
# Usage :
#   .\build.ps1                 # Release, Visual Studio 2026
#   .\build.ps1 -Debug          # Debug
#   .\build.ps1 -Preset vs2022-x86
#
# Binaires produits : trainer courant + HD_AI_AUTHORITY_HOST + HD_AI_AUTHORITY_CLIENT.
param(
    [switch]$Debug,
    [string]$Preset = 'vs2026-x86'
)

$ErrorActionPreference = 'Stop'

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$buildPreset = if ($Debug) { 'debug-x86' } else { 'release-x86' }
if ($Preset -eq 'vs2026-x86') {
    $buildPreset = if ($Debug) { 'debug-x86-vs2026' } else { 'release-x86-vs2026' }
}

cmake --build --preset $buildPreset --target HDPhase1 HDAuthorityHost HDAuthorityClient
exit $LASTEXITCODE
