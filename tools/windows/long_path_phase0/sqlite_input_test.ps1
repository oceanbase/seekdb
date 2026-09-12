param([Parameter(Mandatory)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$root="$SourceRoot\build_phase0\sqlite-input-test-$([guid]::NewGuid().ToString('N'))"
$package="$root\package"
$cache="$root\cache"
$original="$SourceRoot\deps\3rd\vcpkg\x64-windows"
$commit='1f6bbba3da511773189e5075b6781222402d10fa'
New-Item -ItemType Directory -Path "$package\include","$package\share\sqlite3" -Force | Out-Null
Copy-Item -LiteralPath "$original\share\sqlite3\vcpkg.spdx.json","$original\share\sqlite3\copyright" -Destination "$package\share\sqlite3"
Copy-Item -LiteralPath "$original\include\sqlite3-vcpkg-config.h" -Destination "$package\include"
Copy-Item -LiteralPath "$SourceRoot\build_phase0\sqlite-inputs" -Destination $cache -Recurse
function Check-Input([string]$Name,[string]$ExpectedError) {
    $ErrorActionPreference='Continue'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$SourceRoot\deps\init\sqlite\recover-inputs.ps1" -PackageRoot $package -CacheDirectory $cache -PortCommit $commit -Offline *> "$root\$Name.log"
    $code=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    [IO.File]::WriteAllText("$root\$Name.exit",[string]$code)
    if (!$ExpectedError) {
        if ($code -ne 0) { throw "Offline source verification failed: $root/$Name.log" }
        $identity=Get-Content "$cache\$commit\verified.json" -Raw | ConvertFrom-Json
        if ($identity.files.Count -ne 10 -or $identity.installed_inputs.Count -ne 2) { throw 'Verified input set incomplete' }
    } elseif ($code -eq 0 -or !(Select-String -LiteralPath "$root\$Name.log" -SimpleMatch -Pattern $ExpectedError -Quiet)) {
        throw "Input rejection mismatch: $root/$Name.log"
    }
}
Check-Input 'offline-relocated' ''
$archive="$cache\sqlite-autoconf-3510200.tar.gz"
Move-Item -LiteralPath $archive -Destination "$archive.saved"
Check-Input 'missing-archive' 'Offline source archive missing'
Move-Item -LiteralPath "$archive.saved" -Destination $archive
$stream=[IO.File]::Open($archive,[IO.FileMode]::Append,[IO.FileAccess]::Write)
try { $stream.WriteByte(0) } finally { $stream.Dispose() }
Check-Input 'corrupt-archive' 'Cached source archive checksum mismatch'
Copy-Item -LiteralPath "$SourceRoot\build_phase0\sqlite-inputs\sqlite-autoconf-3510200.tar.gz" -Destination $archive -Force
foreach ($inputName in @('include/sqlite3-vcpkg-config.h','share/sqlite3/copyright')) {
    $target=Join-Path $package $inputName
    [IO.File]::AppendAllText($target,'corrupted test input')
    Check-Input ([IO.Path]::GetFileName($inputName)) "Installed input differs from original package: $inputName"
    Copy-Item -LiteralPath (Join-Path $original $inputName) -Destination $target -Force
}
Move-Item -LiteralPath "$cache\$commit\CMakeLists.txt" -Destination "$root\CMakeLists.txt.saved"
Check-Input 'missing-port' 'Offline port input missing: CMakeLists.txt'
[IO.File]::WriteAllText("$root\matrix.exit",'0')
Write-Output "SQLITE_INPUT_TEST_PASS root=$root cases=6"
