param(
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$CMake,
    [int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$build = "$SourceRoot\build_phase0_nio"
if (!(Test-Path "$build\CMakeCache.txt")) { throw 'Configure native-startup first' }
$root = "$SourceRoot\build_phase0\native-install-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
$prefix = "$root\installed product"
Write-Output "NATIVE_INSTALL_ROOT=$root"

function Invoke-Stage([string]$Name, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & $CMake @Arguments *> "$root\$Name.log"
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    [IO.File]::WriteAllText("$root\$Name.exit", [string]$code)
    if ($code -ne 0) {
        Get-Content "$root\$Name.log" -Tail 40
        throw "$Name failed: $code; see $root\$Name.log"
    }
}

# Use the product's actual install rules and existing compiler/dependency cache.
# Generating WiX install rules does not invoke WiX or install an MSI/service.
Invoke-Stage 'configure' @('-S', $SourceRoot, '-B', $build,
    '-DOB_BUILD_PACKAGE=ON', '-DOB_BUILD_WIX=ON', '-DOB_BUILD_RPM=OFF',
    '-DOB_BUILD_DEB=OFF', '-DOB_BUILD_TGZ=OFF')
Invoke-Stage 'build' @('--build', $build, '--target', 'seekdb', '--parallel', "$Jobs")
Invoke-Stage 'install' @('--install', $build, '--config', 'RelWithDebInfo',
    '--component', 'server', '--prefix', $prefix)

$cache = Get-Content "$build\CMakeCache.txt"
$sqliteEntry = @($cache | Where-Object { $_ -match '^OB_SQLITE_DIR:[^=]+=' })
if ($sqliteEntry.Count -ne 1) { throw 'Cannot identify installed SQLite input' }
$sqlite = $sqliteEntry[0].Substring($sqliteEntry[0].IndexOf('=') + 1)
$identities = @()
foreach ($pair in @(
    @{source="$build\src\observer\seekdb.exe"; installed='bin\seekdb.exe'},
    @{source="$sqlite\bin\sqlite3.dll"; installed='bin\sqlite3.dll'},
    @{source="$sqlite\share\sqlite3\copyright"; installed='share\licenses\sqlite3\copyright'})) {
    $expected = (Get-FileHash -LiteralPath $pair.source).Hash
    $actual = (Get-FileHash -LiteralPath "$prefix\$($pair.installed)").Hash
    if ($expected -ne $actual) { throw "Installed identity differs: $($pair.installed)" }
    $identities += [ordered]@{file=$pair.installed; sha256=$actual.ToLowerInvariant()}
}
$identities | ConvertTo-Json | Set-Content "$root\identities.json" -Encoding UTF8
$python = (Get-Command python.exe -ErrorAction Stop).Source
$savedPath = $env:PATH
$key = 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
$originalPolicy = Get-ItemPropertyValue $key LongPathsEnabled
try {
    # Resolve Python before removing build/dependency directories from PATH.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($policy in @(0, 1)) {
        Set-ItemProperty $key LongPathsEnabled $policy
        $ErrorActionPreference = 'Continue'
        & $python "$SourceRoot\tools\windows\long_path_phase0\native_cli_sql.py" `
            --source-root $SourceRoot --exe "$prefix\bin\seekdb.exe" `
            --base-units 2048 --unicode-path --daemon --default-tcp --cleanup-success `
            *> "$root\sql-p$policy.log"
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        [IO.File]::WriteAllText("$root\sql-p$policy.exit", [string]$code)
        Get-Content "$root\sql-p$policy.log" -Tail 8
        if ($code -ne 0) { throw "Installed SQL failed under policy $policy; see $root" }
    }
} finally {
    $env:PATH = $savedPath
    Set-ItemProperty $key LongPathsEnabled $originalPolicy
    [ordered]@{original=$originalPolicy; restored=(Get-ItemPropertyValue $key LongPathsEnabled)} |
        ConvertTo-Json | Set-Content "$root\policy-restored.json" -Encoding UTF8
}
$identities | ConvertTo-Json
[IO.File]::WriteAllText("$root\test.exit", '0')
Write-Output "NATIVE_INSTALL_PASS root=$root"
