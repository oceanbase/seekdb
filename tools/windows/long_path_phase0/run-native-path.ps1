# Compile the production directory caller and test the production path helper.
param([int]$Jobs=4, [string]$CleanupRoot="")
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$r=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Set-Location $r
if(Get-Process windows_file_path_test,sqlite_temp_probe,nio_path_probe,nio_tls_probe,grpc_path_probe,cargo,rustc -ErrorAction SilentlyContinue) {throw 'Concurrent task probe/build'}
if((Get-PSDrive C).Free -lt 2GB){throw 'Insufficient free space for native path build'}
$e=Join-Path $r ('build_phase0/native-path-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory $e | Out-Null
Write-Output "NATIVE_PATH_EVIDENCE=$e"
$ErrorActionPreference='Continue'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$r/build.ps1" native-path -j $Jobs 2>&1 | Out-File "$e/build.log" -Encoding UTF8
$code=$LASTEXITCODE
$ErrorActionPreference='Stop'
[IO.File]::WriteAllText("$e/build.exit",[string]$code)
Get-Content "$e/build.log" -Tail 20
if($code -ne 0){exit $code}
$b="$r/build_phase0_nio"
Get-FileHash "$b/windows_file_path_test.exe","$b/CMakeCache.txt",
 "$r/src/oblib/lib/file/windows_file_path.cpp","$r/src/oblib/lib/file/windows_file_path.h",
 "$r/src/oblib/lib/file/file_directory_utils.cpp",
 "$r/tools/windows/long_path_phase0/windows_file_path_test.cpp","$r/build.ps1",
 "$r/tools/windows/long_path_phase0/CMakeLists.txt" -Algorithm SHA256 |
 Format-List | Out-File "$e/identity.log" -Encoding UTF8
if($CleanupRoot) {
 $cleanup=Start-Process "$b/windows_file_path_test.exe" -ArgumentList @($CleanupRoot,'--cleanup') -WorkingDirectory $r -NoNewWindow -PassThru -RedirectStandardOutput "$e/cleanup.log" -RedirectStandardError "$e/cleanup.stderr.log"
 try {
  $handle=$cleanup.Handle
  $timeout=-not $cleanup.WaitForExit(900000)
  if($timeout){$cleanup.Kill()}
  $cleanup.WaitForExit();$native=$cleanup.ExitCode
  [IO.File]::WriteAllText("$e/cleanup-process.json",([ordered]@{pid=$cleanup.Id;native_exit=$native;timed_out=$timeout;root=$CleanupRoot}|ConvertTo-Json))
 } finally {
  if(-not $cleanup.HasExited){$cleanup.Kill();$cleanup.WaitForExit()}
  $cleanup.Dispose()
 }
 Get-Content "$e/cleanup.log"
 Get-Content "$e/cleanup.stderr.log"
 if($null -eq $native -or $native -ne 0 -or $timeout){throw 'Prior fixture cleanup failed'}
}
$policyKey='HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
$before=Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
try {
 foreach($policy in @(0,1)) {
  Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $policy
  $testRoot="C:\s\seek533-"+(Split-Path $e -Leaf)+"-$policy"
  New-Item -ItemType Directory $testRoot | Out-Null
  $p=Start-Process "$b/windows_file_path_test.exe" -ArgumentList @($testRoot) -WorkingDirectory $r -NoNewWindow -PassThru -RedirectStandardOutput "$e/policy-$policy.log" -RedirectStandardError "$e/policy-$policy.stderr.log"
  try {
   $handle=$p.Handle
   $timeout=-not $p.WaitForExit(900000)
   if($timeout){$p.Kill()}
   $p.WaitForExit();$native=$p.ExitCode
   [IO.File]::WriteAllText("$e/policy-$policy-process.json",([ordered]@{pid=$p.Id;native_exit=$native;timed_out=$timeout;root=$testRoot}|ConvertTo-Json))
  } finally {
   if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}
   $p.Dispose()
  }
  Get-Content "$e/policy-$policy.log"
  Get-Content "$e/policy-$policy.stderr.log"
  if($null -eq $native -or $native -ne 0 -or $timeout){throw "Native path policy $policy failed"}
  $lines=@(Get-Content "$e/policy-$policy.log")
  if(@($lines|Where-Object {$_ -eq 'NATIVE_PATH_COMPONENT_PASS'}).Count -ne 1 -or
     @($lines|Where-Object {$_.StartsWith('NATIVE_DIRECTORY_PASS')}).Count -ne 18 -or
     @($lines|Where-Object {$_.StartsWith('NATIVE_REJECT_PASS')}).Count -ne 19){throw 'Missing native path evidence'}
  if(@($lines|Where-Object {$_.StartsWith('NATIVE_FILE_PASS')}).Count -ne 18 -or
     @($lines|Where-Object {$_ -eq 'NATIVE_TREE_PASS allocation_failures=15'}).Count -ne 1 -or
     @($lines|Where-Object {$_ -eq 'NATIVE_SPARSE_PASS bytes=4294967313'}).Count -ne 1 -or
     @($lines|Where-Object {$_ -eq 'NATIVE_DEEP_PASS units=4096'}).Count -ne 1 -or
     @($lines|Where-Object {$_ -eq 'NATIVE_REPARSE_PASS'}).Count -ne 1 -or
     @($lines|Where-Object {$_.StartsWith('NATIVE_RESOURCE_PASS')}).Count -ne 1){throw 'Missing file lifecycle evidence'}
  if(Test-Path $testRoot){throw 'Test root remains after successful probe'}
 }
} finally {
 Set-ItemProperty $policyKey -Name LongPathsEnabled -Value $before
 $after=Get-ItemPropertyValue $policyKey -Name LongPathsEnabled
 "ORIGINAL=$before RESTORED=$after" | Out-File "$e/policy-restored.log" -Encoding UTF8
 if($after -ne $before){throw 'Policy restoration failed'}
}
Write-Output 'NATIVE_PATH_BOTH_POLICIES_PASS'
exit 0
