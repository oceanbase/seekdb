#Requires -Version 5.1
<#
.SYNOPSIS
  Pack libseekdb for Windows into libseekdb-windows-x64.zip (seekdb.h, seekdb.dll, seekdb.lib, libs/*.dll).

  The Bazel target //src/observer:libseekdb declares the seekdb.lib import library and symlinks the runtime
  DLLs (OpenSSL, vcpkg, ...) next to seekdb.dll, so the pack only stages what the build already produced.
  macOS packs deps under libs/ via dylibbundler; the Windows zip must match that layout for standalone
  embed consumers.

.EXAMPLE
  cd package\libseekdb
  .\libseekdb-build.ps1
  .\libseekdb-build.ps1 -IncludeDir C:\path\to\build_release\bin\src\observer
#>
param(
  [string]$IncludeDir = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TopDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
. (Join-Path $TopDir "unittest\include\seekdb-windows-dll-resolve.ps1")

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
  Join-Path $TopDir "build_$BuildDirName\bin\src\observer"
}

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
  Write-Error "seekdb.dll not found under $WorkDir (build it first: python ./build.py build -o build_$BuildDirName //src/observer:libseekdb)"
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
  Write-Host "[libseekdb-build.ps1] Collecting runtime DLLs from $DllDir"
  Copy-ColocatedRuntimeDlls -DllDir $DllDir -OutLibsDir $LibsStaging

  $libCount = @(Get-ChildItem -LiteralPath $LibsStaging -Filter "*.dll" -File -ErrorAction SilentlyContinue).Count
  if ($libCount -lt 1) {
    throw "No runtime DLLs under $DllDir — cannot produce a standalone Windows zip. The Bazel link step symlinks them next to seekdb.dll (final_link runtime_dlls)."
  }
  Write-Host "[libseekdb-build.ps1] libs/ contains $libCount runtime DLL(s)"

  Copy-Item $Header (Join-Path $Staging "seekdb.h")
  Copy-Item $Dll (Join-Path $Staging "seekdb.dll")
  if ($Lib -and (Test-Path -LiteralPath $Lib)) {
    Copy-Item $Lib (Join-Path $Staging "seekdb.lib")
  } else {
    throw "seekdb.lib not found next to $Dll — the Bazel target must declare the import library (final_link import_library)."
  }

  if (Test-Path $OutZip) { Remove-Item -Force $OutZip }
  Compress-Archive -Path (Join-Path $Staging "*") -DestinationPath $OutZip
  Write-Host "[libseekdb-build.ps1] Created $OutZip"
} finally {
  Remove-Item -Recurse -Force $Staging -ErrorAction SilentlyContinue
}
