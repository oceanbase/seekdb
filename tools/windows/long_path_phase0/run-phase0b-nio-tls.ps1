# Execute serially with all other LongPathsEnabled matrices.
param([int]$Jobs=4)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Set-Location $repo
if (Get-Process path_context_probe,nio_path_probe,nio_tls_probe,grpc_path_probe,grpc_lifecycle_probe,grpc_file_boundary_probe -ErrorAction SilentlyContinue) { throw 'Another policy probe is active' }
$runId=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$evidence="$repo\build_phase0\nio-tls-$runId"
$testRoot="C:\s\seek533-nio-tls-$runId"
New-Item -ItemType Directory $evidence | Out-Null
Write-Output "NIO_TLS_EVIDENCE=$evidence"
$lockBefore=(Get-FileHash rust/Cargo.lock -Algorithm SHA256).Hash
Copy-Item rust/Cargo.lock "$evidence\Cargo.lock"
$ErrorActionPreference='Continue'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 phase0-nio -j $Jobs 2>&1 | Out-File "$evidence\build.log" -Encoding utf8
$buildExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
[IO.File]::WriteAllText("$evidence\build.exit",[string]$buildExit)
if ($buildExit -ne 0) { Get-Content "$evidence\build.log" -Tail 90; exit $buildExit }
if ((Get-FileHash rust/Cargo.lock -Algorithm SHA256).Hash -ne $lockBefore) { throw 'Cargo.lock changed during component build' }
$build="$repo\build_phase0_nio"
Get-FileHash "$build\nio_tls_probe.exe", "$build\rust-target\release\sql_nio.lib", 'rust/Cargo.lock', 'rust/sql-nio/src/reactor.rs', 'rust/sql-nio/src/tls.rs', 'rust/sql-nio/include/nio.h', 'tools/windows/long_path_phase0/nio_tls_probe.cpp', 'tools/windows/long_path_phase0/CMakeLists.txt', 'build.ps1' -Algorithm SHA256 | Format-List | Out-File "$evidence\identity.log" -Encoding utf8
Copy-Item "$build\CMakeCache.txt" "$evidence\CMakeCache.txt"
Copy-Item "$build\compile_commands.json" "$evidence\compile_commands.json"
$cacheLines=@(Get-Content "$build\CMakeCache.txt" | Where-Object { $_ -match '^OB_OPENSSL_DIR:[^=]+=' })
if ($cacheLines.Count -ne 1) { throw 'Missing unique product OpenSSL directory' }
$opensslRoot=(Resolve-Path ($cacheLines[0] -replace '^[^=]+=','')).Path
Get-FileHash "$opensslRoot\lib\VC\x64\MD\libssl.lib", "$opensslRoot\lib\VC\x64\MD\libcrypto.lib" -Algorithm SHA256 | Format-List | Out-File "$evidence\openssl-link-inputs.log" -Encoding utf8
$originalPath=$env:PATH
# Product oblib links these OpenSSL import libraries; load the matching DLLs.
$env:PATH="$opensslRoot\bin;$repo\deps\3rd\vcpkg\x64-windows\bin;$originalPath"
Get-ChildItem "$opensslRoot\bin" -Filter '*.dll' | Get-FileHash -Algorithm SHA256 | Format-List | Out-File "$evidence\openssl-identity.log" -Encoding utf8
New-Item -ItemType Directory $testRoot | Out-Null
$policyKey='HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
$originalPolicy=Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
$failed=$false
try {
  foreach ($policy in @(0,1)) {
    Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $policy
    $prefix="$evidence\policy-$policy"
    $probe=Start-Process "$build\nio_tls_probe.exe" -NoNewWindow -PassThru -ArgumentList @($testRoot,[string]$policy) -RedirectStandardOutput "$prefix.log" -RedirectStandardError "$prefix.stderr.log"
    try {
      $handle=$probe.Handle
      $timeout=-not $probe.WaitForExit(180000)
      if ($timeout) { $probe.Kill() }
      $probe.WaitForExit(); $code=$probe.ExitCode
      if ($null -eq $code) { throw 'Missing native exit' }
      [IO.File]::WriteAllText("$prefix-process.json",([ordered]@{pid=$probe.Id;timed_out=$timeout;native_exit=$code} | ConvertTo-Json))
    } finally {
      if (-not $probe.HasExited) { $probe.Kill(); $probe.WaitForExit() }; $probe.Dispose()
    }
    $lines=@(Get-Content "$prefix.log" -Encoding UTF8)
    $modules=@($lines | Where-Object { $_.StartsWith('NIO_TLS_MODULE=') } | ForEach-Object { $_.Substring(15) })
    if ($modules.Count -ne 2) { throw 'Missing loaded TLS module identity' }
    foreach ($module in $modules) {
      if (-not $module.StartsWith("$opensslRoot\bin\", [StringComparison]::OrdinalIgnoreCase)) { throw "TLS module is outside product OpenSSL: $module" }
    }
    Get-FileHash $modules -Algorithm SHA256 | Format-List | Out-File "$prefix-modules.log" -Encoding utf8
    $cases=@($lines | Where-Object { $_.StartsWith('NIO_TLS_PATH_PASS ') }).Count
    $rejects=@($lines | Where-Object { $_.StartsWith('NIO_TLS_REJECT_PASS ') }).Count
    $combined=@($lines | Where-Object { $_.StartsWith('NIO_TLS_COMBINED_PASS ') }).Count
    $authReject=@($lines | Where-Object { $_.StartsWith('NIO_TLS_HANDSHAKE_REJECT_PASS kind=1 ') }).Count
    $versionReject=@($lines | Where-Object { $_.StartsWith('NIO_TLS_HANDSHAKE_REJECT_PASS kind=2 ') }).Count
    $minimum=@($lines | Where-Object { $_ -eq 'NIO_TLS_MIN_VERSION_PASS' }).Count
    $pass=@($lines | Where-Object { $_ -eq "NIO_TLS_COMPONENT_PASS policy=$policy" }).Count
    Write-Output "NIO_TLS_EXIT policy=$policy native_exit=$code timeout=$timeout cases=$cases rejects=$rejects combined=$combined auth_reject=$authReject version_reject=$versionReject minimum=$minimum pass=$pass"
    if ($code -ne 0 -or $timeout -or $cases -ne 18 -or $rejects -ne 8 -or $pass -ne 1 -or $combined -ne 2 -or $authReject -ne 2 -or $versionReject -ne 1 -or $minimum -ne 1) { $failed=$true; Get-Content "$prefix.stderr.log" -Tail 20; break }
  }
} finally {
  $env:PATH=$originalPath
  Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $originalPolicy
  $final=Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
  "POLICY_ORIGINAL=$originalPolicy POLICY_FINAL=$final" | Out-File "$evidence\policy-restored.log" -Encoding utf8
  if ($final -ne $originalPolicy) { throw 'Policy restoration failed' }
}
if ($failed) { exit 1 }
Remove-Item -LiteralPath $testRoot
Write-Output 'NIO_TLS_VALIDATION_PASS'
exit 0
