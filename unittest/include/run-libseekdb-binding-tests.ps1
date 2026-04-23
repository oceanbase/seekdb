#Requires -Version 5.1
<#
  Run libseekdb FFI binding tests on Windows (PowerShell).
  Requires: seekdb.dll under <repo>/build_release/src/include
  Requires for full suite: gcc (MinGW) for Go CGO, mvn for Java JNI.

  -ContinueOnError runs every language section even after a failure; exit code is non-zero if any section failed.
#>
param(
  [Parameter(Mandatory = $false)]
  [string]$RepoRoot = "",
  [Parameter(Mandatory = $false)]
  [switch]$ContinueOnError
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RepoRoot {
  if ($RepoRoot) { return (Resolve-Path $RepoRoot).Path }
  return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$root = Get-RepoRoot
$libDir = Join-Path $root "build_release\src\include"
$dllPath = Join-Path $libDir "seekdb.dll"
if (-not (Test-Path $dllPath)) {
  throw "seekdb.dll not found at $dllPath — build libseekdb first."
}

$env:SEEKDB_LIB_PATH = $dllPath
# seekdb.dll imports vcpkg DLLs (e.g. abseil_dll.dll) via *.lib stubs — those live under vcpkg\bin.
# Match build.ps1 resolution so Loader / Python ctypes can find transitive deps (same as OB_VCPKG_DIR in CMake).
$depsDone = Test-Path (Join-Path $root "deps\3rd\DONE")
$vcpkgRoot = if ($env:OB_VCPKG_DIR -and $env:OB_VCPKG_DIR.Trim().Length -gt 0) {
  $env:OB_VCPKG_DIR.TrimEnd('\', '/')
} elseif ($depsDone) {
  (Join-Path $root "deps\3rd\vcpkg\x64-windows")
} else {
  "C:/VcpkgInstalled/x64-windows"
}
$vcpkgBin = Join-Path $vcpkgRoot "bin"

$opensslRoot = if ($env:OB_OPENSSL_DIR -and $env:OB_OPENSSL_DIR.Trim().Length -gt 0) {
  $env:OB_OPENSSL_DIR.TrimEnd('\', '/')
} elseif ($depsDone) {
  (Join-Path $root "deps\3rd\openssl")
} else {
  "C:/Program Files/OpenSSL-Win64"
}
$opensslBin = Join-Path $opensslRoot "bin"

# ctypes on Python 3.8+ needs os.add_dll_directory (see unittest/include/python/seekdb.py), not only PATH.
if (Test-Path $vcpkgBin) {
  $env:SEEKDB_VCPKG_BIN = $vcpkgBin
}
if (Test-Path $opensslBin) {
  $env:SEEKDB_OPENSSL_BIN = $opensslBin
}

$pathLead = @($libDir)
if (Test-Path $opensslBin) { $pathLead = @($opensslBin) + $pathLead }
if (Test-Path $vcpkgBin) { $pathLead = @($vcpkgBin) + $pathLead }
$env:PATH = (($pathLead -join ';') + ';' + $env:PATH)
$env:CGO_ENABLED = "1"

Write-Host "=== libseekdb Windows binding tests ==="
Write-Host "Repo: $root"
Write-Host "SEEKDB_LIB_PATH=$($env:SEEKDB_LIB_PATH)"
if ($env:SEEKDB_VCPKG_BIN) { Write-Host "SEEKDB_VCPKG_BIN=$($env:SEEKDB_VCPKG_BIN)" }
if ($env:SEEKDB_OPENSSL_BIN) { Write-Host "SEEKDB_OPENSSL_BIN=$($env:SEEKDB_OPENSSL_BIN)" }
Write-Host ""

$bindingFailures = New-Object System.Collections.ArrayList

function Write-BindLog {
  param([string]$Message)
  $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
  Write-Host "[$ts] [seekdb-bind] $Message"
  try { [Console]::Out.Flush() } catch {}
}

# Stream npm lines to CI log (native npm output can appear buffered otherwise).
function Install-NodeBindingDeps {
  Write-BindLog "npm: preparing in $(Get-Location)"
  Write-Host "::notice::Installing Node deps under $(Get-Location) — first run can take several minutes (download + optional native build for koffi)."
  Write-BindLog "npm: node/npm versions"
  node --version | ForEach-Object { Write-Host "[node] $_"; Write-BindLog "node $_" }
  npm --version | ForEach-Object { Write-Host "[npm] $_"; Write-BindLog "npm $_" }

  # Do not pipe npm into ForEach-Object — exit code becomes unreliable on Windows PowerShell.
  if (Test-Path "package-lock.json") {
    Write-BindLog "npm: starting npm ci (verbose; do not pipe — preserves exit code)"
    & npm ci --no-audit --no-fund --foreground-scripts --loglevel verbose
  } else {
    Write-BindLog "npm: starting npm install (verbose)"
    & npm install --no-audit --no-fund --foreground-scripts --loglevel verbose
  }

  if ($LASTEXITCODE -ne 0) {
    Write-BindLog "npm FAILED exit=$LASTEXITCODE in $(Get-Location)"
    throw "npm failed in $(Get-Location) (exit $LASTEXITCODE)"
  }
  Write-BindLog "npm: finished OK in $(Get-Location)"
}

function Invoke-BindingSection {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [Parameter(Mandatory = $true)]
    [scriptblock]$Script
  )
  Write-BindLog "SECTION START: $Name"
  Write-Host "--- $Name ---"
  try {
    & $Script
    Write-BindLog "SECTION END OK: $Name"
  } catch {
    $msg = $_.Exception.Message
    Write-BindLog "SECTION FAILED: $Name — $msg"
    Write-Host "::error::$Name — $msg"
    [void]$bindingFailures.Add($Name)
    if (-not $ContinueOnError) {
      throw $_
    }
  }
}

Invoke-BindingSection "Python" {
  Push-Location (Join-Path $root "unittest\include\python")
  try {
    Write-Host "::group::Python binding tests"
    try {
      if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
      if (Test-Path "seekdb_abs.db") { Remove-Item -Recurse -Force "seekdb_abs.db" }
      $pyExe = (Get-Command python).Source
      Write-BindLog "Python: running $pyExe -u test.py ..."
      & $pyExe -u test.py ".\seekdb.db" "test"
      $pyExit = $LASTEXITCODE
      Write-BindLog "Python: LASTEXITCODE=$pyExit"
      Write-Host "::notice::Python binding tests finished (exit $pyExit). Next: Node FFI (npm may run silently for several minutes)."
      if ($pyExit -ne 0) { throw "Python tests failed: $pyExit" }
    }
    finally {
      Write-Host "::endgroup::"
    }
  }
  finally {
    Pop-Location
    Write-BindLog "Python: Pop-Location done"
  }
}

Invoke-BindingSection "Node.js FFI (koffi)" {
  Push-Location (Join-Path $root "unittest\include\nodejs")
  try {
    Write-Host "::notice::Starting Node.js FFI (koffi): npm ci/install next — log may be quiet for minutes while packages download or compile."
    Write-Host "::group::Node FFI — npm install"
    try {
      Install-NodeBindingDeps
    }
    finally {
      Write-Host "::endgroup::"
    }
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    Write-BindLog "Node FFI: running node test.js (relative db path)"
    node test.js ".\seekdb.db" "test"
    Write-BindLog "Node FFI: relative run exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Node FFI tests (relative path) failed: $LASTEXITCODE" }
    $absDb = Join-Path $PWD.Path "seekdb_abs.db"
    if (Test-Path $absDb) { Remove-Item -Recurse -Force $absDb }
    Write-BindLog "Node FFI: running node test.js (absolute db path)"
    node test.js $absDb "test"
    Write-BindLog "Node FFI: absolute run exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Node FFI tests (absolute path) failed: $LASTEXITCODE" }
  }
  finally {
    Pop-Location
  }
}

Invoke-BindingSection "Node.js N-API" {
  Push-Location (Join-Path $root "unittest\include\nodejs_napi")
  try {
    Install-NodeBindingDeps
    $py = (Get-Command python).Source
    npm config set python $py
    Write-BindLog "N-API: npm config set python -> $py"
    Write-Host "::notice::node-gyp rebuild — streaming output below."
    Write-BindLog "N-API: starting npx node-gyp rebuild --verbose (same process stream; preserves exit code)"
    npx --yes node-gyp rebuild --verbose
    Write-BindLog "N-API: node-gyp rebuild exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "node-gyp rebuild failed: $LASTEXITCODE" }
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    Write-BindLog "N-API: node test.js relative"
    node test.js ".\seekdb.db" "test"
    Write-BindLog "N-API: relative exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Node N-API tests (relative path) failed: $LASTEXITCODE" }
    $absDb = Join-Path $PWD.Path "seekdb_abs.db"
    if (Test-Path $absDb) { Remove-Item -Recurse -Force $absDb }
    Write-BindLog "N-API: node test.js absolute"
    node test.js $absDb "test"
    Write-BindLog "N-API: absolute exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Node N-API tests (absolute path) failed: $LASTEXITCODE" }
  }
  finally {
    Pop-Location
  }
}

Invoke-BindingSection "Rust" {
  Push-Location (Join-Path $root "unittest\include\rust")
  try {
    $env:RUSTFLAGS = "-L native=$libDir"
    Write-BindLog "Rust: cargo build --bin test"
    cargo build --bin test
    Write-BindLog "Rust: cargo build exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "cargo build failed: $LASTEXITCODE" }
    $exe = Join-Path $PWD.Path "target\debug\test.exe"
    if (-not (Test-Path $exe)) { throw "Rust test binary not found: $exe" }
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    Write-BindLog "Rust: running test.exe relative"
    & $exe ".\seekdb.db" "test"
    Write-BindLog "Rust: relative exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Rust tests (relative path) failed: $LASTEXITCODE" }
    $absDb = Join-Path $PWD.Path "seekdb_abs.db"
    if (Test-Path $absDb) { Remove-Item -Recurse -Force $absDb }
    Write-BindLog "Rust: running test.exe absolute"
    & $exe $absDb "test"
    Write-BindLog "Rust: absolute exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Rust tests (absolute path) failed: $LASTEXITCODE" }
  }
  finally {
    Remove-Item Env:\RUSTFLAGS -ErrorAction SilentlyContinue
    Pop-Location
  }
}

Invoke-BindingSection "Go" {
  if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    throw "gcc not found on PATH (required for Go CGO). Install MinGW (e.g. choco install mingw)."
  }
  Push-Location (Join-Path $root "unittest\include\go")
  try {
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    Write-BindLog "Go: go run relative"
    go run test.go ".\seekdb.db"
    Write-BindLog "Go: relative exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Go tests (relative path) failed: $LASTEXITCODE" }
    $absDb = Join-Path $PWD.Path "seekdb_abs.db"
    if (Test-Path $absDb) { Remove-Item -Recurse -Force $absDb }
    Write-BindLog "Go: go run absolute"
    go run test.go $absDb
    Write-BindLog "Go: absolute exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Go tests (absolute path) failed: $LASTEXITCODE" }
  }
  finally {
    Pop-Location
  }
}

Invoke-BindingSection "Java" {
  if (-not (Get-Command mvn -ErrorAction SilentlyContinue)) {
    throw "mvn not found on PATH (required for Java tests). Install Maven."
  }
  $javaDir = Join-Path $root "unittest\include\java"
  Push-Location $javaDir
  try {
    New-Item -ItemType Directory -Force -Path "build" | Out-Null
    Write-BindLog "Java JNI: cmake configure"
    cmake -S . -B build
    Write-BindLog "Java JNI: cmake configure exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure (JNI) failed: $LASTEXITCODE" }
    Write-BindLog "Java JNI: cmake --build"
    cmake --build build --config Release --parallel
    Write-BindLog "Java JNI: cmake build exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "CMake build (JNI) failed: $LASTEXITCODE" }
    $jniRelease = Join-Path $javaDir "build\Release\seekdb_jni.dll"
    $jniRoot = Join-Path $javaDir "build\seekdb_jni.dll"
    $jniDir = ""
    if (Test-Path $jniRelease) {
      $jniDir = Split-Path $jniRelease
    } elseif (Test-Path $jniRoot) {
      $jniDir = Split-Path $jniRoot
    } else {
      throw "seekdb_jni.dll not found under unittest/include/java/build/"
    }
    Write-BindLog "Java JNI: mvn compile test-compile"
    mvn -q compile test-compile
    Write-BindLog "Java JNI: mvn exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "mvn compile failed: $LASTEXITCODE" }
    $javaLibPath = "${jniDir};${libDir}"
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    Write-BindLog "Java JNI: java SeekdbTest relative"
    java "-Djava.library.path=$javaLibPath" -cp "target/classes;target/test-classes" seekdb.SeekdbTest ".\seekdb.db"
    Write-BindLog "Java JNI: java relative exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Java tests (relative path) failed: $LASTEXITCODE" }
    $absDb = Join-Path $PWD.Path "seekdb_abs.db"
    if (Test-Path $absDb) { Remove-Item -Recurse -Force $absDb }
    Write-BindLog "Java JNI: java SeekdbTest absolute"
    java "-Djava.library.path=$javaLibPath" -cp "target/classes;target/test-classes" seekdb.SeekdbTest $absDb
    Write-BindLog "Java JNI: java absolute exit=$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { throw "Java tests (absolute path) failed: $LASTEXITCODE" }
  }
  finally {
    Pop-Location
  }
}

Write-Host ""
if ($bindingFailures.Count -gt 0) {
  Write-Host "::error::Binding test failures: $($bindingFailures -join ', ')"
  exit 1
}
Write-Host "All Windows binding tests completed successfully."
