# Run only on the explicitly authorized Windows test VM. No dependency updates.
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$evidence = Join-Path $repo "build_phase0/phase0a-$runId"
New-Item -ItemType Directory -Path $evidence | Out-Null
Set-Location $repo
# Preserve the previous probe before the normal incremental product build.
if (Test-Path 'build_phase0/sqlite_path_probe.exe') {
    Copy-Item 'build_phase0/sqlite_path_probe.exe' "$evidence/previous-sqlite_path_probe.exe"
}
# Keep native warnings as evidence. Isolate build.ps1 in its own PowerShell so
# the tee/redirection cannot promote CMake stderr warnings into script failure.
$ErrorActionPreference = 'Continue'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 phase0 -j $Jobs 2>&1 |
    Tee-Object "$evidence/build.log"
$buildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Write-Output "PHASE0A_BUILD_EXIT=$buildExit"
if ($buildExit -ne 0) { exit $buildExit }
Get-FileHash 'build_phase0/sqlite_path_probe.exe','build_phase0/sqlite3.dll',
    'tools/windows/long_path_phase0/sqlite_path_probe.cpp',
    'tools/windows/long_path_phase0/run-phase0a.ps1' -Algorithm SHA256 |
    Format-List | Out-String | Tee-Object "$evidence/identity.log"
$policyKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
$originalPolicy = Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
$testRoot = "C:\s\seek533-phase0a-$runId"
New-Item -ItemType Directory -Path $testRoot | Out-Null
$failed = $false
try {
    foreach ($policy in @(0, 1)) {
        Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $policy
        Write-Output "PHASE0A_POLICY_BEGIN=$policy"
        # Native stderr is evidence, not a PowerShell terminating exception.
        $ErrorActionPreference = 'Continue'
        & ./build_phase0/sqlite_path_probe.exe $testRoot $policy 2>&1 |
            Tee-Object "$evidence/policy-$policy.log"
        $probeExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        Write-Output "PHASE0A_POLICY_EXIT=$policy,$probeExit"
        if ($probeExit -ne 0) { $failed = $true }
    }
} finally {
    $ErrorActionPreference = 'Stop'
    Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $originalPolicy
    $finalPolicy = Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
    if ($finalPolicy -ne $originalPolicy) { throw 'Policy restoration failed' }
    "POLICY_ORIGINAL=$originalPolicy POLICY_FINAL=$finalPolicy" |
        Tee-Object "$evidence/policy-restored.log"
    Write-Output "PHASE0A_EVIDENCE=$evidence"
}
Get-FileHash "$evidence/policy-0.log","$evidence/policy-1.log" -Algorithm SHA256 |
    Format-List | Out-String
if ($failed) { exit 1 }
Write-Output 'PHASE0A_BOTH_POLICIES_PASS (Phase 0B and product acceptance still required)'
exit 0
