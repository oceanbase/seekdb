# Called by build.ps1 after building real old/new Rust and positive controls.
param([Parameter(Mandatory=$true)][string]$BuildDirectory,
      [Parameter(Mandatory=$true)][string]$Ninja, [int]$Jobs=4)
$ErrorActionPreference='Stop'
$evidence=Join-Path $BuildDirectory ('nio-abi-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory $evidence | Out-Null
Write-Output "NIO_ABI_EVIDENCE=$evidence"
foreach($case in @(@('nio_abi_old_control','1','NIO_ABI26_CALLER_PASS error=1'),
                  @('nio_abi_old_new','2','NIO_ABI26_CALLER_PASS error=2'),
                  @('nio_abi_new_control','0','NIO_ABI27_CALLER_PASS'))) {
    $name=$case[0]; $cwd=Join-Path $evidence "$name-cwd"
    New-Item -ItemType Directory $cwd | Out-Null
    $process=Start-Process "$BuildDirectory\$name.exe" -ArgumentList $case[1] -WorkingDirectory $cwd -PassThru -NoNewWindow -RedirectStandardOutput "$evidence\$name.log" -RedirectStandardError "$evidence\$name.stderr.log"
    try {
        $handle=$process.Handle
        $timeout=-not $process.WaitForExit(30000)
        if ($timeout) { $process.Kill() }
        $process.WaitForExit(); $code=$process.ExitCode
        [IO.File]::WriteAllText("$evidence\$name-process.json",([ordered]@{pid=$process.Id;native_exit=$code;timed_out=$timeout} | ConvertTo-Json))
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
    if ($null -eq $code -or $code -ne 0 -or $timeout) { throw "NIO ABI control failed: $name" }
    if (@(Get-ChildItem -Force $cwd).Count -ne 0) { throw "NIO ABI control created unexpected resources: $name" }
    $lines=@(Get-Content "$evidence\$name.log")
    if (@($lines | Where-Object { $_ -eq $case[2] }).Count -ne 1) { throw "NIO ABI control evidence missing: $name" }
    Remove-Item $cwd
    Write-Output "NIO_ABI_CONTROL_PASS name=$name native_exit=$code"
}
# Compile already succeeded for the shared caller object and both Rust libs.
# Only the new symbol's absence is an acceptable negative result.
$ErrorActionPreference='Continue'
& $Ninja -C $BuildDirectory -j $Jobs nio_abi_new_old 2>&1 | Out-File "$evidence\new-old-link.log" -Encoding utf8
$negativeExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
[IO.File]::WriteAllText("$evidence\new-old-link.exit",[string]$negativeExit)
$errors=@(Get-Content "$evidence\new-old-link.log" | Where-Object { $_ -match 'undefined symbol:' })
if ($negativeExit -eq 0 -or $errors.Count -ne 1 -or $errors[0] -notmatch 'undefined symbol: nio_start_v27\s*$') {
    Get-Content "$evidence\new-old-link.log" -Tail 40
    throw 'Expected only missing nio_start_v27 from the original Rust library'
}
Get-FileHash "$BuildDirectory\rust-target\release\sql_nio.lib", "$BuildDirectory\rust-baseline-target\release\sql_nio.lib",
    "$BuildDirectory\nio_abi_old_control.exe", "$BuildDirectory\nio_abi_old_new.exe", "$BuildDirectory\nio_abi_new_control.exe" -Algorithm SHA256 |
    Format-List | Out-File "$evidence\artifacts.log" -Encoding utf8
Write-Output "NIO_ABI_EXPECTED_LINK_FAILURE native_exit=$negativeExit symbol=nio_start_v27"
Write-Output 'NIO_ABI_MIX_PASS'
exit 0
