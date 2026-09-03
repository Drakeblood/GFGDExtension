#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Configures and builds every GDExtension target for one platform.

.DESCRIPTION
    GODOTCPP_TARGET is baked in at configure time and the platform comes from
    the toolchain, so every platform/target pair needs its own build directory.
    This walks all the presets in CMakePresets.json whose name starts with the
    given platform and leaves the libraries side by side in
    project/addons/gfgdextension/bin/<platform>/.

    Defaults to the host platform. Cross-compiling only works for android (via
    the NDK); linux and macos have to be built on a Linux or macOS host, and
    their presets are disabled elsewhere.

    The presets use the Ninja generator, which does not locate MSVC on its own:
    for the windows platform, run this from "Developer PowerShell for VS" /
    "x64 Native Tools Command Prompt", or from a Rider terminal, which already
    has the toolchain in scope. The android NDK brings its own clang, so it
    needs no such setup - only NDK_ROOT pointing at the NDK.

.EXAMPLE
    ./build_all.ps1
    ./build_all.ps1 android
    ./build_all.ps1 windows -Targets windows-editor
    ./build_all.ps1 macos -List
#>

param(
    [Parameter(Position = 0)]
    [ValidateSet("windows", "linux", "macos", "android")]
    [string]$Platform,

    # Build only these presets instead of every one for the platform.
    [string[]]$Targets,

    # Print what would be built and exit.
    [switch]$List
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

if (-not $Platform) {
    # $IsWindows and friends only exist on PowerShell Core; Windows PowerShell
    # is Windows by definition.
    $Platform = if ($PSVersionTable.PSVersion.Major -le 5 -or $IsWindows) { "windows" }
                elseif ($IsLinux) { "linux" }
                elseif ($IsMacOS) { "macos" }
                else { throw "Could not determine the host platform - pass one explicitly." }
    Write-Host "No platform given, using the host: $Platform" -ForegroundColor DarkGray
}

if ($Targets) {
    $presets = $Targets
} else {
    $presets = (Get-Content -LiteralPath "CMakePresets.json" -Raw | ConvertFrom-Json).configurePresets |
        Where-Object { -not $_.hidden -and $_.name -like "$Platform-*" } |
        ForEach-Object { $_.name }
}

if (-not $presets) {
    throw "No presets found for platform '$Platform'."
}

Write-Host "Platform '$Platform' -> $($presets.Count) preset(s):" -ForegroundColor Cyan
$presets | ForEach-Object { Write-Host "  $_" }

if ($List) { return }

foreach ($preset in $presets) {
    Write-Host ""
    Write-Host "==> configuring $preset" -ForegroundColor Cyan
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed for preset '$preset' (exit $LASTEXITCODE)" }

    Write-Host "==> building $preset" -ForegroundColor Cyan
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "build failed for preset '$preset' (exit $LASTEXITCODE)" }
}

Write-Host ""
Write-Host "Built for $($Platform): $($presets -join ', ')" -ForegroundColor Green
