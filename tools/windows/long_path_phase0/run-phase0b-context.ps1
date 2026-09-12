# Phase 0 component only. Run serially with other policy-changing matrices.
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Set-Location $repo
if (Get-Process path_context_probe,grpc_path_probe,grpc_lifecycle_probe,grpc_file_boundary_probe -ErrorAction SilentlyContinue) {
    throw 'Another policy probe is active'
}
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$evidence = "$repo\build_phase0\context-$runId"
$testRoot = "C:\s\seek533-context-$runId"
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Output "CONTEXT_EVIDENCE=$evidence"
$ErrorActionPreference = 'Continue'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 phase0-context -j $Jobs 2>&1 |
    Out-File "$evidence\build.log" -Encoding utf8
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
[IO.File]::WriteAllText("$evidence\build.exit", [string]$code)
if ($code -ne 0) { exit $code }
$build = "$repo\build_phase0_context"
Copy-Item "$build\path_context_probe.exe" "$evidence\probe.exe"
Get-FileHash "$evidence\probe.exe", 'build.ps1',
    'tools/windows/long_path_phase0/path_context.h',
    'tools/windows/long_path_phase0/path_context_probe.cpp',
    'tools/windows/long_path_phase0/CMakeLists.txt',
    'tools/windows/long_path_phase0/run-phase0b-context.ps1' -Algorithm SHA256 |
    Format-List | Out-File "$evidence\identity.log" -Encoding utf8
Copy-Item "$build\CMakeCache.txt" "$evidence\CMakeCache.txt"
$commands = Get-Content "$build\compile_commands.json" -Raw | ConvertFrom-Json
$command = @($commands | Where-Object { $_.file -like '*/path_context_probe.cpp' })
if ($command.Count -ne 1) { throw 'Missing unique component compile command' }
[IO.File]::WriteAllText("$evidence\compile-command.json",
    (ConvertTo-Json -InputObject $command[0] -Depth 4), [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$policyKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
$originalPolicy = Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
$failed = $false
try {
    foreach ($policy in @(0,1)) {
        Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $policy
        $prefix = "$evidence\policy-$policy"
        $probe = Start-Process -FilePath "$build\path_context_probe.exe" -NoNewWindow -PassThru -ArgumentList @($testRoot,[string]$policy) -RedirectStandardOutput "$prefix.log" -RedirectStandardError "$prefix.stderr.log"
        try {
            $handle = $probe.Handle
            $timeout = -not $probe.WaitForExit(180000)
            if ($timeout) { $probe.Kill() }
            $probe.WaitForExit()
            $nativeExit = $probe.ExitCode
            if ($null -eq $nativeExit) { throw 'Missing native component exit' }
            [IO.File]::WriteAllText("$prefix-process.json",
                ([ordered]@{pid=$probe.Id;timed_out=$timeout;native_exit=$nativeExit} | ConvertTo-Json),
                [Text.UTF8Encoding]::new($false))
        } finally {
            if (-not $probe.HasExited) { $probe.Kill(); $probe.WaitForExit() }
            $probe.Dispose()
        }
        [IO.File]::WriteAllText("$prefix.exit", [string]$nativeExit)
        $lines = @(Get-Content "$prefix.log" -Encoding UTF8)
        $rejects = @($lines | Where-Object { $_.StartsWith('CONTEXT_REJECT_PASS ') }).Count
        $bases = @($lines | Where-Object { $_.StartsWith('CONTEXT_BASE_PASS ') }).Count
        $files = @($lines | Where-Object { $_.StartsWith('CONTEXT_FILE_PASS ') }).Count
        $pass = @($lines | Where-Object { $_ -eq "CONTEXT_MATRIX_PASS policy=$policy" }).Count
        $cli = @($lines | Where-Object { $_.StartsWith('CONTEXT_CLI_PASS ') }).Count
        Write-Output "CONTEXT_EXIT policy=$policy exit=$nativeExit timeout=$timeout rejects=$rejects bases=$bases files=$files cli=$cli"
        if ($nativeExit -ne 0 -or $timeout -or $rejects -ne 18 -or $bases -ne 10 -or $files -ne 10 -or $cli -ne 5 -or $pass -ne 1) {
            $failed = $true
            break # Keep failed evidence and owned tree; do not reuse a dirty fixture.
        }
    }
} finally {
    Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $originalPolicy
    $finalPolicy = Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
    "POLICY_ORIGINAL=$originalPolicy POLICY_FINAL=$finalPolicy" | Out-File "$evidence\policy-restored.log" -Encoding utf8
    if ($finalPolicy -ne $originalPolicy) { throw 'Policy restoration failed' }
}
if ($failed) { exit 1 }
Remove-Item -LiteralPath $testRoot # Empty-only; never recurse into an unknown tree.
Write-Output 'CONTEXT_COMPONENT_PASS (CLI integration, ABI and discovery remain separate gates)'
exit 0
