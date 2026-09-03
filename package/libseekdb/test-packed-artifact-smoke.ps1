#Requires -Version 5.1
<#
  Smoke-test a packed libseekdb Windows zip using the embedded Node.js load layout:
    <unpack>/seekdb.dll + <unpack>/libs/ + <unpack>/seekdb.node

  Mirrors package/libseekdb/test-packed-artifact-smoke.sh (linux/macos) on Windows:
    - unzip the artifact to a temp dir
    - prepend <unpack> and <unpack>/libs to PATH (seekdb.dll imports runtime DLLs from libs/;
      Windows DLL search does not recurse into subdirectories, so libs/ must be on PATH)
    - build seekdb.node into the unpack tree via smoke-loader (node-gyp rebuild --pack_dir=<unpack>)
    - run node smoke-vsag.js with wall-clock timeout + binding exit probe + taskkill /T fallback
      (vsag smoke has a stall history on Windows runners; the process guard is mandatory)
    - exit code 0/1

  Usage:
    .\test-packed-artifact-smoke.ps1 -Zip package\libseekdb\libseekdb-windows-x64.zip
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$Zip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TopDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$LoaderDir = Join-Path $ScriptDir "smoke-loader"
$Utils = Join-Path $TopDir "unittest\include\windows-process-utils.ps1"
if (-not (Test-Path -LiteralPath $Utils)) {
  throw "Missing shared module: $Utils"
}
. $Utils

function Write-SmokeLog {
  param([string]$Message)
  Write-Host "[smoke] $Message"
  try { [Console]::Out.Flush() } catch {}
}

if (-not (Test-Path -LiteralPath $Zip)) {
  Write-Host "::error::zip not found: $Zip"
  exit 1
}
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
  Write-Host "::error::node not found (install Node.js 18+)"
  exit 1
}

$UnpackDir = Join-Path $env:TEMP ("libseekdb-smoke-" + [guid]::NewGuid().ToString())
try {
  Write-SmokeLog "unpacking $Zip -> $UnpackDir"
  Expand-Archive -LiteralPath $Zip -DestinationPath $UnpackDir -Force

  $MainDll = Join-Path $UnpackDir "seekdb.dll"
  if (-not (Test-Path -LiteralPath $MainDll)) {
    Write-Host "::error::seekdb.dll not found in zip"
    Get-ChildItem -LiteralPath $UnpackDir | Out-Host
    exit 1
  }

  $LibsDir = Join-Path $UnpackDir "libs"
  if (Test-Path -LiteralPath $LibsDir) {
    $dllCount = @(Get-ChildItem -LiteralPath $LibsDir -Filter *.dll -File).Count
    Write-SmokeLog "layout: seekdb.dll + libs/ ($dllCount runtime DLLs)"
  }
  else {
    Write-SmokeLog "layout: seekdb.dll (no libs/)"
  }

  # seekdb.dll imports transitive runtime DLLs from libs/ (vcpkg/OpenSSL); Windows DLL search does
  # not recurse into subdirectories — both the unpack root and libs/ must be on PATH.
  $env:PATH = (($UnpackDir + ";" + $LibsDir + ";" + $env:PATH) -replace ";;", ";")

  # --- Build seekdb.node INTO the unpack tree (@loader_path equivalent) ---
  if (-not (Test-Path (Join-Path $LoaderDir "node_modules"))) {
    Write-SmokeLog "npm install (smoke-loader, no lifecycle build)"
    $p = Start-NpmProcess -NpmArguments @('install', '--ignore-scripts', '--no-audit', '--no-fund') -WorkingDirectory $LoaderDir
    if ($null -eq $p) { throw "Start-NpmProcess returned null" }
    $npmR = Wait-ProcessWithDeadline -Process $p -TimeoutMs 2400000 -Label "npm install (smoke-loader)" -HeartbeatSec 120
    if ($null -eq $npmR -or $npmR['TimedOut']) {
      Write-Host "::error::npm install (smoke-loader) timed out or failed"
      exit 1
    }
    if ($npmR['ExitCode'] -ne 0) {
      Write-Host "::error::npm install (smoke-loader) failed (exit $($npmR['ExitCode']))"
      exit 1
    }
  }

  Write-SmokeLog "building seekdb.node into unpack dir (pack_dir=$UnpackDir)"
  $p = Start-NpmProcess -NpmArguments @('exec', '--yes', '--', 'node-gyp', 'rebuild', "--pack_dir=$UnpackDir") -WorkingDirectory $LoaderDir
  if ($null -eq $p) { throw "Start-NpmProcess returned null" }
  $gypR = Wait-ProcessWithDeadline -Process $p -TimeoutMs 1800000 -Label "node-gyp rebuild (smoke-loader)" -HeartbeatSec 120
  if ($null -eq $gypR -or $gypR['TimedOut'] -or $gypR['ExitCode'] -ne 0) {
    Write-Host "::error::node-gyp rebuild failed (pack_dir=$UnpackDir)"
    exit 1
  }

  $NodeOut = Join-Path $UnpackDir "seekdb.node"
  if (-not (Test-Path -LiteralPath $NodeOut)) {
    Write-Host "::error::seekdb.node not produced in $UnpackDir"
    exit 1
  }

  # --- vsag + hybrid search (embedded N-API path) ---
  $DbDir = Join-Path $UnpackDir "smoke-seekdb.db"
  if (Test-Path -LiteralPath $DbDir) { Remove-Item -Recurse -Force $DbDir }
  Write-SmokeLog "vsag + hybrid search (embedded N-API path)"
  try {
    Push-Location $UnpackDir
    try {
      Invoke-ExternalTestWithBindingExitProbe `
        -FilePath (Get-Command node).Source `
        -ArgumentList @((Join-Path $LoaderDir "smoke-vsag.js"), $DbDir) `
        -Description 'smoke-vsag.js'
    }
    finally {
      Pop-Location
    }
  }
  catch {
    Write-Host "::error::smoke-vsag.js failed: $($_.Exception.Message)"
    exit 1
  }

  Write-SmokeLog "passed (packed zip load path + vsag)"
}
finally {
  if (Test-Path -LiteralPath $UnpackDir) {
    Remove-Item -Recurse -Force $UnpackDir -ErrorAction SilentlyContinue
  }
}