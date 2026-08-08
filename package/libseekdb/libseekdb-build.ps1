#Requires -Version 5.1
<#
.SYNOPSIS
  Pack libseekdb for Windows into libseekdb-windows-x64.zip (seekdb.h, seekdb.dll, seekdb.lib, libs/*.dll).

  Runtime DLLs are collected with cmake/BundleRuntimeDllsWindows.cmake (same as POST_BUILD on
  libseekdb and the binding-test PATH layout). macOS packs deps under libs/ via dylibbundler;
  Windows zip must match that layout for standalone consumers (e.g. seekdb-js).

.EXAMPLE
  cd package\libseekdb
  .\libseekdb-build.ps1
  .\libseekdb-build.ps1 -IncludeDir C:\path\to\build_release\src\include
#>
param(
  [string]$IncludeDir = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TopDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
. (Join-Path $TopDir "unittest\include\seekdb-windows-dll-resolve.ps1")

function Get-CMakeExecutable {
  param([Parameter(Mandatory = $true)][string]$RepoRoot)
  $bundled = Join-Path $RepoRoot "deps\3rd\tools\cmake\bin\cmake.exe"
  if (Test-Path -LiteralPath $bundled) { return $bundled }
  $cmd = Get-Command cmake -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  throw "cmake not found (expected deps\3rd\tools\cmake\bin\cmake.exe after build init)"
}

function Get-SeekDbWindowsRuntimeSearchDirs {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$DllDir
  )
  $dirs = [System.Collections.Generic.List[string]]::new()
  $seen = @{}

  function Add-Dir([string]$path) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return }
    $key = (Resolve-Path -LiteralPath $path).Path.ToLowerInvariant()
    if ($seen.ContainsKey($key)) { return }
    $seen[$key] = $true
    $null = $dirs.Add($key)
  }

  # POST_BUILD may already have copied DLLs next to seekdb.dll.
  Add-Dir $DllDir

  $depsDone = Test-Path (Join-Path $RepoRoot "deps\3rd\DONE")
  $vcpkgRoot = if ($env:OB_VCPKG_DIR -and $env:OB_VCPKG_DIR.Trim().Length -gt 0) {
    $env:OB_VCPKG_DIR.TrimEnd('\', '/')
  } elseif ($depsDone) {
    Join-Path $RepoRoot "deps\3rd\vcpkg\x64-windows"
  } else {
    "C:/VcpkgInstalled/x64-windows"
  }
  Add-Dir (Join-Path $vcpkgRoot "bin")

  $opensslRoot = if ($env:OB_OPENSSL_DIR -and $env:OB_OPENSSL_DIR.Trim().Length -gt 0) {
    $env:OB_OPENSSL_DIR.TrimEnd('\', '/')
  } elseif ($depsDone) {
    Join-Path $RepoRoot "deps\3rd\openssl"
  } else {
    "C:/Program Files/OpenSSL-Win64"
  }
  Add-Dir (Join-Path $opensslRoot "bin")

  $vsagRoot = if ($env:OB_VSAG_DIR -and $env:OB_VSAG_DIR.Trim().Length -gt 0) {
    $env:OB_VSAG_DIR.TrimEnd('\', '/')
  } elseif ($depsDone) {
    Join-Path $RepoRoot "deps\3rd\vsag"
  } else {
    ""
  }
  if ($vsagRoot) { Add-Dir (Join-Path $vsagRoot "bin") }

  return @($dirs)
}

function Invoke-BundleRuntimeDllsForPack {
  param(
    [Parameter(Mandatory = $true)][string]$CmakeExe,
    [Parameter(Mandatory = $true)][string]$BundleScript,
    [Parameter(Mandatory = $true)][string]$DllPath,
    [Parameter(Mandatory = $true)][string]$OutLibsDir,
    [Parameter(Mandatory = $true)][string[]]$SearchDirs
  )
  New-Item -ItemType Directory -Path $OutLibsDir -Force | Out-Null
  $searchJoined = ($SearchDirs -join ';')
  & $CmakeExe `
    "-DEXE=$DllPath" `
    "-DOUT_DIR=$OutLibsDir" `
    "-DSEARCH_DIRS=$searchJoined" `
    -P $BundleScript
  if ($LASTEXITCODE -ne 0) {
    throw "BundleRuntimeDllsWindows.cmake failed with exit code $LASTEXITCODE"
  }
}

function Copy-ColocatedRuntimeDlls {
  param(
    [Parameter(Mandatory = $true)][string]$DllDir,
    [Parameter(Mandatory = $true)][string]$OutLibsDir
  )
  New-Item -ItemType Directory -Path $OutLibsDir -Force | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $DllDir -Filter "*.dll" -File -ErrorAction SilentlyContinue) {
    if ($item.Name -ieq "seekdb.dll" -or $item.Name -ieq "libseekdb.dll") { continue }
    $dest = Join-Path $OutLibsDir $item.Name
    if (-not (Test-Path -LiteralPath $dest)) {
      Copy-Item -LiteralPath $item.FullName -Destination $dest
    }
  }
}

$BuildDirName = Get-SeekDbWindowsBuildDirNameFromEnv

$WorkDir = if ($IncludeDir) {
  (Resolve-Path $IncludeDir).Path
} else {
  Join-Path $TopDir "build_$BuildDirName\src\include"
}

$BuildRoot = Join-Path $TopDir "build_$BuildDirName"
$Dll = $null
if ($IncludeDir) {
  foreach ($name in @("seekdb.dll", "libseekdb.dll")) {
    $p = Join-Path $WorkDir $name
    if (Test-Path -LiteralPath $p) { $Dll = $p; break }
  }
} else {
  $resolved = Find-SeekDbWindowsDll -RepoRoot $TopDir -BuildDirName $BuildDirName
  if ($resolved) { $Dll = $resolved.DllPath }
}

if (-not $Dll -or -not (Test-Path -LiteralPath $Dll)) {
  if (-not $IncludeDir) {
    Write-SeekDbWindowsDllDiagnostics -RepoRoot $TopDir -BuildDirName $BuildDirName
  }
  $hint = if ($IncludeDir) { $WorkDir } else { $BuildRoot }
  Write-Error "seekdb.dll not found under $hint (build libseekdb first: .\build.ps1 release --ninja --target libseekdb -DBUILD_EMBED_MODE=ON)"
}

$DllDir = Split-Path -Parent $Dll
$Lib = $null
foreach ($ln in @("seekdb.lib", "libseekdb.lib")) {
  $c = Join-Path $DllDir $ln
  if (Test-Path -LiteralPath $c) { $Lib = $c; break }
}

$Header = Join-Path $TopDir "src\include\seekdb.h"
if (-not (Test-Path $Header)) {
  Write-Error "seekdb.h not found: $Header"
}

$ZipName = "libseekdb-windows-x64.zip"
$OutZip = Join-Path $ScriptDir $ZipName
$Staging = Join-Path $env:TEMP ("libseekdb-pack-" + [guid]::NewGuid().ToString())
$LibsStaging = Join-Path $Staging "libs"
New-Item -ItemType Directory -Path $Staging -Force | Out-Null

try {
  $cmakeExe = Get-CMakeExecutable -RepoRoot $TopDir
  $bundleScript = Join-Path $TopDir "cmake\BundleRuntimeDllsWindows.cmake"
  if (-not (Test-Path -LiteralPath $bundleScript)) {
    Write-Error "Missing $bundleScript"
  }

  $searchDirs = Get-SeekDbWindowsRuntimeSearchDirs -RepoRoot $TopDir -DllDir $DllDir
  Write-Host "[libseekdb-build.ps1] Bundling runtime DLLs into libs/ (search: $($searchDirs.Count) dirs)"
  Invoke-BundleRuntimeDllsForPack `
    -CmakeExe $cmakeExe `
    -BundleScript $bundleScript `
    -DllPath $Dll `
    -OutLibsDir $LibsStaging `
    -SearchDirs $searchDirs

  # Include DLLs already colocated by libseekdb POST_BUILD (may overlap).
  Copy-ColocatedRuntimeDlls -DllDir $DllDir -OutLibsDir $LibsStaging

  $libCount = @(Get-ChildItem -LiteralPath $LibsStaging -Filter "*.dll" -File -ErrorAction SilentlyContinue).Count
  if ($libCount -lt 1) {
    throw "No runtime DLLs under $LibsStaging — cannot produce a standalone Windows zip. Check vcpkg/OpenSSL paths and BundleRuntimeDllsWindows.cmake output."
  }
  Write-Host "[libseekdb-build.ps1] libs/ contains $libCount runtime DLL(s)"

  Copy-Item $Header (Join-Path $Staging "seekdb.h")
  Copy-Item $Dll (Join-Path $Staging "seekdb.dll")
  if ($Lib -and (Test-Path -LiteralPath $Lib)) {
    Copy-Item $Lib (Join-Path $Staging "seekdb.lib")
  } else {
    Write-Host "[libseekdb-build.ps1][WARN] seekdb.lib not found; zip will contain DLL + header only." -ForegroundColor Yellow
  }

  if (Test-Path $OutZip) { Remove-Item -Force $OutZip }
  Compress-Archive -Path (Join-Path $Staging "*") -DestinationPath $OutZip
  Write-Host "[libseekdb-build.ps1] Created $OutZip"
} finally {
  Remove-Item -Recurse -Force $Staging -ErrorAction SilentlyContinue
}
