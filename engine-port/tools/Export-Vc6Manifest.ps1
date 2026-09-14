[CmdletBinding()]
param(
    [Parameter()]
    [string] $SourceRoot,

    [Parameter()]
    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
if (-not $SourceRoot) {
    $SourceRoot = Split-Path -Parent $PSScriptRoot
}
if (-not $OutputPath) {
    $OutputPath = Join-Path $SourceRoot 'vc6-project-manifest.json'
}
$workspacePath = Join-Path $SourceRoot '_src\Hidden And Dangerous.dsw'
if (-not (Test-Path -LiteralPath $workspacePath)) {
    throw "Workspace VC6 introuvable: $workspacePath"
}

$projects = @()
$workspaceDirectory = Split-Path -Parent $workspacePath
$blocks = (Get-Content -LiteralPath $workspacePath -Raw) -split '###############################################################################'
foreach ($block in $blocks) {
    $projectMatch = [regex]::Match(
        $block,
        'Project:\s+"(?<name>[^"]+)"=(?<path>"?.+?\.dsp"?)\s+-')
    if (-not $projectMatch.Success) {
        continue
    }

    $projectName = $projectMatch.Groups['name'].Value
    $dspRelativePath = $projectMatch.Groups['path'].Value.Trim('"').Replace('/', '\')
    $dspPath = Join-Path $workspaceDirectory $dspRelativePath
    if (-not (Test-Path -LiteralPath $dspPath)) {
        Write-Warning "DSP absent pour ${projectName}: $dspPath"
        continue
    }

    $dspDirectory = Split-Path -Parent $dspPath
    $dspLines = Get-Content -LiteralPath $dspPath
    $targetType = ''
    foreach ($dspLine in $dspLines) {
        if ($dspLine -match '^# TARGTYPE\s+"(?<type>[^"]+)"') {
            $targetType = $Matches.type
            break
        }
    }
    $sourceFiles = @(
        foreach ($dspLine in $dspLines) {
            if ($dspLine -match '^SOURCE=(?<source>.+\.(cpp|c|cxx|rc))$') {
                $candidate = $Matches.source.Trim('"').Replace('/', '\')
                [IO.Path]::GetFullPath((Join-Path $dspDirectory $candidate))
            }
        }
    )

    $linkLines = @(
        $dspLines |
            Where-Object { $_ -match '^# ADD LINK32 ' } |
            ForEach-Object { [string]$_ }
    )
    $cppLines = @(
        $dspLines |
            Where-Object { $_ -match '^# ADD CPP ' } |
            ForEach-Object { [string]$_ }
    )
    $baseCppLines = @(
        $dspLines |
            Where-Object { $_ -match '^# ADD BASE CPP ' } |
            ForEach-Object { [string]$_ }
    )
    $dependencies = @(
        [regex]::Matches($block, 'Project_Dep_Name\s+(?<name>[^\s]+)') |
            ForEach-Object { $_.Groups['name'].Value }
    )
    $projects += [pscustomobject]@{
        name = $projectName
        dsp = [IO.Path]::GetFullPath($dspPath)
        target_type = $targetType
        sources = $sourceFiles
        dependencies = $dependencies
        base_compiler_settings = $baseCppLines
        compiler_settings = $cppLines
        linker_settings = $linkLines
    }
}

$manifest = [pscustomobject]@{
    format = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    source_root = [IO.Path]::GetFullPath($SourceRoot)
    workspace = [IO.Path]::GetFullPath($workspacePath)
    projects = $projects
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "VC6 manifest written: $OutputPath ($($projects.Count) projects)"
