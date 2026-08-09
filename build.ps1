<#
.SYNOPSIS
    Configure or build the seekdb CMake compatibility release on Windows x64.

.EXAMPLE
    .\build.ps1 release --init --ninja -j 16
#>

$ErrorActionPreference = "Stop"
$TOPDIR = $PSScriptRoot
$Action = "release"
$Build = $false
$Init = $false
$Jobs = 0
$Help = $false
$ExtraCMakeArgs = @()

$i = 0
while ($i -lt $args.Count) {
    $arg = "$($args[$i])"
    switch -Wildcard ($arg) {
        { $_ -in "-h", "--help", "-help" } { $Help = $true }
        { $_ -in "--ninja", "-ninja", "--make" } { $Build = $true }
        { $_ -in "--init", "-init" } { $Init = $true }
        { $_ -in "-j", "--jobs" } {
            $i++
            if ($i -ge $args.Count) { throw "$arg requires a job count" }
            $Jobs = [int]$args[$i]
        }
        { $_.StartsWith("-D") } { $ExtraCMakeArgs += $arg }
        default {
            if ($arg.StartsWith("-")) { throw "unsupported option: $arg" }
            $Action = $arg
        }
    }
    $i++
}

function Write-Log { param([string]$Message) Write-Host "[build.ps1] $Message" }
function Write-Err { param([string]$Message) Write-Host "[build.ps1][ERROR] $Message" -ForegroundColor Red }

function Show-Usage {
    Write-Host @"
Usage:
    .\build.ps1 -h
    .\build.ps1 init
    .\build.ps1 clean
    .\build.ps1 release [--init] [-DName=Value ...]
    .\build.ps1 release [--init] [-DName=Value ...] --ninja [-j N]

Supported compatibility build:
    Windows x64, RelWithDebInfo (-O2), Unity, seekdb production binary.

Bazel remains authoritative for modular dependencies, tests, architecture
checks, and non-release options. Invoke it through .\bazel.py directly.
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

$NativeArch = if ($env:PROCESSOR_ARCHITEW6432) {
    $env:PROCESSOR_ARCHITEW6432
} else {
    $env:PROCESSOR_ARCHITECTURE
}
if ($NativeArch -notin "AMD64", "x86_64") {
    Write-Err "Only Windows x64 is supported; detected $NativeArch"
    exit 2
}

$DEPS_3RD = "$TOPDIR\deps\3rd"
$TOOLS_DIR = "$DEPS_3RD\tools"

function Add-DependencyToolsToPath {
    $ToolPaths = @(
        "$TOOLS_DIR\cmake\bin",
        "$TOOLS_DIR\ninja",
        "$TOOLS_DIR\llvm18\bin",
        "$TOOLS_DIR\win_flex_bison"
    )
    foreach ($Path in $ToolPaths) {
        if ((Test-Path $Path) -and ($env:PATH -notlike "*$Path*")) {
            $env:PATH = "$Path;$env:PATH"
        }
    }
}

function Do-Init {
    $Script = "$TOPDIR\deps\init\dep_create.ps1"
    if (-not (Test-Path $Script)) {
        throw "dependency initializer not found: $Script"
    }
    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Script
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $Stopwatch.Stop()
    Write-Log "dependency initialization completed in $([int]$Stopwatch.Elapsed.TotalSeconds)s"
    Add-DependencyToolsToPath
}

function Do-Clean {
    $BuildDir = "$TOPDIR\build_release"
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Log "removed $BuildDir"
    } else {
        Write-Log "nothing to clean"
    }
}

if ($Action.ToLower() -eq "init") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "init does not accept build options"
    }
    Do-Init
    exit 0
}
if ($Action.ToLower() -eq "clean") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "clean does not accept build options"
    }
    Do-Clean
    exit 0
}
if ($Action.ToLower() -notin "release", "relwithdebinfo") {
    Write-Err "Unsupported build type: $Action (only release is maintained)"
    Show-Usage
    exit 2
}

if ($Init) { Do-Init }
Add-DependencyToolsToPath

$CMake = Get-Command cmake -ErrorAction SilentlyContinue
$Ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $CMake) { throw "cmake not found; run with --init or install CMake 3.20+" }
if (-not $Ninja) { throw "ninja not found; run with --init or install Ninja" }

$DefaultVcpkgDir = if ($env:OB_VCPKG_DIR) { $env:OB_VCPKG_DIR } else { "$DEPS_3RD\vcpkg\x64-windows" }
$DefaultOpenSSLDir = if ($env:OB_OPENSSL_DIR) { $env:OB_OPENSSL_DIR } else { "$DEPS_3RD\openssl" }
$DefaultLLVMDir = if ($env:OB_LLVM_DIR) { $env:OB_LLVM_DIR } else { "$TOOLS_DIR\llvm18" }
$BuildDir = "$TOPDIR\build_release"
$CMakeArgs = @(
    "-S", $TOPDIR,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DOB_ENABLE_UNITY=ON",
    "-DOB_USE_LLD=ON",
    "-DOB_VCPKG_DIR=$DefaultVcpkgDir",
    "-DOB_OPENSSL_DIR=$DefaultOpenSSLDir",
    "-DOB_LLVM_DIR=$DefaultLLVMDir"
) + $ExtraCMakeArgs

Write-Log "configuring Windows x64 release in $BuildDir"
& $CMake.Source @CMakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Build) {
    if ($Jobs -le 0) {
        $Jobs = (Get-CimInstance Win32_Processor |
            Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
        if (-not $Jobs -or $Jobs -lt 1) { $Jobs = 4 }
    }
    Write-Log "building seekdb with Ninja (-j $Jobs)"
    & $Ninja.Source -C $BuildDir -j $Jobs seekdb
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
