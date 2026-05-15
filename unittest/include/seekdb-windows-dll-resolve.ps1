# Dot-source only. Resolves seekdb.dll / libseekdb.dll under the CMake build tree (Windows; Ninja preferred).
# Match build.ps1: RelWithDebInfo -> build_release, Debug -> build_debug.

function Get-SeekDbWindowsBuildDirNameFromEnv {
  $raw = if ($env:BUILD_TYPE -and $env:BUILD_TYPE.Trim().Length -gt 0) { $env:BUILD_TYPE.Trim() } else { "release" }
  switch -Wildcard ($raw) {
    "RelWithDebInfo" { return "release" }
    "Debug"          { return "debug" }
    default          { return $raw.ToLowerInvariant() }
  }
}

function Find-SeekDbDllPathsFromNinja {
  param([Parameter(Mandatory = $true)][string]$BuildRoot)
  $ninja = Join-Path $BuildRoot "build.ninja"
  if (-not (Test-Path -LiteralPath $ninja)) { return @() }
  $found = [System.Collections.Generic.List[string]]::new()
  try {
    foreach ($line in [System.IO.File]::ReadLines($ninja)) {
      if ($line.Length -lt 8 -or -not $line.StartsWith("build ")) { continue }
      $colon = $line.IndexOf(":", 6)
      if ($colon -lt 0) { continue }
      $outPart = $line.Substring(6, $colon - 6).Trim()
      $pipe = $outPart.IndexOf("|")
      if ($pipe -ge 0) { $outPart = $outPart.Substring(0, $pipe).TrimEnd() }
      foreach ($token in $outPart -split "\s+") {
        if (-not $token) { continue }
        $leaf = Split-Path -Leaf $token
        if ($leaf -ne "seekdb.dll" -and $leaf -ne "libseekdb.dll") { continue }
        $full = if ([System.IO.Path]::IsPathRooted($token)) { $token } else { Join-Path $BuildRoot $token }
        $norm = $full.Replace("/", [System.IO.Path]::DirectorySeparatorChar)
        if (Test-Path -LiteralPath $norm) { $found.Add($norm) }
      }
    }
  } catch {
    # ignore parse / IO issues; other strategies still run
  }
  return @($found)
}

function Find-SeekDbWindowsDll {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirName
  )
  $buildRoot = Join-Path $RepoRoot "build_$BuildDirName"
  if (-not (Test-Path -LiteralPath $buildRoot)) { return $null }

  $preferred = Join-Path $RepoRoot "build_$BuildDirName\src\include"
  foreach ($name in @("seekdb.dll", "libseekdb.dll")) {
    $p = Join-Path $preferred $name
    if (Test-Path -LiteralPath $p) { return @{ DllPath = $p; LibDir = $preferred } }
  }

  foreach ($sub in @("bin", "lib")) {
    $d = Join-Path $buildRoot $sub
    if (-not (Test-Path -LiteralPath $d)) { continue }
    foreach ($name in @("seekdb.dll", "libseekdb.dll")) {
      $p = Join-Path $d $name
      if (Test-Path -LiteralPath $p) { return @{ DllPath = $p; LibDir = $d } }
    }
  }

  $ninjaHits = @(Find-SeekDbDllPathsFromNinja -BuildRoot $buildRoot)
  if ($ninjaHits.Count -gt 0) {
    $nPath = $ninjaHits[0]
    $dir = Split-Path -Parent $nPath
    return @{ DllPath = $nPath; LibDir = $dir }
  }

  $underSrc = Join-Path $buildRoot "src"
  if (Test-Path -LiteralPath $underSrc) {
    foreach ($pat in @("seekdb.dll", "libseekdb.dll")) {
      $hit = Get-ChildItem -LiteralPath $underSrc -Filter $pat -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
      if ($hit) { return @{ DllPath = $hit.FullName; LibDir = $hit.Directory.FullName } }
    }
  }

  # Full-tree search (.NET avoids PS -Depth limits / edge cases on deep trees).
  foreach ($pat in @("seekdb.dll", "libseekdb.dll")) {
    try {
      $arr = [System.IO.Directory]::GetFiles($buildRoot, $pat, [System.IO.SearchOption]::AllDirectories)
      if ($arr -and $arr.Length -gt 0) {
        $first = $arr[0]
        $dir = Split-Path -Parent $first
        return @{ DllPath = $first; LibDir = $dir }
      }
    } catch {
      # continue
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
  Write-Host "[seekdb-win] Diagnostics: build root = $buildRoot"
  if (-not (Test-Path -LiteralPath $buildRoot)) {
    Write-Host "[seekdb-win] build directory does not exist (configure/build may have failed or used a different BUILD_TYPE)."
    return
  }
  Write-Host "[seekdb-win] Top-level entries:"
  Get-ChildItem -LiteralPath $buildRoot -ErrorAction SilentlyContinue | Select-Object -First 40 Name, Mode | Format-Table -AutoSize
  $ninja = Join-Path $buildRoot "build.ninja"
  if (Test-Path -LiteralPath $ninja) {
    $n = @(Find-SeekDbDllPathsFromNinja -BuildRoot $buildRoot).Count
    Write-Host "[seekdb-win] build.ninja seekdb.dll path entries (existing files): $n"
  } else {
    Write-Host "[seekdb-win] build.ninja not found — CMake may have used another generator (see CMAKE_GENERATOR below) or Ninja was not on PATH at configure time."
  }
  $cacheFile = Join-Path $buildRoot "CMakeCache.txt"
  if (Test-Path -LiteralPath $cacheFile) {
    $gen = Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($gen) { Write-Host "[seekdb-win] $($gen.Line.Trim())" }
    $make = Select-String -Path $cacheFile -Pattern '^CMAKE_MAKE_PROGRAM:' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($make) { Write-Host "[seekdb-win] $($make.Line.Trim())" }
  }
  Write-Host "[seekdb-win] Sample *.dll under build tree (first 40, full recurse):"
  try {
    $dlls = [System.IO.Directory]::GetFiles($buildRoot, "*.dll", [System.IO.SearchOption]::AllDirectories)
    $nShow = [Math]::Min(40, $dlls.Length)
    for ($i = 0; $i -lt $nShow; $i++) {
      Write-Host "  $($dlls[$i])"
    }
    if ($dlls.Length -eq 0) { Write-Host "  (none)" }
  } catch {
    Write-Host "  (enumeration failed: $($_.Exception.Message))"
  }
}
