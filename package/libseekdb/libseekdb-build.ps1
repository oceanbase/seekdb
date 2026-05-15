#Requires -Version 5.1
<#
.SYNOPSIS
  Pack libseekdb for Windows into libseekdb-windows-x64.zip (seekdb.h, seekdb.dll, seekdb.lib).

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
New-Item -ItemType Directory -Path $Staging -Force | Out-Null

try {
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
