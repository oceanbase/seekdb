# SQLite component observation only; does not change TEMP or temp_store.
param([int]$Jobs=4)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$r=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Set-Location $r
if(Get-Process sqlite_temp_probe,nio_path_probe,nio_tls_probe,grpc_path_probe,cargo,rustc -ErrorAction SilentlyContinue) {throw 'Concurrent task probe/build'}
if((Get-PSDrive C).Free -lt 2GB){throw 'Insufficient free space for bounded sort fixture'}
$e=Join-Path $r ('build_phase0/sqlite-temp-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory $e | Out-Null
Write-Output "SQLITE_TEMP_EVIDENCE=$e"
$ErrorActionPreference='Continue'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$r/build.ps1" phase0-sqlite-temp -j $Jobs 2>&1 | Out-File "$e/build.log" -Encoding UTF8
$code=$LASTEXITCODE
$ErrorActionPreference='Stop'
[IO.File]::WriteAllText("$e/build.exit",[string]$code)
Get-Content "$e/build.log" -Tail 20
if($code -ne 0){exit $code}
$b="$r/build_phase0_nio"
Get-FileHash "$b/sqlite_temp_probe.exe","$b/sqlite3.dll","$b/CMakeCache.txt",
 "$r/tools/windows/long_path_phase0/sqlite_temp_probe.cpp","$r/build.ps1",
 "$r/tools/windows/long_path_phase0/CMakeLists.txt" -Algorithm SHA256 |
 Format-List | Out-File "$e/identity.log" -Encoding UTF8
$policyKey='HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
$before=Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
try {
 foreach($policy in @(0,1)) {
  Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $policy
  $testRoot="C:\s\seek533-"+(Split-Path $e -Leaf)+"-$policy"
  New-Item -ItemType Directory $testRoot | Out-Null
  $p=Start-Process "$b/sqlite_temp_probe.exe" -ArgumentList @($testRoot,[string]$policy) -WorkingDirectory $r -NoNewWindow -PassThru -RedirectStandardOutput "$e/policy-$policy.log" -RedirectStandardError "$e/policy-$policy.stderr.log"
  try {
   $handle=$p.Handle
   $timeout=-not $p.WaitForExit(180000)
   if($timeout){$p.Kill()}
   $p.WaitForExit();$native=$p.ExitCode
   [IO.File]::WriteAllText("$e/policy-$policy-process.json",([ordered]@{pid=$p.Id;native_exit=$native;timed_out=$timeout;root=$testRoot}|ConvertTo-Json))
  } finally {
   if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}
   $p.Dispose()
  }
  Get-Content "$e/policy-$policy.log"
  Get-Content "$e/policy-$policy.stderr.log"
  if($null -eq $native -or $native -ne 0 -or $timeout){throw "SQLite temp policy $policy failed"}
  $lines=@(Get-Content "$e/policy-$policy.log")
  if(@($lines|Where-Object {$_ -eq 'SQLITE_TEMP_COMPONENT_PASS'}).Count -ne 1 -or
     @($lines|Where-Object {$_.StartsWith('SQLITE_TEMP_CASE_PASS')}).Count -ne 2 -or
     @($lines|Where-Object {$_.StartsWith('SQLITE_SORT_PASS')}).Count -ne 4){throw 'Missing SQLite temporary file evidence'}
  if(Test-Path $testRoot){throw 'Test root remains after successful probe'}
 }
} finally {
 Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $before
 $after=Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
 "ORIGINAL=$before RESTORED=$after" | Out-File "$e/policy-restored.log" -Encoding UTF8
 if($after -ne $before){throw 'Policy restoration failed'}
}
Write-Output 'SQLITE_TEMP_BOTH_POLICIES_PASS'
exit 0
