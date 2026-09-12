# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
param(
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$PackageFile
)
$ErrorActionPreference = 'Stop'
$build = "$SourceRoot\build_release"
if (!(Test-Path -LiteralPath $PackageFile -PathType Leaf) -or
    [IO.Path]::GetExtension($PackageFile) -ne '.zip') { throw 'A built ZIP package is required' }
$root = "$SourceRoot\build_phase0\package-check-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$unpacked = "$root\unpacked"
[IO.Compression.ZipFile]::ExtractToDirectory($PackageFile, $unpacked)
$images = @(Get-ChildItem -LiteralPath $unpacked -Filter seekdb.exe -Recurse -File |
    Where-Object { $_.Directory.Name -eq 'bin' })
if ($images.Count -ne 1) { throw 'Package must contain exactly one bin/seekdb.exe' }
$prefix = $images[0].Directory.Parent.FullName
$cache = Get-Content -LiteralPath "$build\CMakeCache.txt"
$sqliteEntry = @($cache | Where-Object { $_ -match '^OB_SQLITE_DIR:[^=]+=' })
if ($sqliteEntry.Count -ne 1) { throw 'Cannot identify the package SQLite input' }
$sqlite = $sqliteEntry[0].Substring($sqliteEntry[0].IndexOf('=') + 1)
$pairs = @(
    @{source="$build\src\observer\seekdb.exe"; file='bin\seekdb.exe'},
    @{source="$sqlite\bin\sqlite3.dll"; file='bin\sqlite3.dll'},
    @{source="$sqlite\share\sqlite3\copyright"; file='share\licenses\sqlite3\copyright'},
    @{source="$SourceRoot\src\share\parameter\default_parameter.json"; file='etc\default_parameter.json'},
    @{source="$SourceRoot\src\share\system_variable\default_system_variable.json"; file='etc\default_system_variable.json'},
    @{source="$build\src\share\ob_system_variable_init.json"; file='etc\ob_system_variable_init.json'},
    @{source="$SourceRoot\tools\default_srs_data_mysql.sql"; file='share\srs\default_srs_data_mysql.sql'}
)
$identities = @(foreach ($pair in $pairs) {
    $expected = (Get-FileHash -LiteralPath $pair.source).Hash
    $actual = (Get-FileHash -LiteralPath "$prefix\$($pair.file)").Hash
    if ($actual -ne $expected) { throw "Packaged file differs from its input: $($pair.file)" }
    [ordered]@{file=$pair.file; sha256=$actual.ToLowerInvariant()}
})
if (@(Get-ChildItem -LiteralPath "$prefix\share\admin" -Recurse -File).Count -eq 0) {
    throw 'Package contains no admin SQL'
}
$report = [ordered]@{
    package=$PackageFile; package_sha256=(Get-FileHash -LiteralPath $PackageFile).Hash.ToLowerInvariant()
    prefix=$prefix; files=$identities
}
$report | ConvertTo-Json -Depth 5 | Set-Content "$root\identities.json" -Encoding UTF8
# Inspect the real packaged PE resource without executing its initialization code.
$python = (Get-Command python.exe -ErrorAction Stop).Source
$sqliteDlls = @("$sqlite\bin\sqlite3.dll", "$prefix\bin\sqlite3.dll")
if ($env:SEEKDB_SQLITE_BASELINE_DLL) { $sqliteDlls += $env:SEEKDB_SQLITE_BASELINE_DLL }
& $python "$SourceRoot\tools\windows\long_path_phase0\sqlite_identity.py" --dll @sqliteDlls *> "$root\sqlite-identity.log"
if ($LASTEXITCODE -ne 0) { throw "Packaged SQLite identity check failed; see $root\sqlite-identity.log" }
& $python "$SourceRoot\tools\windows\long_path_phase0\product_identity.py" --source-root $SourceRoot --exe "$prefix\bin\seekdb.exe"
if ($LASTEXITCODE -ne 0) { throw 'Packaged product identity check failed' }
[IO.File]::WriteAllText("$root\test.exit", '0')
Write-Output "PACKAGE_CHECK_PASS ROOT=$root EXE=$prefix\bin\seekdb.exe"
