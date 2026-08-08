#Requires -Version 5.1
<#
  Deterministic facts for Windows libseekdb CI debugging (no guessing).
  Dot-source after seekdb-windows-dll-resolve.ps1 (uses Get-SeekDbWindowsBuildDirNameFromEnv).

  Usage:
    . ./unittest/include/seekdb-windows-dll-resolve.ps1
    . ./unittest/include/debug-libseekdb-windows.ps1
    Write-LibseekdbWindowsBuildFacts -RepoRoot "$PWD"
#>

function Write-LibseekdbWindowsBuildFacts {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
  )
  $bdn = Get-SeekDbWindowsBuildDirNameFromEnv
  $buildRoot = Join-Path $RepoRoot "build_$bdn"
  $bar = "============================================================"

  Write-Host $bar
  Write-Host "[fact] RepoRoot: $(Resolve-Path $RepoRoot)"
  Write-Host "[fact] BUILD_TYPE env: $($env:BUILD_TYPE)"
  Write-Host "[fact] Resolved build dir name: $bdn"
  Write-Host "[fact] build tree path: $buildRoot"
  Write-Host "[fact] build tree exists: $(Test-Path -LiteralPath $buildRoot)"
  if (-not (Test-Path -LiteralPath $buildRoot)) {
    Write-Host $bar
    return
  }

  $bn = Join-Path $buildRoot "build.ninja"
  Write-Host "[fact] Test-Path build.ninja: $(Test-Path -LiteralPath $bn)"
  if (Test-Path -LiteralPath $bn) { Write-Host "[fact] build.ninja full path: $(Resolve-Path $bn)" }

  $ninjaAtRoot = @()
  Write-Host "[fact] Top-level *.ninja files (non-recursive):"
  try {
    $ninjaAtRoot = @([System.IO.Directory]::GetFiles($buildRoot, "*.ninja", [System.IO.SearchOption]::TopDirectoryOnly))
    if ($ninjaAtRoot.Length -eq 0) { Write-Host "  (none)" }
    else { foreach ($f in $ninjaAtRoot) { Write-Host "  $f" } }
  } catch {
    Write-Host "  (error: $($_.Exception.Message))"
  }

  $cache = Join-Path $buildRoot "CMakeCache.txt"
  $generatorLine = $null
  if (Test-Path -LiteralPath $cache) {
    Write-Host "[fact] CMakeCache excerpts (generator / make program):"
    $generatorLine = Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($generatorLine) { Write-Host "  $($generatorLine.Line.Trim())" }
    Select-String -Path $cache -Pattern '^CMAKE_MAKE_PROGRAM:' -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
    Select-String -Path $cache -Pattern '^CMAKE_COMMAND:' -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
  } else {
    Write-Host "[fact] CMakeCache.txt: missing"
  }

  if ($generatorLine -and $generatorLine.Line -match 'Ninja' -and -not (Test-Path -LiteralPath $bn) -and $ninjaAtRoot.Length -eq 0) {
    Write-Host "[fact] STATE: CMAKE_GENERATOR is Ninja but build root has no build.ninja and no *.ninja — CMake did not finish generating the Ninja backend (or files were deleted after configure)."
  }

  foreach ($pat in @("seekdb.dll", "libseekdb.dll", "seekdb.lib", "libseekdb.lib")) {
    try {
      $hits = [System.IO.Directory]::GetFiles($buildRoot, $pat, [System.IO.SearchOption]::AllDirectories)
      Write-Host "[fact] Count $pat : $($hits.Length)"
      $max = [Math]::Min(8, $hits.Length)
      for ($i = 0; $i -lt $max; $i++) { Write-Host "    $($hits[$i])" }
    } catch {
      Write-Host "[fact] Count $pat : enumeration error — $($_.Exception.Message)"
    }
  }

  try {
    $allDll = [System.IO.Directory]::GetFiles($buildRoot, "*.dll", [System.IO.SearchOption]::AllDirectories)
    Write-Host "[fact] Total *.dll under build tree: $($allDll.Length)"
  } catch {
    Write-Host "[fact] Total *.dll : $($_.Exception.Message)"
  }

  $ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
  if ($ninjaCmd) {
    Write-Host "[fact] ninja on PATH: $($ninjaCmd.Source)"
    try {
      $nv = & ninja --version 2>&1
      Write-Host "[fact] ninja --version: $nv"
    } catch { Write-Host "[fact] ninja --version failed: $($_.Exception.Message)" }
  } else {
    Write-Host "[fact] ninja: not on PATH"
  }

  if (-not (Test-Path -LiteralPath $bn)) {
    Write-Host "[fact] ninja invoked from build root without build.ninja (capture stderr as fact):"
    Push-Location $buildRoot
    try {
      $failOut = & ninja -t targets 2>&1
      Write-Host "  exit: $LASTEXITCODE"
      $failOut | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
    } finally {
      Pop-Location
    }
    Write-Host "[fact] skip ninja -t query / ninja -n dry-run: no build.ninja"
    Write-Host $bar
    return
  }

  Write-Host "[fact] ninja -t targets (lines matching seekdb / libseekdb):"
  Push-Location $buildRoot
  try {
    $allTargets = & ninja -t targets 2>&1
    $tc = $LASTEXITCODE
    Write-Host "  ninja -t targets exit code: $tc"
    if ($tc -ne 0) {
      $allTargets | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
    } else {
      $m = @($allTargets | Where-Object { $_ -match 'seekdb' })
      if ($m.Count -eq 0) {
        Write-Host "  (no target line contains seekdb)"
        Write-Host "  (first 50 target lines for sanity):"
        $allTargets | Select-Object -First 50 | ForEach-Object { Write-Host "  $_" }
      } else {
        $m | Select-Object -First 100 | ForEach-Object { Write-Host "  $_" }
      }
    }
  } catch {
    Write-Host "  error: $($_.Exception.Message)"
  }

  Write-Host "[fact] ninja -t query libseekdb (if supported by ninja build):"
  try {
    $q = & ninja -t query libseekdb 2>&1
    Write-Host "  exit: $LASTEXITCODE"
    $q | Select-Object -First 60 | ForEach-Object { Write-Host "  $_" }
  } catch {
    Write-Host "  (ninja -t query not run or failed: $($_.Exception.Message))"
  } finally {
    Pop-Location
  }

  Write-Host "[fact] ninja -n libseekdb (dry run, first 80 lines + stderr):"
  Push-Location $buildRoot
  try {
    $dry = & ninja -n libseekdb 2>&1
    $dry | Select-Object -First 80 | ForEach-Object { Write-Host "  $_" }
    Write-Host "[fact] ninja -n libseekdb exit code: $LASTEXITCODE"
  } catch {
    Write-Host "  error: $($_.Exception.Message)"
  } finally {
    Pop-Location
  }

  Write-Host $bar
}
