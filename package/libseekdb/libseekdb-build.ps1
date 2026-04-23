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
$BuildType = if ($env:BUILD_TYPE) { $env:BUILD_TYPE } else { "release" }

$WorkDir = if ($IncludeDir) {
  (Resolve-Path $IncludeDir).Path
} else {
  Join-Path $TopDir "build_$BuildType\src\include"
}

$Dll = Join-Path $WorkDir "seekdb.dll"
$Lib = Join-Path $WorkDir "seekdb.lib"
if (-not (Test-Path $Dll)) {
  Write-Error "seekdb.dll not found under $WorkDir (build libseekdb first: .\build.ps1 release --ninja --target libseekdb -DBUILD_EMBED_MODE=ON)"
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
  if (Test-Path $Lib) {
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
