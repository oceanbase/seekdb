<#
.SYNOPSIS
    Pack Windows build dependencies into tar.gz archives for distribution.

.DESCRIPTION
    Run this on a "golden" machine that has all build dependencies installed.
    It creates tar.gz archives that can be uploaded to the deps HTTP server,
    and regenerates the deps manifest (oceanbase.windows.x86_64.deps).

    Each archive contains a single top-level directory; dep_create.ps1 extracts
    with --strip-components=1, so the internal paths determine the final layout
    under deps/3rd/.

.PARAMETER OutputDir
    Directory where tar.gz files are written (default: current directory).

.PARAMETER VcpkgRoot
    Root of vcpkg installed packages (default: C:\VcpkgInstalled).

.PARAMETER OpenSSLDir
    OpenSSL installation root (default: C:\Program Files\OpenSSL-Win64).

.PARAMETER LLVMDir
    LLVM 18 installation root (default: C:\Program Files\LLVM18).

.PARAMETER Repo
    Base URL for the deps HTTP server (written into the .deps file).

.EXAMPLE
    .\pack_win_deps.ps1
    .\pack_win_deps.ps1 -OutputDir D:\archives -Repo http://myhost:1234/win/deps
#>

param(
    [string]$OutputDir    = ".",
    [string]$VcpkgRoot    = "C:\VcpkgInstalled",
    [string]$OpenSSLDir   = "C:\Program Files\OpenSSL-Win64",
    [string]$LLVMDir      = "C:\Program Files\LLVM18",
    [string]$DateStamp    = (Get-Date -Format "yyyyMMdd")
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot

function Write-Log  { param([string]$msg) Write-Host "[pack_win_deps] $msg" }
function Write-Err  { param([string]$msg) Write-Host "[pack_win_deps][ERROR] $msg" -ForegroundColor Red }

$OutputDir = (Resolve-Path $OutputDir).Path

# -- Auto-detect tools -----------------------------------------------

# CMake
$cmakeCmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
$CMakeDir = $null
if ($cmakeCmd) {
    $CMakeDir = Split-Path (Split-Path $cmakeCmd.Source) -Parent
    Write-Log "CMake detected: $CMakeDir"
} else {
    Write-Err "cmake.exe not found in PATH. CMake will not be packaged."
}

# Ninja
$ninjaCmd = Get-Command ninja.exe -ErrorAction SilentlyContinue
$NinjaDir = $null
if ($ninjaCmd) {
    $NinjaDir = Split-Path $ninjaCmd.Source
    Write-Log "Ninja detected: $NinjaDir"
} else {
    Write-Err "ninja.exe not found in PATH. Ninja will not be packaged."
}

# Flex & Bison (win_flex_bison)
$flexCmd = Get-Command win_flex.exe -ErrorAction SilentlyContinue
$FlexBisonDir = $null
if ($flexCmd) {
    $FlexBisonDir = Split-Path $flexCmd.Source
    Write-Log "Flex/Bison detected: $FlexBisonDir"
} else {
    Write-Err "win_flex.exe not found in PATH. Flex/Bison will not be packaged."
}

# -- Helper: create a tar.gz using directory junctions (fast, no copy) -

function New-DepArchive {
    param(
        [string]$ArchiveName,       # e.g. "devdeps-vcpkg-all-20260414.tar.gz"
        [string]$TopDirName,        # top-level dir inside tar, e.g. "devdeps-vcpkg-all"
        [hashtable]$Mappings        # "internal/path" -> "source/path" (dirs to junction)
    )

    $staging = "$env:TEMP\ob_pack_staging"
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }

    $junctions = @()
    foreach ($kv in $Mappings.GetEnumerator()) {
        $internalPath = "$staging\$TopDirName\$($kv.Key)"
        $sourcePath   = $kv.Value

        if (-not (Test-Path $sourcePath)) {
            Write-Err "Source not found: $sourcePath - skipping $($kv.Key)"
            continue
        }

        $parentDir = Split-Path $internalPath
        if (-not (Test-Path $parentDir)) {
            New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
        }

        if ((Get-Item $sourcePath) -is [System.IO.DirectoryInfo]) {
            New-Item -ItemType Junction -Path $internalPath -Target $sourcePath | Out-Null
            $junctions += $internalPath
        } else {
            $destDir = Split-Path $internalPath
            if (-not (Test-Path $destDir)) { New-Item -ItemType Directory $destDir -Force | Out-Null }
            Copy-Item $sourcePath $internalPath
        }
    }

    $outPath = Join-Path $OutputDir $ArchiveName
    if (Test-Path $outPath) { Remove-Item $outPath }

    Write-Log "  Creating: $ArchiveName ..."
    Push-Location $staging
    try {
        & tar -czf $outPath $TopDirName 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Err "tar failed for $ArchiveName"
            return $null
        }
    } finally {
        Pop-Location
    }

    # Remove junctions first (so we don't delete real files)
    foreach ($j in $junctions) {
        if (Test-Path $j) {
            # Remove-Item on a junction deletes the junction, not the target
            cmd /c rmdir $j 2>$null
        }
    }
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }

    if (Test-Path $outPath) {
        $sizeMB = [math]::Round((Get-Item $outPath).Length / 1MB, 1)
        $msg = "  Created: $ArchiveName `($sizeMB MB`)"
        Write-Log $msg
    }
    return $ArchiveName
}

# -- Pack each component ---------------------------------------------
$depsPackages  = @()
$toolsPackages = @()

# 1. vcpkg all packages
if (Test-Path "$VcpkgRoot\x64-windows") {
    $name = "devdeps-vcpkg-all-$DateStamp.tar.gz"
    Write-Log "Packing vcpkg packages from $VcpkgRoot ..."
    $result = New-DepArchive -ArchiveName $name -TopDirName "devdeps-vcpkg-all" -Mappings @{
        "vcpkg\x64-windows" = "$VcpkgRoot\x64-windows"
    }
    if ($result) { $depsPackages += $result }
} else {
    Write-Err "vcpkg not found: $VcpkgRoot\x64-windows"
}

# 2. OpenSSL
if (Test-Path $OpenSSLDir) {
    $name = "devdeps-openssl-$DateStamp.tar.gz"
    Write-Log "Packing OpenSSL from $OpenSSLDir ..."
    $result = New-DepArchive -ArchiveName $name -TopDirName "devdeps-openssl" -Mappings @{
        "openssl" = $OpenSSLDir
    }
    if ($result) { $depsPackages += $result }
} else {
    Write-Err "OpenSSL not found: $OpenSSLDir"
}

# 3. CMake
if ($CMakeDir -and (Test-Path $CMakeDir)) {
    $name = "obdevtools-cmake-$DateStamp.tar.gz"
    Write-Log "Packing CMake from $CMakeDir ..."
    $result = New-DepArchive -ArchiveName $name -TopDirName "obdevtools-cmake" -Mappings @{
        "tools\cmake" = $CMakeDir
    }
    if ($result) { $toolsPackages += $result }
}

# 4. Ninja
if ($NinjaDir -and (Test-Path $NinjaDir)) {
    $name = "obdevtools-ninja-$DateStamp.tar.gz"
    Write-Log "Packing Ninja from $NinjaDir ..."
    $result = New-DepArchive -ArchiveName $name -TopDirName "obdevtools-ninja" -Mappings @{
        "tools\ninja" = $NinjaDir
    }
    if ($result) { $toolsPackages += $result }
}

# 5. LLVM 18
if (Test-Path $LLVMDir) {
    $name = "obdevtools-llvm-18.1.8-$DateStamp.tar.gz"
    Write-Log "Packing LLVM from $LLVMDir ..."
    $result = New-DepArchive -ArchiveName $name -TopDirName "obdevtools-llvm" -Mappings @{
        "tools\llvm18" = $LLVMDir
    }
    if ($result) { $toolsPackages += $result }
} else {
    Write-Err "LLVM not found: $LLVMDir"
}

# 6. Flex & Bison
if ($FlexBisonDir -and (Test-Path $FlexBisonDir)) {
    $name = "obdevtools-win-flex-bison-$DateStamp.tar.gz"
    Write-Log "Packing Flex/Bison from $FlexBisonDir ..."
    $result = New-DepArchive -ArchiveName $name -TopDirName "obdevtools-win-flex-bison" -Mappings @{
        "tools\win_flex_bison" = $FlexBisonDir
    }
    if ($result) { $toolsPackages += $result }
}

# -- Generate deps file ----------------------------------------------
$depsFile = "$ScriptDir\oceanbase.windows.x86_64.deps"
$content  = @"
[target-default]
os=windows
arch=x86_64
repo=$Repo

[deps]

"@
foreach ($p in $depsPackages) { $content += "$p`n" }

$content += "`n[tools]`n"
foreach ($p in $toolsPackages) { $content += "$p`n" }

Set-Content -Path $depsFile -Value $content -Encoding UTF8 -NoNewline
Write-Log ""
Write-Log "Updated deps manifest: $depsFile"

# -- Summary ----------------------------------------------------------
Write-Log ""
Write-Log "==========================================================="
Write-Log "  Archives generated in: $OutputDir"
Write-Log ""
foreach ($p in ($depsPackages + $toolsPackages)) {
    $f = Get-Item (Join-Path $OutputDir $p)
    $sizeMB = [math]::Round($f.Length / 1MB, 1)
    $msg = "    $p  `($sizeMB MB`)"
    Write-Log $msg
}
Write-Log ""
Write-Log "  Next steps:"
$step1 = "    1. Upload archives to $Repo/"
Write-Log $step1
Write-Log "    2. On target machine:  .\build.ps1 init"
Write-Log "==========================================================="
