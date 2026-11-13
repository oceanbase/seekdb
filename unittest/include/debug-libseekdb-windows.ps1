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
  Write-Host "[fact] Bazel output base: $buildRoot"
  Write-Host "[fact] output base exists: $(Test-Path -LiteralPath $buildRoot)"
  if (-not (Test-Path -LiteralPath $buildRoot)) {
    Write-Host "[fact] run: python ./build.py build -o build_$bdn //src/observer:libseekdb"
    Write-Host $bar
    return
  }

  # build.py sets --symlink_prefix=<output_base>/, so the convenience links are
  # build_<type>\bin and build_<type>\bazel-bin; both point at the same tree.
  foreach ($rel in @("bin", "bazel-bin")) {
    $p = Join-Path $buildRoot $rel
    Write-Host "[fact] Test-Path ${rel}: $(Test-Path -LiteralPath $p)"
    if (Test-Path -LiteralPath $p) {
      try { Write-Host "    target: $((Get-Item -LiteralPath $p).Target)" } catch { }
    }
  }

  $libDir = Join-Path $buildRoot "bin\src\observer"
  Write-Host "[fact] package output dir: $libDir"
  Write-Host "[fact] package output exists: $(Test-Path -LiteralPath $libDir)"
  if (Test-Path -LiteralPath $libDir) {
    Get-ChildItem -LiteralPath $libDir -File -ErrorAction SilentlyContinue |
      Sort-Object Name |
      Select-Object -First 40 Name, Length |
      Format-Table -AutoSize | Out-String | Write-Host
    $dlls = @(Get-ChildItem -LiteralPath $libDir -Filter "*.dll" -File -ErrorAction SilentlyContinue)
    $runtime = @($dlls | Where-Object { $_.Name -ine "seekdb.dll" -and $_.Name -ine "libseekdb.dll" })
    Write-Host "[fact] runtime DLLs beside seekdb.dll: $($runtime.Count)"
    Write-Host "[fact] import library present: $(Test-Path -LiteralPath (Join-Path $libDir 'seekdb.lib'))"
  }

  Write-Host "[fact] RepoRoot seekdb.h: $(Test-Path -LiteralPath (Join-Path $RepoRoot 'src\include\seekdb.h'))"

  $resolved = Find-SeekDbWindowsDll -RepoRoot $RepoRoot -BuildDirName $bdn
  if ($resolved) {
    Write-Host "[fact] resolved DLL: $($resolved.DllPath)"
    Write-Host "[fact] resolved lib dir: $($resolved.LibDir)"
  } else {
    Write-Host "[fact] resolved DLL: none"
  }

  Write-Host $bar
}
