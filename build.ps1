<#
.SYNOPSIS
    OceanBase Lite Windows build script — mirrors build.sh on Linux/macOS.

.EXAMPLE
    .\build.ps1 -h
    .\build.ps1 init
    .\build.ps1 release
    .\build.ps1 release --ninja
    .\build.ps1 release --ninja -j 16
    .\build.ps1 debug
    .\build.ps1 clean
#>

param(
    [Parameter(Position = 0)]
    [string]$Action = "debug",

    [switch]$Ninja,

    [Alias("j")]
    [int]$Jobs = 0,

    [switch]$h
)

$ErrorActionPreference = "Stop"
$TOPDIR = $PSScriptRoot

# ── Dependency path defaults (override via env vars) ────────────────
$DefaultVcpkgDir  = if ($env:OB_VCPKG_DIR)    { $env:OB_VCPKG_DIR }    else { "C:/VcpkgInstalled/x64-windows" }
$DefaultOpenSSLDir = if ($env:OB_OPENSSL_DIR)  { $env:OB_OPENSSL_DIR }  else { "C:/Program Files/OpenSSL-Win64" }
$DefaultLLVMDir   = if ($env:OB_LLVM_DIR)      { $env:OB_LLVM_DIR }     else { "C:/Program Files/LLVM18" }
$DefaultWinDepsZip = if ($env:OB_WIN_DEPS_ZIP) { $env:OB_WIN_DEPS_ZIP } else { "$TOPDIR/win_deps.zip" }

# ── Helpers ─────────────────────────────────────────────────────────
function Write-Log  { param([string]$msg) Write-Host "[build.ps1] $msg" }
function Write-Err  { param([string]$msg) Write-Host "[build.ps1][ERROR] $msg" -ForegroundColor Red }

function Show-Usage {
    Write-Host @"

Usage:
    .\build.ps1 -h                       Show this help
    .\build.ps1 init                     Extract pre-built deps (win_deps.zip)
    .\build.ps1 clean                    Remove build_* directories
    .\build.ps1 [BuildType]                Configure only (cmake)
    .\build.ps1 [BuildType] --ninja        Configure + compile (ninja)
    .\build.ps1 [BuildType] --ninja -j 16  Configure + compile with 16 jobs
    .\build.ps1 package                    Build release + generate MSI/ZIP installer

BuildType:
    debug           Debug build (default)
    release         RelWithDebInfo build
    relwithdebinfo  Alias for release

Environment variables (override dependency paths):
    OB_VCPKG_DIR      vcpkg install root   (default: C:/VcpkgInstalled/x64-windows)
    OB_OPENSSL_DIR    OpenSSL root          (default: C:/Program Files/OpenSSL-Win64)
    OB_LLVM_DIR       LLVM 18 root          (default: C:/Program Files/LLVM18)
    OB_WIN_DEPS_ZIP   Path to deps zip      (default: <project>/win_deps.zip)

"@
}

if ($Jobs -eq 0) {
    $cpuCount = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
    if (-not $cpuCount -or $cpuCount -lt 1) { $cpuCount = 4 }
    $totalMemGB = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    $memJobs = [math]::Max(1, [math]::Floor($totalMemGB / 3))
    $Jobs = [math]::Min($cpuCount, $memJobs)
    Write-Log "Auto jobs: $Jobs (cpus=$cpuCount, mem=${totalMemGB}GB, ~3GB/job)"
}

# ── init: extract dependency archive ────────────────────────────────
function Do-Init {
    $zipPath = $DefaultWinDepsZip
    if (-not (Test-Path $zipPath)) {
        Write-Err "Deps archive not found: $zipPath"
        Write-Log "Set OB_WIN_DEPS_ZIP or place win_deps.zip in the project root."
        Write-Log ""
        Write-Log "To create win_deps.zip on a machine that already has deps installed:"
        Write-Log "  .\build.ps1 pack"
        exit 1
    }

    Write-Log "Extracting deps from $zipPath ..."
    $destDir = "$TOPDIR\win_deps"
    if (Test-Path $destDir) {
        Write-Log "Removing existing win_deps/ ..."
        Remove-Item -Recurse -Force $destDir
    }
    Expand-Archive -Path $zipPath -DestinationPath $destDir -Force
    Write-Log "Dependencies extracted to $destDir"
    Write-Log ""
    Write-Log "Set the following env vars (or they will use defaults):"
    Write-Log "  `$env:OB_VCPKG_DIR   = '$destDir\vcpkg\x64-windows'"
    Write-Log "  `$env:OB_OPENSSL_DIR = '$destDir\openssl'"
    Write-Log "  `$env:OB_LLVM_DIR    = '$destDir\llvm18'"
}

# ── pack: create dependency archive from current machine ────────────
function Do-Pack {
    $packDir = "$TOPDIR\win_deps_staging"
    if (Test-Path $packDir) { Remove-Item -Recurse -Force $packDir }
    New-Item -ItemType Directory -Path $packDir | Out-Null

    Write-Log "Packing vcpkg from: $DefaultVcpkgDir"
    if (Test-Path $DefaultVcpkgDir) {
        $vcpkgDest = "$packDir\vcpkg\x64-windows"
        New-Item -ItemType Directory -Path "$packDir\vcpkg" -Force | Out-Null
        Copy-Item -Recurse -Path $DefaultVcpkgDir -Destination $vcpkgDest
    } else {
        Write-Err "vcpkg dir not found: $DefaultVcpkgDir"
    }

    Write-Log "Packing OpenSSL from: $DefaultOpenSSLDir"
    if (Test-Path $DefaultOpenSSLDir) {
        Copy-Item -Recurse -Path $DefaultOpenSSLDir -Destination "$packDir\openssl"
    } else {
        Write-Err "OpenSSL dir not found: $DefaultOpenSSLDir"
    }

    Write-Log "Packing LLVM from: $DefaultLLVMDir"
    if (Test-Path $DefaultLLVMDir) {
        Copy-Item -Recurse -Path $DefaultLLVMDir -Destination "$packDir\llvm18"
    } else {
        Write-Err "LLVM dir not found: $DefaultLLVMDir"
    }

    $outZip = "$TOPDIR\win_deps.zip"
    Write-Log "Compressing to $outZip ..."
    if (Test-Path $outZip) { Remove-Item $outZip }
    Compress-Archive -Path "$packDir\*" -DestinationPath $outZip -CompressionLevel Optimal
    Remove-Item -Recurse -Force $packDir
    Write-Log "Done! Archive: $outZip"
}

# ── clean ───────────────────────────────────────────────────────────
function Do-Clean {
    Write-Log "Cleaning build directories ..."
    Get-ChildItem -Path $TOPDIR -Directory -Filter "build*" | ForEach-Object {
        Write-Log "  Removing $($_.Name)"
        Remove-Item -Recurse -Force $_.FullName
    }
    Write-Log "Clean done."
}

# ── cmake configure ────────────────────────────────────────────────
function Do-Build {
    param(
        [string]$BuildType,
        [string[]]$ExtraCMakeArgs = @()
    )

    $buildDir = "$TOPDIR\build_$($BuildType.ToLower())"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    $cmakeArgs = @(
        $TOPDIR,
        "-G", "Ninja",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=1",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DOB_USE_LLD=ON",
        "-DOB_VCPKG_DIR=$DefaultVcpkgDir",
        "-DOB_OPENSSL_DIR=$DefaultOpenSSLDir",
        "-DOB_LLVM_DIR=$DefaultLLVMDir"
    ) + $ExtraCMakeArgs

    Write-Log "CMake configure: build_$($BuildType.ToLower())"
    Write-Log "  Build type : $BuildType"
    Write-Log "  VcpkgDir   : $DefaultVcpkgDir"
    Write-Log "  OpenSSLDir : $DefaultOpenSSLDir"
    Write-Log "  LLVMDir    : $DefaultLLVMDir"
    Write-Log ""

    Push-Location $buildDir
    try {
        & cmake @cmakeArgs 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Err "CMake configure failed (exit code $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
        Write-Log "CMake configure succeeded."

        # Copy compile_commands.json to project root for IDE support
        $ccJson = "$buildDir\compile_commands.json"
        if (Test-Path $ccJson) {
            Copy-Item $ccJson "$TOPDIR\compile_commands.json" -Force
            Write-Log "compile_commands.json copied to project root."
        }
    }
    finally {
        Pop-Location
    }

    return $buildDir
}

# ── ninja build ─────────────────────────────────────────────────────
function Do-Ninja {
    param([string]$BuildDir)

    Write-Log "Building with Ninja (-j $Jobs) in $BuildDir ..."
    Push-Location $BuildDir
    try {
        & ninja -j $Jobs observer 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Build failed (exit code $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
        Write-Log "Build succeeded!"
    }
    finally {
        Pop-Location
    }
}

# ── package: build release + create installer ───────────────────────
function Do-Package {
    $buildDir = Do-Build -BuildType "RelWithDebInfo" -ExtraCMakeArgs @("-DOB_BUILD_PACKAGE=ON")
    Do-Ninja -BuildDir $buildDir

    Write-Log "Creating installer package in $buildDir ..."
    Push-Location $buildDir
    try {
        $wixFound = Get-Command wix -ErrorAction SilentlyContinue
        if ($wixFound) {
            Write-Log "WiX v4 found, generating MSI..."
            & cpack -G WIX -C RelWithDebInfo 2>&1 | Out-Host
            if ($LASTEXITCODE -ne 0) {
                Write-Log "WiX MSI generation failed, falling back to ZIP..."
                & cpack -G ZIP -C RelWithDebInfo 2>&1 | Out-Host
            }
        } else {
            Write-Log "WiX not found, generating ZIP package..."
            Write-Log "  To generate MSI: dotnet tool install --global wix"
            & cpack -G ZIP -C RelWithDebInfo 2>&1 | Out-Host
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Package generation failed (exit code $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
        $packages = Get-ChildItem -Path $buildDir -Include "seekdb-*.msi","seekdb-*.zip" -File
        if ($packages) {
            Write-Log "Package(s) created:"
            foreach ($pkg in $packages) {
                Write-Log "  $($pkg.FullName)"
            }
        }
        Write-Log "Package build succeeded!"
    }
    finally {
        Pop-Location
    }
}

# ── Main ────────────────────────────────────────────────────────────
if ($h) {
    Show-Usage
    exit 0
}

switch ($Action.ToLower()) {
    "init" {
        Do-Init
    }
    "pack" {
        Do-Pack
    }
    "package" {
        Do-Package
    }
    "clean" {
        Do-Clean
    }
    { $_ -in "release", "relwithdebinfo" } {
        $buildDir = Do-Build -BuildType "RelWithDebInfo"
        if ($Ninja) { Do-Ninja -BuildDir $buildDir }
    }
    { $_ -in "debug", "" } {
        $buildDir = Do-Build -BuildType "Debug"
        if ($Ninja) { Do-Ninja -BuildDir $buildDir }
    }
    "-h" {
        Show-Usage
    }
    default {
        Write-Err "Unknown action: $Action"
        Show-Usage
        exit 1
    }
}
