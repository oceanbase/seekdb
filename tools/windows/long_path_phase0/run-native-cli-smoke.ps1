param([Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$exe=Join-Path $SourceRoot 'build_phase0_nio/src/observer/seekdb.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Build native product first' }
$root=Join-Path $SourceRoot ('build_phase0/cli-smoke-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($root) | Out-Null
$cwd=Join-Path $root 'cwd'
[IO.Directory]::CreateDirectory($cwd) | Out-Null
Write-Output "EXE_SHA256=$((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)"
function Invoke-Case([string]$Name, [string]$Arguments, [int]$Expected) {
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$exe
    $start.Arguments=$Arguments
    $start.WorkingDirectory=$cwd
    $start.UseShellExecute=$false
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process
    $process.StartInfo=$start
    try {
        if (-not $process.Start()) { throw 'Cannot start product' }
        $out=$process.StandardOutput.ReadToEndAsync()
        $err=$process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Case $Name timed out"
        }
        [IO.File]::WriteAllText((Join-Path $root ($Name+'.stdout.log')), $out.Result)
        [IO.File]::WriteAllText((Join-Path $root ($Name+'.stderr.log')), $err.Result)
        $code=$process.ExitCode
        Write-Output "CLI_CASE=$Name EXIT=$code EXPECTED=$Expected PID=$($process.Id)"
        if ($code -ne $Expected) { throw "Unexpected exit for $Name; logs: $root" }
    } finally { $process.Dispose() }
}
Invoke-Case 'version' '--version' 0
Invoke-Case 'help' '--help' 0
$base=Join-Path $root 'rejected'
while ($base.Length -lt 2049) {
    $remaining=2049-$base.Length
    if ($remaining -eq 1) { $base+='a' } else {
        $base+='\'+('a'*[Math]::Min(100,$remaining-1))
    }
}
Invoke-Case 'base-2049' ('--base-dir "'+$base+'" --embedded --nodaemon') 2
if ([IO.Directory]::Exists((Join-Path $root 'rejected'))) { throw 'Rejected base created directories' }
if (@(Get-ChildItem -LiteralPath $cwd -Force).Count -ne 0) { throw 'CLI created files in original cwd' }
Write-Output "NATIVE_CLI_SMOKE_PASS LOG_ROOT=$root"
