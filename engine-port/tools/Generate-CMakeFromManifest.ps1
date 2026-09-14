[CmdletBinding()]
param(
    [Parameter()]
    [string] $SourceRoot,

    [Parameter()]
    [string] $ManifestPath,

    [Parameter()]
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
if (-not $SourceRoot) {
    $SourceRoot = Split-Path -Parent $PSScriptRoot
}
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $SourceRoot 'vc6-project-manifest.json'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $SourceRoot '_cmake'
}
if (-not (Test-Path -LiteralPath $ManifestPath)) {
    throw "Manifest VC6 introuvable: $ManifestPath"
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$byName = @{}
foreach ($project in $manifest.projects) {
    $byName[$project.name] = $project
}

function Get-TargetName([string] $Name) {
    return ('hd_' + ($Name -replace '[^A-Za-z0-9_]', '_')).ToLowerInvariant()
}

function Add-Closure([string] $ProjectName, [hashtable] $Visited) {
    if ($Visited.ContainsKey($ProjectName)) {
        return
    }
    if (-not $byName.ContainsKey($ProjectName)) {
        throw "Dependance VC6 absente du manifest: $ProjectName"
    }
    $Visited[$ProjectName] = $true
    foreach ($dependency in @($byName[$ProjectName].dependencies)) {
        Add-Closure $dependency $Visited
    }
}

$included = @{}
Add-Closure 'Hidden And Dangerous' $included
Add-Closure 'IEditor' $included
$orderedProjects = @($manifest.projects | Where-Object { $included.ContainsKey($_.name) })

$cmake = New-Object System.Collections.Generic.List[string]
$cmake.Add('cmake_minimum_required(VERSION 3.25)')
$cmake.Add('project(HDDeluxeAuthority LANGUAGES C CXX RC)')
$cmake.Add('')
$cmake.Add('# Generated from the original VC6 workspace. Do not edit by hand.')
$cmake.Add('set(CMAKE_CXX_STANDARD 14)')
$cmake.Add('set(CMAKE_CXX_STANDARD_REQUIRED OFF)')
$cmake.Add('set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")')
$cmake.Add('add_compile_definitions(WIN32 NDEBUG HD_CMAKE_PORT)')
$cmake.Add('# VC6 headers inject library names with #pragma comment(lib,...).')
$cmake.Add('# CMake expresses those dependencies as targets; suppress the stale')
$cmake.Add('# copied-library names so link order and locations stay deterministic.')
$cmake.Add('add_link_options(/NODEFAULTLIB:base_lib /NODEFAULTLIB:base_lib_d /NODEFAULTLIB:icom_rd /NODEFAULTLIB:iexcpt /NODEFAULTLIB:insanity /NODEFAULTLIB:insanity_d /NODEFAULTLIB:i3d2_math /NODEFAULTLIB:i3d2_math_d /NODEFAULTLIB:i3d2_thunk /NODEFAULTLIB:igraph2_thunk /NODEFAULTLIB:inet2_thunk /NODEFAULTLIB:isound2_thunk /NODEFAULTLIB:itabler2 /NODEFAULTLIB:ogg /NODEFAULTLIB:vorbis /NODEFAULTLIB:D3dx8 /NODEFAULTLIB:th32)')
$cmake.Add('')
$cmake.Add('set(HD_SOURCE_ROOT "' + $SourceRoot.Replace('\', '/') + '")')
$cmake.Add('# Minimal, local DirectX 8 SDK headers/import libraries for this source port.')
$cmake.Add('# They are kept in _vendor and are never copied into the installed game.')
$cmake.Add('set(HD_LEGACY_DXSDK_ROOT "${HD_SOURCE_ROOT}/_vendor/directx8_min" CACHE PATH "Local DirectX 8 compatibility SDK root")')
$cmake.Add('if(EXISTS "${HD_LEGACY_DXSDK_ROOT}/include/dplay8.h")')
$cmake.Add('  message(STATUS "Using local DirectX 8 compatibility SDK: ${HD_LEGACY_DXSDK_ROOT}")')
$cmake.Add('  include_directories("${HD_LEGACY_DXSDK_ROOT}/include")')
$cmake.Add('  if(EXISTS "${HD_LEGACY_DXSDK_ROOT}/lib")')
$cmake.Add('    link_directories("${HD_LEGACY_DXSDK_ROOT}/lib")')
$cmake.Add('  endif()')
$cmake.Add('else()')
$cmake.Add('  message(WARNING "DirectPlay headers absent. hd_inet2 cannot be built without the local compatibility SDK.")')
$cmake.Add('endif()')
$cmake.Add('include_directories("${HD_SOURCE_ROOT}/_src" "${HD_SOURCE_ROOT}/_src/Insanity/Include" "${HD_SOURCE_ROOT}/_src/Insanity/Lib/ISnd2/OggVorbis/include")')
$cmake.Add('')

foreach ($project in $orderedProjects) {
    $target = Get-TargetName $project.name
    $type = switch -Wildcard ($project.target_type) {
        '*Static Library*' { 'STATIC'; break }
        '*Dynamic-Link Library*' { 'SHARED'; break }
        '*Application*' { 'WIN32'; break }
        default { throw "Type VC6 inconnu pour $($project.name): $($project.target_type)" }
    }

    $cmake.Add('# ' + $project.name)
    $cmake.Add('set(' + $target + '_sources')
    foreach ($source in @($project.sources)) {
        $cmake.Add('  "' + $source.Replace('\', '/') + '"')
    }
    if ($project.name -eq 'inet2') {
        # DirectPlay Voice GUID definitions were historically supplied by
        # dvoice.lib.  Modern Windows no longer ships that import library,
        # so this source-only compatibility unit owns those GUIDs.
        $cmake.Add('  "' + (Join-Path $SourceRoot '_src/Insanity/Lib/INet2/LegacyDxGuids.cpp').Replace('\', '/') + '"')
    }
    $cmake.Add(')')
    if ($project.name -eq 'QHull') {
        # CMake interprets uppercase .C as C++ on Windows.  The historic
        # QHull sources export a C ABI consumed by Thunk.cpp, so force the
        # original C language for those units while retaining the C++ thunk.
        $cmake.Add('set_source_files_properties(')
        foreach ($source in @($project.sources | Where-Object { [IO.Path]::GetExtension($_) -ceq '.C' })) {
            $cmake.Add('  "' + $source.Replace('\', '/') + '"')
        }
        $cmake.Add('  PROPERTIES LANGUAGE C)')
    }
    if ($type -eq 'WIN32') {
        $cmake.Add('add_executable(' + $target + ' WIN32 ${' + $target + '_sources})')
    }
    else {
        $cmake.Add('add_library(' + $target + ' ' + $type + ' ${' + $target + '_sources})')
    }
    # VC6 deliberately kept for-loop variables alive after the loop and did
    # not use sized deallocation.  Keep those two legacy rules while porting
    # the engine; changing every historic source file first would hide the
    # actual engine/build issues we need to solve.
    $cmake.Add('target_compile_options(' + $target + ' PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)')

    $definitions = New-Object System.Collections.Generic.List[string]
    foreach ($setting in @($project.base_compiler_settings + $project.compiler_settings | Where-Object { $_ -match 'NDEBUG' })) {
        foreach ($definition in [regex]::Matches($setting, '/D\s+"(?<value>[^"]+)"')) {
            $value = $definition.Groups['value'].Value
            if (-not $definitions.Contains($value)) {
                $definitions.Add($value)
            }
        }
    }
    if ($project.name -eq 'Hidden And Dangerous' -and -not $definitions.Contains('FINAL')) {
        $definitions.Add('FINAL')
    }
    if ($definitions.Count) {
        $cmake.Add('target_compile_definitions(' + $target + ' PRIVATE ' + ($definitions -join ' ') + ')')
    }

    if ($project.name -eq 'Hidden And Dangerous') {
        # MSVC 2026 currently ICEs while optimizing the original 5k-line
        # Actors.cpp.  Keep the game logic unchanged and compile just this
        # legacy translation unit without optimizer transformations.
        $cmake.Add('set_source_files_properties("' + (Join-Path $SourceRoot '_src/Actors.cpp').Replace('\', '/') + '" PROPERTIES COMPILE_OPTIONS "/Od")')
        # MainMenu.cpp contains the game's normal front-end behind
        # #ifndef EDITOR.  Preserve EDITOR for its helper units, but undefine
        # it for this one source so those entry points are linked.
        $cmake.Add('set_source_files_properties("' + (Join-Path $SourceRoot '_src/MainMenu.cpp').Replace('\', '/') + '" PROPERTIES COMPILE_OPTIONS "/UEDITOR")')
    }

    if ($project.name -eq 'Dta_read') {
        $cmake.Add('target_compile_definitions(' + $target + ' PRIVATE DTA_READ_BUILD)')
        $cmake.Add('set_target_properties(' + $target + ' PROPERTIES OUTPUT_NAME "icom_rd")')
    }
    if ($project.name -eq 'IEditor') {
        $cmake.Add('set_target_properties(' + $target + ' PROPERTIES OUTPUT_NAME "IEditor")')
        # Editor.h injects a self-reference to the VC6 import library.  The
        # original DSP explicitly suppressed that self-link; retain it here.
        $cmake.Add('target_link_options(' + $target + ' PRIVATE /NODEFAULTLIB:IEditor.lib)')
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE comctl32 shell32)')
    }
    if ($project.name -eq 'Crash') {
        # The VC6 project obtained StackWalk and symbol APIs through the
        # obsolete th32 import library. Modern Windows supplies them in
        # DebugHelp instead.
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE dbghelp)')
    }
    if ($project.name -eq 'inet2') {
        # The original VC6 workspace depended on DirectPlay through the
        # legacy DX SDK.  Keep that dependency local to the ported network
        # transport rather than copying SDK DLLs into the game package.
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE dplayx dxguid ole32 user32 winmm gdi32 hd_insanity)')
    }
    if ($project.name -eq 'igraph2') {
        # DirectInput and the multimedia timer are supplied by the modern
        # Windows SDK; VC6 previously injected these import libraries.
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE dinput8 dxguid winmm)')
    }
    if ($project.name -eq 'tabler2') {
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE comctl32)')
    }
    if ($project.name -eq 'isnd2') {
        # DirectSound interfaces and the legacy MMIO WAV loader are supplied
        # by the supported Windows SDK import libraries.
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE dsound dxguid winmm)')
    }

    $dependencies = @($project.dependencies | Where-Object { $included.ContainsKey($_) })
    if ($dependencies.Count) {
        $dependencyTargets = @($dependencies | ForEach-Object { Get-TargetName $_ })
        $cmake.Add('target_link_libraries(' + $target + ' PRIVATE ' + ($dependencyTargets -join ' ') + ')')
    }
    $cmake.Add('')
}

$mainTarget = Get-TargetName 'Hidden And Dangerous'
$cmake.Add('target_link_libraries(' + $mainTarget + ' PRIVATE ' + (Get-TargetName 'IEditor') + ')')
$cmake.Add('target_link_libraries(' + $mainTarget + ' PRIVATE user32 gdi32 shell32 comdlg32 ole32 oleaut32 winmm dxguid wsock32)')
$cmake.Add('set_target_properties(' + $mainTarget + ' PROPERTIES OUTPUT_NAME "HDE_Authority" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")')

if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}
$cmakePath = Join-Path $OutputDirectory 'CMakeLists.txt'
$cmake -join [Environment]::NewLine | Set-Content -LiteralPath $cmakePath -Encoding utf8
Write-Host "CMake project written: $cmakePath ($($orderedProjects.Count) targets)"
