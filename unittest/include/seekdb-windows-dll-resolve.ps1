# Dot-source only. Resolves seekdb.dll / libseekdb.dll from the Bazel output base created by build.py.
# Match build.py -o build_release / -o build_debug: BUILD_TYPE release -> build_release, debug -> build_debug.
# build.py sets --symlink_prefix=<output_base>/, so build_<type>\bin points at the Bazel bin directory and the
# libseekdb package output lives at build_<type>\bin\src\observer.

function Get-SeekDbWindowsBuildDirNameFromEnv {
  $raw = if ($env:BUILD_TYPE -and $env:BUILD_TYPE.Trim().Length -gt 0) { $env:BUILD_TYPE.Trim() } else { "release" }
  switch -Wildcard ($raw) {
    "RelWithDebInfo" { return "release" }
    "Debug"          { return "debug" }
    default          { return $raw.ToLowerInvariant() }
  }
}

function Find-SeekDbWindowsDll {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirName
  )
  $buildRoot = Join-Path $RepoRoot "build_$BuildDirName"
  if (-not (Test-Path -LiteralPath $buildRoot)) { return $null }

  # Bazel package output (//src/observer:libseekdb), reached through the output-base symlink.
  foreach ($sub in @("bin\src\observer", "src\include")) {
    $dir = Join-Path $buildRoot $sub
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    foreach ($name in @("seekdb.dll", "libseekdb.dll")) {
      $p = Join-Path $dir $name
      if (Test-Path -LiteralPath $p) { return @{ DllPath = $p; LibDir = $dir } }
    }
  }

  # Fallback: locate the DLL anywhere under a bin/ tree of the output base.
  $binRoot = Join-Path $buildRoot "bin"
  if (Test-Path -LiteralPath $binRoot) {
    foreach ($pat in @("seekdb.dll", "libseekdb.dll")) {
      $hit = Get-ChildItem -LiteralPath $binRoot -Filter $pat -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
      if ($hit) { return @{ DllPath = $hit.FullName; LibDir = $hit.Directory.FullName } }
    }
  }

  return $null
}

function Write-SeekDbWindowsDllDiagnostics {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirName
  )
  $buildRoot = Join-Path $RepoRoot "build_$BuildDirName"
  Write-Host "[seekdb-win] Diagnostics: Bazel output base = $buildRoot"
  if (-not (Test-Path -LiteralPath $buildRoot)) {
    Write-Host "[seekdb-win] output base does not exist — run: python ./build.py build -o build_$BuildDirName //src/observer:libseekdb"
    return
  }
  Write-Host "[seekdb-win] Top-level entries:"
  Get-ChildItem -LiteralPath $buildRoot -ErrorAction SilentlyContinue | Select-Object -First 40 Name, Mode | Format-Table -AutoSize
  $libDir = Join-Path $buildRoot "bin\src\observer"
  Write-Host "[seekdb-win] package output dir $libDir exists: $(Test-Path -LiteralPath $libDir)"
  if (Test-Path -LiteralPath $libDir) {
    Write-Host "[seekdb-win] entries (first 40):"
    Get-ChildItem -LiteralPath $libDir -ErrorAction SilentlyContinue | Select-Object -First 40 Name, Length | Format-Table -AutoSize
  }
}
