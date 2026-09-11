<#
.SYNOPSIS
    Assembles the distributable addons/gfgdextension folder, optionally building
    it first and installing it into a game project.

.DESCRIPTION
    build_all.ps1 (or scons) produces the libraries; this puts the shippable
    folder together around them, and with -Build it runs that step for you.
    Beyond the manifest, the README and bin/, the addon carries three text-only
    folders that make the extension legible without this repository:

        doc_classes/  the per-class reference as XML - exact signatures, enums
                      and signals, the same files compiled into the binary
        src/          the C++ sources, for when behaviour has to be read rather
                      than guessed at
        skills/       agent skills, installed into a project's .claude/skills/

    Each of those three gets an empty .gdignore, so Godot never scans, imports
    or exports them. Godot does not export non-resource files by default, but
    it does scan them into the FileSystem dock; the .gdignore is what makes
    both guarantees unconditional.

    The .gdignore goes in the SUBFOLDERS and never in the addon root: a root
    one would hide gfgd.gdextension itself and the extension would not load at
    all. (Keep that sentence off the start of a line - Get-Help reads a leading
    ".word" as a section keyword and silently drops the rest of the help.)

    Object files and src/gen are build artefacts and are never copied.

.PARAMETER Build
    Platforms to compile first, by handing each to build_all.ps1. Without it
    nothing is compiled and whatever is already in bin/ is packed as it stands -
    which is what you want after editing doc_classes, skills or the README.

    One platform is one full set of targets: windows, linux and macos each build
    editor + template_debug + template_release, android builds debug and release
    for both arm64 and x86_64.

    Cross-compiling only works for android. Building linux or macos needs that
    host, so naming them on Windows will fail rather than quietly produce
    nothing. A failed build stops the script before anything is packed.

.PARAMETER Destination
    A game project's addons/gfgdextension folder to mirror the result into,
    e.g. "D:\Godot Projects\PaperPlane\addons\gfgdextension". Without it the
    addon inside this repository's demo project is the only thing updated.

.PARAMETER InstallSkills
    With -Destination, also copy skills/gfgd into <project>/.claude/skills/.
    The project root is taken to be the grandparent of the destination.

.EXAMPLE
    ./pack_addon.ps1
    Repacks only. No compiler runs - use this after changing doc_classes,
    skills, or the README.

.EXAMPLE
    ./pack_addon.ps1 -Build windows,android
    The full release cycle for the platforms this project actually ships on:
    three Windows targets, four Android ones, then the packed addon.

    Run it from a "Developer PowerShell for VS" - the presets use Ninja, which
    does not locate MSVC on its own - and with NDK_ROOT pointing at the NDK.

.EXAMPLE
    ./pack_addon.ps1 -Build windows,android -Destination "D:\Godot Projects\PaperPlane\addons\gfgdextension" -InstallSkills
    The same, and installs the result into PaperPlane together with the gfgd
    agent skill. This is the one command that takes a source change all the way
    into the game.
#>

param(
    [ValidateSet("windows", "linux", "macos", "android")]
    [string[]]$Build,

    [string]$Destination,
    [switch]$InstallSkills
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

$addon = Join-Path $PSScriptRoot "project/addons/gfgdextension"
if (-not (Test-Path -LiteralPath $addon)) {
    throw "Addon folder not found at $addon"
}

# --- build ------------------------------------------------------------------
# Before anything is assembled, so a failed compile stops the script rather than
# shipping an addon around stale libraries. The builds themselves drop their
# output straight into the addon's bin/ (CMake POST_BUILD, or env.Install under
# scons), which is why packing has nothing to copy afterwards.
if ($Build) {
    $buildAll = Join-Path $PSScriptRoot "build_all.ps1"
    foreach ($platform in $Build) {
        Write-Host ""
        Write-Host "=== building $platform ===" -ForegroundColor Cyan
        & $buildAll $platform
    }
    Write-Host ""
}

# Folders that ship as plain text and must stay invisible to Godot.
$ignoredSubfolders = @("doc_classes", "src", "skills")

function Reset-Folder([string]$path) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

# --- doc_classes ------------------------------------------------------------
$docsOut = Join-Path $addon "doc_classes"
Reset-Folder $docsOut
$docs = @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot "doc_classes") -Filter *.xml -File)
$docs | Copy-Item -Destination $docsOut
Write-Host "doc_classes  $($docs.Count) file(s)" -ForegroundColor DarkGray

# --- src --------------------------------------------------------------------
# Sources only. Object files land next to them from in-tree scons builds, and
# src/gen holds the generated doc_data translation unit.
$srcRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "src")).Path
$srcOut = Join-Path $addon "src"
$genRoot = Join-Path $srcRoot "gen"
Reset-Folder $srcOut
$sources = @(Get-ChildItem -LiteralPath $srcRoot -Recurse -File |
    Where-Object { $_.Extension -in ".cpp", ".h", ".hpp", ".inl" } |
    Where-Object { -not $_.FullName.StartsWith($genRoot, [StringComparison]::OrdinalIgnoreCase) })
foreach ($file in $sources) {
    $relative = $file.FullName.Substring($srcRoot.Length + 1)
    $target = Join-Path $srcOut $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
}
$srcBytes = ($sources | Measure-Object -Property Length -Sum).Sum
Write-Host "src          $($sources.Count) file(s), $([math]::Round($srcBytes / 1KB)) KB" -ForegroundColor DarkGray

# --- skills -----------------------------------------------------------------
$skillsRoot = Join-Path $PSScriptRoot "skills"
$skillsOut = Join-Path $addon "skills"
Reset-Folder $skillsOut
if (Test-Path -LiteralPath $skillsRoot) {
    Copy-Item -Path (Join-Path $skillsRoot "*") -Destination $skillsOut -Recurse -Force
    $skillCount = @(Get-ChildItem -LiteralPath $skillsOut -Recurse -File).Count
    Write-Host "skills       $skillCount file(s)" -ForegroundColor DarkGray
} else {
    Write-Host "skills       none (skills/ does not exist yet)" -ForegroundColor DarkYellow
}

# --- .gdignore --------------------------------------------------------------
foreach ($name in $ignoredSubfolders) {
    $marker = Join-Path (Join-Path $addon $name) ".gdignore"
    [System.IO.File]::WriteAllText($marker, "")
}
Write-Host ".gdignore    $($ignoredSubfolders -join ', ')" -ForegroundColor DarkGray

# --- VERSION.txt ------------------------------------------------------------
# Stamped so a project (or an agent reading the addon) can tell which build it
# is looking at without this repository being present.
function Get-GitValue([string[]]$gitArgs, [string]$fallback = "unknown") {
    try {
        $value = & git @gitArgs 2>$null
        if ($LASTEXITCODE -eq 0 -and $value) { return ($value | Select-Object -First 1).ToString().Trim() }
    } catch { }
    return $fallback
}

$apiVersion = "unknown"
$sconstruct = Get-Content -LiteralPath (Join-Path $PSScriptRoot "SConstruct") -Raw
if ($sconstruct -match 'api_version"\]\s*=\s*"([^"]+)"') { $apiVersion = $Matches[1] }

$compatibilityMinimum = "unknown"
$manifest = Get-Content -LiteralPath (Join-Path $addon "gfgd.gdextension") -Raw
if ($manifest -match 'compatibility_minimum\s*=\s*"([^"]+)"') { $compatibilityMinimum = $Matches[1] }

$describe = Get-GitValue @("describe", "--tags", "--always", "--dirty")
$stampLines = @(
    "gfgd            $describe",
    "commit          $(Get-GitValue @('rev-parse', 'HEAD'))",
    "packed          $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss K')",
    "godot-cpp       $(Get-GitValue @('-C', 'godot-cpp', 'describe', '--tags', '--always'))",
    "api_version     $apiVersion",
    "godot_minimum   $compatibilityMinimum"
)
Set-Content -LiteralPath (Join-Path $addon "VERSION.txt") -Value $stampLines -Encoding utf8
Write-Host "VERSION.txt  $describe" -ForegroundColor DarkGray

# --- binaries ---------------------------------------------------------------
$binaries = @(Get-ChildItem -LiteralPath (Join-Path $addon "bin") -Recurse -File |
    Where-Object { $_.Extension -in ".dll", ".so", ".dylib", ".wasm" })
if ($binaries.Count -eq 0) {
    Write-Host "bin          EMPTY - run build_all.ps1 first" -ForegroundColor Yellow
} else {
    $platforms = $binaries | Group-Object { $_.Directory.Name } | ForEach-Object { "$($_.Name) x$($_.Count)" }
    Write-Host "bin          $($platforms -join ', ')" -ForegroundColor DarkGray
}

Write-Host "Packed $addon" -ForegroundColor Green

# --- install ----------------------------------------------------------------
if (-not $Destination) { return }

Write-Host ""
if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -Path (Join-Path $addon "*") -Destination $Destination -Recurse -Force
Write-Host "Installed to $Destination" -ForegroundColor Green

if (-not $InstallSkills) { return }

$projectRoot = Split-Path -Parent (Split-Path -Parent $Destination)
$skillsTarget = Join-Path $projectRoot ".claude/skills"
$consumerSkill = Join-Path $skillsOut "gfgd"
if (-not (Test-Path -LiteralPath $consumerSkill)) {
    Write-Host "No skills/gfgd to install." -ForegroundColor DarkYellow
    return
}
New-Item -ItemType Directory -Path $skillsTarget -Force | Out-Null
$installed = Join-Path $skillsTarget "gfgd"
if (Test-Path -LiteralPath $installed) { Remove-Item -LiteralPath $installed -Recurse -Force }
Copy-Item -LiteralPath $consumerSkill -Destination $skillsTarget -Recurse -Force
Write-Host "Installed skill to $installed" -ForegroundColor Green
