param([Parameter(Mandatory)][string]$SourceRoot, [Parameter(Mandatory)][string]$CMake)
$ErrorActionPreference = 'Stop'
$source = "$SourceRoot\build_phase0\sqlite-candidate\install"
$root = "$SourceRoot\build_phase0\sqlite-install-test-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
Copy-Item -LiteralPath $source -Destination "$root\relocated" -Recurse
$relocated = "$root\relocated"
function Check-Installation([string]$Name, [bool]$ShouldPass, [string]$ExpectedError) {
    $ErrorActionPreference = 'Continue'
    & $CMake "-DCMAKE_SOURCE_DIR=$SourceRoot" "-DOB_SQLITE_DIR=$relocated" -P "$SourceRoot\cmake\WindowsSQLite.cmake" *> "$root\$Name.log"
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Set-Content "$root\$Name.exit" $rc
    if ($ShouldPass) {
        if ($rc -ne 0) { throw "$Name failed: $root/$Name.log" }
    } elseif ($rc -eq 0 -or !(Select-String -LiteralPath "$root\$Name.log" -SimpleMatch $ExpectedError -Quiet)) {
        throw "$Name did not reject the intended invalid input: $root/$Name.log"
    }
}
Check-Installation 'relocation' $true ''
# Corrupt only the private copy, preserving the installed candidate and its identity.
$dll = "$relocated\bin\sqlite3.dll"
$stream = [IO.File]::Open($dll, [IO.FileMode]::Append, [IO.FileAccess]::Write)
try { $stream.WriteByte(0) } finally { $stream.Dispose() }
Check-Installation 'corrupt-dll' $false 'SQLite installation hash mismatch'
Copy-Item -LiteralPath "$source\bin\sqlite3.dll" -Destination $dll -Force
Check-Installation 'restored' $true ''
$license = "$relocated\share\sqlite3\copyright"
[IO.File]::AppendAllText($license, 'corrupted test notice')
Check-Installation 'corrupt-license' $false 'SQLite installation hash mismatch'
Copy-Item -LiteralPath "$source\share\sqlite3\copyright" -Destination $license -Force
Check-Installation 'license-restored' $true ''
Rename-Item -LiteralPath "$relocated\seekdb-sqlite-install.json" -NewName 'identity.saved.json'
Check-Installation 'missing-identity' $false 'Missing SQLite installation identity'
Set-Content "$root\matrix.exit" 0
Write-Output "SQLITE_INSTALL_TEST_PASS root=$root cases=6"
