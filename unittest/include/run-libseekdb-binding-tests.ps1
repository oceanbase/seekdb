#Requires -Version 5.1
<#
  Run libseekdb FFI binding tests on Windows (PowerShell).
  Resolves seekdb.dll (or libseekdb.dll): env SEEKDB_LIB_PATH if set and valid; else build_<release|debug> via seekdb-windows-dll-resolve.ps1 (include, bin, lib, build.ninja, src recurse, then bounded full-tree on PS 7+).
  Requires for full suite: gcc (MinGW) for Go CGO, mvn for Java JNI.

  -ContinueOnError runs every language section even after a failure; exit code is non-zero if any section failed.

  CI may set SEEKDB_BINDING_SECTION to Python | NodeFfi | NodeNapi | Rust | Go | Java to run one language per job step
  (pinpoints which toolchain hangs). Omit or All runs the full suite in one process (local default).
#>
param(
  [Parameter(Mandatory = $false)]
  [string]$RepoRoot = "",
  [Parameter(Mandatory = $false)]
  [switch]$ContinueOnError,
  # Run only one language section (use env SEEKDB_BINDING_SECTION in CI). Default All = full suite in one process.
  [Parameter(Mandatory = $false)]
  [ValidateSet('All', 'Python', 'NodeFfi', 'NodeNapi', 'Rust', 'Go', 'Java')]
  [string]$BindSection = 'All'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:BindSectionMode = $BindSection
if ($env:SEEKDB_BINDING_SECTION -and $env:SEEKDB_BINDING_SECTION.Trim().Length -gt 0) {
  $script:BindSectionMode = $env:SEEKDB_BINDING_SECTION.Trim()
}
$_allowedBind = @('All', 'Python', 'NodeFfi', 'NodeNapi', 'Rust', 'Go', 'Java')
if ($_allowedBind -notcontains $script:BindSectionMode) {
  throw "Invalid BindSection / SEEKDB_BINDING_SECTION: '$script:BindSectionMode'. Use: $($_allowedBind -join ', ')"
}
function Test-BindSection {
  param([Parameter(Mandatory = $true)][string]$Name)
  if ($script:BindSectionMode -eq 'All') { return $true }
  return ($script:BindSectionMode -eq $Name)
}

function Get-RepoRoot {
  if ($RepoRoot) { return (Resolve-Path $RepoRoot).Path }
  return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$root = Get-RepoRoot

. (Join-Path $PSScriptRoot "seekdb-windows-dll-resolve.ps1")

$dllPath = ""
$libDir = ""
$pre = if ($env:SEEKDB_LIB_PATH) { $env:SEEKDB_LIB_PATH.Trim() } else { "" }
if ($pre -and (Test-Path -LiteralPath $pre) -and ($pre -match '\.[Dd][Ll][Ll]$')) {
  $dllPath = (Resolve-Path -LiteralPath $pre).Path
  $libDir = Split-Path -Parent $dllPath
}
if (-not $dllPath) {
  $bdn = Get-SeekDbWindowsBuildDirNameFromEnv
  $resolved = Find-SeekDbWindowsDll -RepoRoot $root -BuildDirName $bdn
  if (-not $resolved) {
    Write-SeekDbWindowsDllDiagnostics -RepoRoot $root -BuildDirName $bdn
    throw "seekdb.dll not found under $(Join-Path $root "build_$bdn") — build libseekdb first."
  }
  $dllPath = $resolved.DllPath
  $libDir = $resolved.LibDir
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

$vsagRoot = if ($env:OB_VSAG_DIR -and $env:OB_VSAG_DIR.Trim().Length -gt 0) {
  $env:OB_VSAG_DIR.TrimEnd('\', '/')
} elseif ($depsDone) {
  (Join-Path $root "deps\3rd\vsag")
} else {
  ""
}
$vsagBin = if ($vsagRoot) { Join-Path $vsagRoot "bin" } else { "" }

# ctypes on Python 3.8+ needs os.add_dll_directory (see unittest/include/python/seekdb.py), not only PATH.
if (Test-Path $vcpkgBin) {
  $env:SEEKDB_VCPKG_BIN = $vcpkgBin
}
if (Test-Path $opensslBin) {
  $env:SEEKDB_OPENSSL_BIN = $opensslBin
}
if ($vsagBin -and (Test-Path $vsagBin)) {
  $env:SEEKDB_VSAG_BIN = $vsagBin
}

$pathLead = @($libDir)
if ($vsagBin -and (Test-Path $vsagBin)) { $pathLead = @($vsagBin) + $pathLead }
if (Test-Path $opensslBin) { $pathLead = @($opensslBin) + $pathLead }
if (Test-Path $vcpkgBin) { $pathLead = @($vcpkgBin) + $pathLead }
$env:PATH = (($pathLead -join ';') + ';' + $env:PATH)
$env:CGO_ENABLED = "1"

Write-Host "=== libseekdb Windows binding tests ==="
Write-Host "Repo: $root"
Write-Host "SEEKDB_LIB_PATH=$($env:SEEKDB_LIB_PATH)"
if ($env:SEEKDB_VCPKG_BIN) { Write-Host "SEEKDB_VCPKG_BIN=$($env:SEEKDB_VCPKG_BIN)" }
if ($env:SEEKDB_OPENSSL_BIN) { Write-Host "SEEKDB_OPENSSL_BIN=$($env:SEEKDB_OPENSSL_BIN)" }
if ($env:SEEKDB_VSAG_BIN) { Write-Host "SEEKDB_VSAG_BIN=$($env:SEEKDB_VSAG_BIN)" }
Write-Host ""

$bindingFailures = New-Object System.Collections.ArrayList

# Shared process machinery (Write-BindLog / Wait-ProcessWithDeadline / Start-NpmProcess / Invoke-ExternalTestWithBindingExitProbe):
# also dot-sourced by package/libseekdb/test-packed-artifact-smoke.ps1 — keep edits in both consumers in sync.
. (Join-Path $PSScriptRoot "windows-process-utils.ps1")

# Absolute-path validation runs inside each language test (same process as python test.py abs_same), not a second child — avoids Windows pidfile / lock issues.

# Prefer JDK from JAVA_HOME (GitHub setup-java / Temurin); plain `java` on PATH may be an older JRE (class file mismatch).
function Get-JavaExecutable {
  $try = Join-Path $env:JAVA_HOME 'bin\java.exe'
  if ($env:JAVA_HOME -and (Test-Path -LiteralPath $try)) {
    return $try
  }
  return (Get-Command java).Source
}

# Stream npm lines to CI log (native npm output can appear buffered otherwise).
function Install-NodeBindingDeps {
  Write-BindLog "npm: preparing in $(Get-Location)"
  Write-Host "::notice::Installing Node deps under $(Get-Location) — first run can take several minutes (download + optional native build for koffi)."
  Write-BindLog "npm: node/npm versions"
  node --version | ForEach-Object { Write-Host "[node] $_"; Write-BindLog "node $_" }
  npm --version | ForEach-Object { Write-Host "[npm] $_"; Write-BindLog "npm $_" }

  # Avoid indefinite hangs on stalled registry downloads (does not fix stuck native postinstall builds).
  try {
    npm config set fetch-timeout 600000 2>$null
    npm config set fetch-retries 5 2>$null
  }
  catch {}

  $npmTimeoutMs = 2400000
  if ($env:SEEKDB_NODE_NPM_TIMEOUT_MS -match '^\d+$') {
    $npmTimeoutMs = [int]$env:SEEKDB_NODE_NPM_TIMEOUT_MS
  }
  Write-BindLog "npm: wall-clock timeout ${npmTimeoutMs}ms"

  if (Test-Path "package-lock.json") {
    Write-BindLog "npm: starting npm ci (verbose; Start-Process + deadline)"
    $argList = @('ci', '--no-audit', '--no-fund', '--foreground-scripts', '--loglevel', 'verbose')
  }
  else {
    Write-BindLog "npm: starting npm install (verbose; Start-Process + deadline)"
    $argList = @('install', '--no-audit', '--no-fund', '--foreground-scripts', '--loglevel', 'verbose')
  }
  $p = Start-NpmProcess -NpmArguments $argList -WorkingDirectory (Get-Location).Path
  if ($null -eq $p) {
    throw "Start-Process npm returned null"
  }
  $r = Wait-ProcessWithDeadline -Process $p -TimeoutMs $npmTimeoutMs -Label "npm ci/install" -HeartbeatSec 120
  if ($r['TimedOut']) {
    Write-BindLog "npm FAILED: timed out after ${npmTimeoutMs} ms in $(Get-Location)"
    throw "npm ci/install timed out after ${npmTimeoutMs} ms"
  }
  if ($r['ExitCode'] -ne 0) {
    Write-BindLog "npm FAILED exit=$($r['ExitCode']) in $(Get-Location)"
    throw "npm failed in $(Get-Location) (exit $($r['ExitCode']))"
  }
  Write-BindLog "npm: finished OK in $(Get-Location)"
}

function Invoke-NodeWithDeadline {
  param(
    [Parameter(Mandatory = $true)]
    [string[]]$NodeArgs,
    [Parameter(Mandatory = $true)]
    [string]$Description
  )
  $nodeExe = (Get-Command node).Source
  Invoke-ExternalTestWithBindingExitProbe -FilePath $nodeExe -ArgumentList $NodeArgs -Description "node $Description"
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

if (-not (Test-BindSection 'Python')) { Write-BindLog "SKIP section Python (BindSection=$script:BindSectionMode)" }
if (Test-BindSection 'Python') {
Invoke-BindingSection "Python" {
  Push-Location (Join-Path $root "unittest\include\python")
  try {
    Write-Host "::group::Python binding tests"
    try {
      if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
      $pyExe = (Get-Command python).Source
      # Stream directly to the runner log. Do NOT use PowerShell *> file redirect here: embedding + native
      # threads writing stdout/stderr to one redirected file has caused indefinite hangs on Windows CI.
      Write-BindLog "Python: running $pyExe -u test.py (stdout/stderr inherit host — live stream for CI) ..."
      $env:PYTHONUNBUFFERED = "1"
      # If python.exe never returns, the whole CI step never finishes. Use Start-Process + wall-clock deadline, then taskkill /T; exit 1 stops the script (do not continue to Node).
      $timeoutMs = 600000
      if ($env:SEEKDB_BINDING_TEST_TIMEOUT_MS -match '^\d+$') {
        $timeoutMs = [int]$env:SEEKDB_BINDING_TEST_TIMEOUT_MS
      }
      $graceMs = 15000
      if ($env:SEEKDB_NODE_POST_SUCCESS_FORCE_KILL_MS -match '^\d+$') {
        $graceMs = [int]$env:SEEKDB_NODE_POST_SUCCESS_FORCE_KILL_MS
      }
      Write-BindLog "Python: wall-clock timeout ${timeoutMs}ms; exit-probe grace ${graceMs}ms"
      # Pass full process environment explicitly — GHA + Start-Process can drop session env (SEEKDB_* / PATH).
      $childEnv = [System.Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
      Get-ChildItem -Path Env: | ForEach-Object { $childEnv[$_.Name] = [string]$_.Value }
      $prevProbe = $env:SEEKDB_BINDING_EXIT_PROBE
      $childEnv['SEEKDB_BINDING_EXIT_PROBE'] = '1'
      $env:SEEKDB_BINDING_EXIT_PROBE = '1'
      try {
        $p = Start-Process -FilePath $pyExe -ArgumentList @('-u', 'test.py', '.\seekdb.db', 'test') -WorkingDirectory (Get-Location) -PassThru -NoNewWindow -Environment $childEnv
        if ($null -eq $p) {
          throw "Start-Process python returned null"
        }
        $wr = Wait-ProcessWithDeadline -Process $p -TimeoutMs $timeoutMs -Label "python test.py" -HeartbeatSec 60 -UseBindingExitProbe -BindingExitProbeGraceMs $graceMs
      }
      finally {
        if ($null -eq $prevProbe) {
          Remove-Item Env:\SEEKDB_BINDING_EXIT_PROBE -ErrorAction SilentlyContinue
        }
        else {
          $env:SEEKDB_BINDING_EXIT_PROBE = $prevProbe
        }
      }
      $pythonTimedOut = [bool]($wr['TimedOut'])
      $pyExit = if ($pythonTimedOut) { -1 } else { $wr['ExitCode'] }
      if ($wr['ForcedAfterProbe']) {
        Write-Host "::notice::[seekdb-bind] python test.py — exit code from probe $pyExit (process did not terminate; likely DLL unload hang)"
      }
      if ($pythonTimedOut) {
        Write-Host "::error::Python binding tests exceeded ${timeoutMs} ms"
      }
      Remove-Item Env:\PYTHONUNBUFFERED -ErrorAction SilentlyContinue
      if ($pythonTimedOut) {
        Write-Host "::error::Stopping run-libseekdb-binding-tests.ps1 after Python timeout (downstream languages skipped)."
        # Nested scriptblock: plain exit can be scoped oddly; ExitProcess guarantees the CI step ends.
        [System.Environment]::Exit(1)
      }
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
  Write-BindLog "=== Python block finished; about to chdir to unittest\\include\\nodejs and run npm (no output is normal for several minutes on a cold run) ==="
  Write-Host "::notice::[seekdb-bind] If the log looks idle after Python passed, the job is almost certainly in Node npm (download) or a native dependency build, not in Python. Timestamps below are from the runner."
}
}

if (-not (Test-BindSection 'NodeFfi')) { Write-BindLog "SKIP section NodeFfi (BindSection=$script:BindSectionMode)" }
if (Test-BindSection 'NodeFfi') {
Invoke-BindingSection "Node.js FFI (koffi)" {
  Push-Location (Join-Path $root "unittest\include\nodejs")
  try {
    Write-BindLog "Entering nodejs/ — will run npm ci or npm install (this can be silent 5–15+ minutes on Windows)"
    Write-Host "::notice::Starting Node.js FFI (koffi): npm ci/install next — log may be quiet for minutes while packages download or compile."
    Write-Host "::group::Node FFI — npm install"
    try {
      Install-NodeBindingDeps
    }
    finally {
      Write-Host "::endgroup::"
    }
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    Write-BindLog "Node FFI: node test.js .\\seekdb.db (includes in-process absolute-path check)"
    Invoke-NodeWithDeadline -NodeArgs @('test.js', '.\seekdb.db', 'test') -Description 'FFI test.js'
  }
  finally {
    Pop-Location
  }
}
}

if (-not (Test-BindSection 'NodeNapi')) { Write-BindLog "SKIP section NodeNapi (BindSection=$script:BindSectionMode)" }
if (Test-BindSection 'NodeNapi') {
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
    Write-BindLog "N-API: node test.js .\\seekdb.db (includes in-process absolute-path check)"
    Invoke-NodeWithDeadline -NodeArgs @('test.js', '.\seekdb.db', 'test') -Description 'N-API test.js'
  }
  finally {
    Pop-Location
  }
}
}

if (-not (Test-BindSection 'Rust')) { Write-BindLog "SKIP section Rust (BindSection=$script:BindSectionMode)" }
if (Test-BindSection 'Rust') {
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
    Write-BindLog "Rust: test.exe .\\seekdb.db (includes in-process absolute-path check)"
    Invoke-ExternalTestWithBindingExitProbe -FilePath $exe -ArgumentList @('.\seekdb.db', 'test') -Description 'Rust test.exe'
  }
  finally {
    Remove-Item Env:\RUSTFLAGS -ErrorAction SilentlyContinue
    Pop-Location
  }
}
}

if (-not (Test-BindSection 'Go')) { Write-BindLog "SKIP section Go (BindSection=$script:BindSectionMode)" }
if (Test-BindSection 'Go') {
Invoke-BindingSection "Go" {
  if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    throw "gcc not found on PATH (required for Go CGO). Install MinGW (e.g. choco install mingw)."
  }
  Push-Location (Join-Path $root "unittest\include\go")
  try {
    if (Test-Path "seekdb.db") { Remove-Item -Recurse -Force "seekdb.db" }
    # go run uses a different PID than the test process — exit probe must match Start-Process (build then run binary).
    $goExe = (Get-Command go).Source
    $goBin = Join-Path $env:TEMP "seekdb_go_binding_${PID}.exe"
    Write-BindLog "Go: go build -> $goBin"
    & $goExe build -o $goBin .
    if ($LASTEXITCODE -ne 0) { throw "go build failed: $LASTEXITCODE" }
    try {
      Write-BindLog "Go: binding test .\\seekdb.db (includes in-process absolute-path check)"
      Invoke-ExternalTestWithBindingExitProbe -FilePath $goBin -ArgumentList @('.\seekdb.db') -Description 'Go binding test'
    }
    finally {
      Remove-Item -LiteralPath $goBin -Force -ErrorAction SilentlyContinue
    }
  }
  finally {
    Pop-Location
  }
}
}

if (-not (Test-BindSection 'Java')) { Write-BindLog "SKIP section Java (BindSection=$script:BindSectionMode)" }
if (Test-BindSection 'Java') {
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
    $javaExe = Get-JavaExecutable
    Write-BindLog "Java JNI: SeekdbTest .\\seekdb.db (includes in-process absolute-path check)"
    Invoke-ExternalTestWithBindingExitProbe -FilePath $javaExe -ArgumentList @(
      "-Djava.library.path=$javaLibPath",
      '-cp', 'target/classes;target/test-classes',
      'seekdb.SeekdbTest',
      '.\seekdb.db'
    ) -Description 'Java SeekdbTest'
  }
  finally {
    Pop-Location
  }
}
}

Write-Host ""
if ($bindingFailures.Count -gt 0) {
  Write-Host "::error::Binding test failures: $($bindingFailures -join ', ')"
  exit 1
}
Write-Host "All Windows binding tests completed successfully."
