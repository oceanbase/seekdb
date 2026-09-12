# Recover the original package recipe. A version match alone is insufficient.
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$CacheDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$PortCommit,
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$metadata = Join-Path $PackageRoot 'share/sqlite3/vcpkg.spdx.json'
$spdx = Get-Content -Raw -LiteralPath $metadata | ConvertFrom-Json
$port = @($spdx.packages | Where-Object SPDXID -eq 'SPDXRef-port')
if ($port.Count -ne 1 -or $port[0].versionInfo -ne '3.51.2') {
    throw 'Expected the recorded SQLite 3.51.2 package'
}
# These installed inputs are copied into the new build/installation. Verify
# them against the original package just like the port and source archive.
$installedInputs = @()
foreach ($name in @('include/sqlite3-vcpkg-config.h', 'share/sqlite3/copyright')) {
    $entry = @($spdx.files | Where-Object { $_.fileName -eq "./$name" -and $_.SPDXID -like 'SPDXRef-binary-file-*' })
    if ($entry.Count -ne 1) { throw "Missing installed input identity: $name" }
    $checksum = @($entry[0].checksums | Where-Object algorithm -eq 'SHA256')
    if ($checksum.Count -ne 1 -or $checksum[0].checksumValue -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Missing installed input SHA256: $name"
    }
    $actual = (Get-FileHash -LiteralPath (Join-Path $PackageRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $checksum[0].checksumValue) { throw "Installed input differs from original package: $name" }
    $installedInputs += [ordered]@{ name=$name; sha256=$actual }
    Write-Output "INSTALLED_INPUT_VERIFIED=$name"
}
$destination = Join-Path $CacheDirectory $PortCommit
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$verified = @()
foreach ($file in $spdx.files) {
    if ($file.SPDXID -notlike 'SPDXRef-port-file-*') { continue }
    $name = $file.fileName -replace '^\./', ''
    if ($name -notmatch '^[A-Za-z0-9_.-]+$') { throw "Unsafe port filename: $name" }
    $checksum = @($file.checksums | Where-Object algorithm -eq 'SHA256')
    if ($checksum.Count -ne 1) { throw "Missing SHA256: $name" }
    $path = Join-Path $destination $name
    $url = "https://raw.githubusercontent.com/microsoft/vcpkg/$PortCommit/ports/sqlite3/$name"
    if (!(Test-Path -LiteralPath $path)) {
        if ($Offline) { throw "Offline port input missing: $name" }
        $partial = "$path.partial"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $partial -TimeoutSec 60
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $checksum[0].checksumValue) {
            throw "Port file differs from original package: $name ($partial retained)"
        }
        Move-Item -LiteralPath $partial -Destination $path
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $checksum[0].checksumValue) { throw "Cached port file differs: $name" }
    $verified += [ordered]@{ name=$name; url=$url; sha256=$actual }
    Write-Output "PORT_FILE_VERIFIED=$name"
}
if ($verified.Count -eq 0) { throw 'No port files in package metadata' }
[ordered]@{ commit=$PortCommit; metadata_sha256=(Get-FileHash $metadata).Hash.ToLowerInvariant(); files=$verified; installed_inputs=$installedInputs } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'verified.json') -Encoding UTF8
Write-Output "SQLITE_PORT_VERIFIED=$destination"

# Lock the upstream archive named by this verified recipe as well as the port.
$archiveName = 'sqlite-autoconf-3510200.tar.gz'
$archiveUrl = "https://sqlite.org/2026/$archiveName"
$archiveSha512 = '0f59037e537543154711fdfa5707592658121e8b0973eb07396a9fb980e6c0c822717d0a787c564ebd6bf7383a3388f128fce6adbbf98b417909855d42ac8710'
$recipe = Get-Content -Raw -LiteralPath (Join-Path $destination 'portfile.cmake')
if (!$recipe.Contains($archiveSha512)) { throw 'Verified port archive checksum differs from the locked source' }
$archive = Join-Path $CacheDirectory $archiveName
if (!(Test-Path -LiteralPath $archive)) {
    if ($Offline) { throw 'Offline source archive missing' }
    $partial = "$archive.partial"
    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $partial -TimeoutSec 120
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA512).Hash -ne $archiveSha512) {
        throw "Source archive checksum mismatch ($partial retained)"
    }
    Move-Item -LiteralPath $partial -Destination $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $archiveSha512) {
    throw 'Cached source archive checksum mismatch'
}
[ordered]@{
    url=$archiveUrl; sha512=$archiveSha512
    sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    port_commit=$PortCommit
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'source-verified.json') -Encoding UTF8
Write-Output "SQLITE_SOURCE_VERIFIED=$archive"
