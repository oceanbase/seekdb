#Requires -Version 5.1
<#
  Shared Windows process utilities for libseekdb CI scripts (pure functions, no logic changes):
    - Write-BindLog
    - Wait-ProcessWithDeadline
    - Start-NpmProcess
    - Invoke-ExternalTestWithBindingExitProbe

  Dot-sourced by unittest/include/run-libseekdb-binding-tests.ps1 and
  package/libseekdb/test-packed-artifact-smoke.ps1 (see package/libseekdb/README.md).

  All functions are self-contained except Start-NpmProcess / Invoke-ExternalTestWithBindingExitProbe,
  which call Write-BindLog and Wait-ProcessWithDeadline defined here.
#>

Set-StrictMode -Version Latest

function Write-BindLog {
  param([string]$Message)
  $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
  Write-Host "[$ts] [seekdb-bind] $Message"
  try { [Console]::Out.Flush() } catch {}
}

# Wait for a child process with Refresh/WaitForExit chunks + wall-clock deadline + taskkill /T on expiry.
function Wait-ProcessWithDeadline {
  param(
    [Parameter(Mandatory = $true)]
    [System.Diagnostics.Process]$Process,
    [Parameter(Mandatory = $true)]
    [int]$TimeoutMs,
    [Parameter(Mandatory = $true)]
    [string]$Label,
    [Parameter(Mandatory = $false)]
    [int]$HeartbeatSec = 60,
    # Child writes before_process_exit to %TEMP%\seekdb_binding_exit_probe_<pid>.log; native teardown may still hang.
    [switch]$UseBindingExitProbe,
    [Parameter(Mandatory = $false)]
    [int]$BindingExitProbeGraceMs = 15000
  )
  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  $timedOut = $false
  $heartbeatUtc = [DateTime]::UtcNow
  $probeFirstUtc = $null
  $forcedFromProbe = $false
  $forcedExitCode = 0
  while ($true) {
    $Process.Refresh()
    if ($Process.HasExited) { break }
    if ([DateTime]::UtcNow -ge $deadline) {
      $timedOut = $true
      Write-Host "::error::${Label}: exceeded ${TimeoutMs} ms; taskkill /F /T pid=$($Process.Id)"
      # taskkill stdout must not reach the function output stream or callers get an array ($wr['TimedOut'] then fails).
      $null = & taskkill.exe /F /T /PID $Process.Id 2>$null
      $Process.Refresh()
      if (-not $Process.HasExited) {
        $null = $Process.WaitForExit(45000)
      }
      break
    }
    if ($UseBindingExitProbe) {
      $probePath = Join-Path $env:TEMP "seekdb_binding_exit_probe_$($Process.Id).log"
      if (Test-Path -LiteralPath $probePath) {
        $pr = Get-Content -LiteralPath $probePath -Raw -ErrorAction SilentlyContinue
        if ($pr -match 'before_process_exit code=(-?\d+)') {
          $probeCode = [int]($Matches[1])
          if ($null -eq $probeFirstUtc) {
            $probeFirstUtc = [DateTime]::UtcNow
            Write-Host "::notice::[seekdb-bind] binding exit probe seen pid=$($Process.Id) code=$probeCode (${BindingExitProbeGraceMs}ms grace, then Stop-Process -Force if still stuck in DLL unload)"
            [Console]::Out.Flush()
          }
          elseif (([DateTime]::UtcNow - $probeFirstUtc).TotalMilliseconds -ge $BindingExitProbeGraceMs) {
            Write-Host "::notice::[seekdb-bind] process still alive after probe+grace — forcing Stop-Process pid=$($Process.Id) (Windows native teardown hang workaround)"
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.Refresh()
            Start-Sleep -Milliseconds 200
            if (-not $Process.HasExited) {
              $null = & taskkill.exe /F /T /PID $Process.Id 2>$null
              $Process.Refresh()
              if (-not $Process.HasExited) {
                $null = $Process.WaitForExit(8000)
              }
            }
            $forcedFromProbe = $true
            $forcedExitCode = $probeCode
            break
          }
        }
      }
    }
    $remainingMs = [Math]::Max(1, [int](($deadline - [DateTime]::UtcNow).TotalMilliseconds))
    $chunkMs = [Math]::Min(500, $remainingMs)
    try {
      $null = $Process.WaitForExit($chunkMs)
    }
    catch {
      $Process.Refresh()
    }
    $Process.Refresh()
    if ($Process.HasExited) { break }
    $nowUtc = [DateTime]::UtcNow
    if (($nowUtc - $heartbeatUtc).TotalSeconds -ge $HeartbeatSec) {
      $heartbeatUtc = $nowUtc
      Write-Host "::notice::[seekdb-bind] still waiting for ${Label} pid=$($Process.Id) (${TimeoutMs}ms max)"
      [Console]::Out.Flush()
    }
  }
  $exitCode = if ($forcedFromProbe) {
    $forcedExitCode
  }
  elseif ($timedOut) {
    -1
  }
  else {
    $Process.ExitCode
  }
  # Set-StrictMode: never use dot notation on hashtables ($r.TimedOut fails). Call sites must use $r['TimedOut'].
  return @{
    TimedOut         = $timedOut
    ExitCode         = $exitCode
    ForcedAfterProbe = $forcedFromProbe
  }
}

# Start-Process requires a Win32 .exe/.cmd — not npm.ps1 or extensionless shims (GHA Node 22 → "%1 is not a valid Win32 application").
function Start-NpmProcess {
  param(
    [Parameter(Mandatory = $true)]
    [string[]]$NpmArguments,
    [Parameter(Mandatory = $true)]
    [string]$WorkingDirectory
  )
  $npmPath = $null
  foreach ($name in @('npm.cmd', 'npm.exe')) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path -LiteralPath $cmd.Source)) {
      $npmPath = $cmd.Source
      break
    }
  }
  if (-not $npmPath) {
    $nodeDir = Split-Path -Parent (Get-Command node -ErrorAction Stop).Source
    $candidate = Join-Path $nodeDir 'npm.cmd'
    if (Test-Path -LiteralPath $candidate) {
      $npmPath = $candidate
    }
  }
  if ($npmPath) {
    Write-BindLog "npm: Start-Process $npmPath"
    return Start-Process -FilePath $npmPath -ArgumentList $NpmArguments -WorkingDirectory $WorkingDirectory -PassThru -NoNewWindow
  }
  $comspec = if ($env:ComSpec -and (Test-Path -LiteralPath $env:ComSpec)) { $env:ComSpec } else { 'cmd.exe' }
  $wrapped = @('/c', 'npm') + $NpmArguments
  Write-BindLog "npm: Start-Process $comspec /c npm (fallback for npm.ps1 or non-Win32 shim)"
  return Start-Process -FilePath $comspec -ArgumentList $wrapped -WorkingDirectory $WorkingDirectory -PassThru -NoNewWindow
}

# Start-Process + wall-clock + optional seekdb_binding_exit_probe_* (all native bind tests: node, rust, go, java).
function Invoke-ExternalTestWithBindingExitProbe {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [Parameter(Mandatory = $true)]
    [string[]]$ArgumentList,
    [Parameter(Mandatory = $true)]
    [string]$Description
  )
  $testMs = 900000
  if ($env:SEEKDB_BINDING_TEST_TIMEOUT_MS -match '^\d+$') {
    $testMs = [int]$env:SEEKDB_BINDING_TEST_TIMEOUT_MS
  }
  elseif ($env:SEEKDB_NODE_TEST_TIMEOUT_MS -match '^\d+$') {
    $testMs = [int]$env:SEEKDB_NODE_TEST_TIMEOUT_MS
  }
  $graceMs = 15000
  if ($env:SEEKDB_NODE_POST_SUCCESS_FORCE_KILL_MS -match '^\d+$') {
    $graceMs = [int]$env:SEEKDB_NODE_POST_SUCCESS_FORCE_KILL_MS
  }
  Write-BindLog "$Description (wall-clock ${testMs}ms; exit-probe grace ${graceMs}ms)"
  $prevProbe = $env:SEEKDB_BINDING_EXIT_PROBE
  $env:SEEKDB_BINDING_EXIT_PROBE = '1'
  $nr = $null
  try {
    $p = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -WorkingDirectory (Get-Location) -PassThru -NoNewWindow
    if ($null -eq $p) {
      throw "Start-Process returned null ($Description)"
    }
    $nr = Wait-ProcessWithDeadline -Process $p -TimeoutMs $testMs -Label $Description -HeartbeatSec 60 -UseBindingExitProbe -BindingExitProbeGraceMs $graceMs
  }
  finally {
    if ($null -eq $prevProbe) {
      Remove-Item Env:\SEEKDB_BINDING_EXIT_PROBE -ErrorAction SilentlyContinue
    }
    else {
      $env:SEEKDB_BINDING_EXIT_PROBE = $prevProbe
    }
  }
  if ($null -eq $nr) {
    throw "${Description}: internal error — no wait result (Start-Process or Wait-ProcessWithDeadline failed before return)"
  }
  if ($nr['ForcedAfterProbe']) {
    Write-Host "::notice::[seekdb-bind] $Description — exit code from probe $($nr['ExitCode']) (process did not terminate; likely DLL unload hang)"
  }
  if ($nr['TimedOut']) {
    throw "$Description timed out after ${testMs} ms"
  }
  if ($nr['ExitCode'] -ne 0) {
    throw "$Description failed (exit $($nr['ExitCode']))"
  }
}