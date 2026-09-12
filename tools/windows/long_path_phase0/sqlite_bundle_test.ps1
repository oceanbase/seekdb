param([Parameter(Mandatory)][string]$SourceRoot, [Parameter(Mandatory)][string]$CMake)
$ErrorActionPreference = 'Stop'
$candidate = "$SourceRoot\build_phase0\sqlite-candidate\install\bin\sqlite3.dll"
$original = "$SourceRoot\deps\3rd\vcpkg\x64-windows\bin\sqlite3.dll"
$probe = "$SourceRoot\build_phase0\sqlite_path_probe.exe"
foreach ($file in @($candidate, $original, $probe)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing test input: $file" }
}
$expected = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()
$oldHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant()
if ($expected -eq $oldHash) { throw 'Candidate and original must differ for this test' }
$root = "$SourceRoot\build_phase0\sqlite-bundle-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
Copy-Item -LiteralPath $probe -Destination "$root\probe.exe"
Copy-Item -LiteralPath $original -Destination "$root\sqlite3.dll"
$argsCommon = @("-DEXE=$root/probe.exe", "-DOUT_DIR=$root", "-DSEARCH_DIRS=$(Split-Path $original);$(Split-Path $candidate)", "-DSQLITE_DLL=$candidate")
$ErrorActionPreference = 'Continue'
& $CMake @argsCommon "-DSQLITE_SHA256=$expected" -P "$SourceRoot\cmake\BundleRuntimeDllsWindows.cmake" *> "$root\positive.log"
$positive = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Set-Content "$root\positive.exit" $positive
if ($positive -ne 0) { throw "Bundling failed; see $root/positive.log" }
if ((Get-FileHash "$root\sqlite3.dll").Hash -ne $expected) { throw 'Old DLL overwrote candidate' }
# A changed configured identity must fail before replacing any destination DLL.
Copy-Item -LiteralPath $original -Destination "$root\sqlite3.dll" -Force
$ErrorActionPreference = 'Continue'
& $CMake @argsCommon "-DSQLITE_SHA256=$oldHash" -P "$SourceRoot\cmake\BundleRuntimeDllsWindows.cmake" *> "$root\negative.log"
$negative = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Set-Content "$root\negative.exit" $negative
if ($negative -eq 0 -or !(Select-String -LiteralPath "$root\negative.log" -SimpleMatch 'SQLite DLL changed after configuration' -Quiet)) { throw 'Expected hash mismatch rejection' }
if ((Get-FileHash "$root\sqlite3.dll").Hash -ne $oldHash) { throw 'Failed validation changed destination' }
Set-Content "$root\matrix.exit" 0
Write-Output "SQLITE_BUNDLE_PASS root=$root candidate=$expected positive=$positive negative=$negative"
