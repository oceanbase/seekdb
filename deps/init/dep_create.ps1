<#
.SYNOPSIS
    Windows dependency initialization script - mirrors dep_create.sh on Linux/macOS.

.DESCRIPTION
    Reads oceanbase.windows.x86_64.deps, downloads tar.gz archives from the
    configured HTTP repo, and extracts them into deps/3rd/.

    After extraction the layout is:
        deps/3rd/vcpkg/x64-windows/   (vcpkg installed packages)
        deps/3rd/openssl/             (OpenSSL)
        deps/3rd/tools/cmake/         (CMake)
        deps/3rd/tools/ninja/         (Ninja)
        deps/3rd/tools/llvm18/        (LLVM 18)
        deps/3rd/tools/win_flex_bison/(Flex & Bison)

.EXAMPLE
    powershell -File dep_create.ps1
#>

$ErrorActionPreference = "Stop"
$ScriptDir  = $PSScriptRoot
$TOPDIR     = (Resolve-Path "$ScriptDir\..\..").Path
$DEPS_3RD   = "$TOPDIR\deps\3rd"
$DONE_FILE  = "$DEPS_3RD\DONE"
$DEP_FILE   = "$ScriptDir\oceanbase.windows.x86_64.deps"

# -- Helpers ----------------------------------------------------------
function Write-Log  { param([string]$msg) Write-Host "[dep_create.ps1] $msg" }
function Write-Err  { param([string]$msg) Write-Host "[dep_create.ps1][ERROR] $msg" -ForegroundColor Red }

# -- Check deps file -------------------------------------------------
if (-not (Test-Path $DEP_FILE)) {
    Write-Err "Dependencies profile not found: $DEP_FILE"
    exit 2
}
Write-Log "Using dependencies profile: $DEP_FILE"

# -- MD5 cache check -------------------------------------------------
$md5 = (Get-FileHash -Path $DEP_FILE -Algorithm MD5).Hash.ToLower()
$MD5_FILE = "$DEPS_3RD\$md5"

if ((Test-Path $MD5_FILE) -and (Test-Path $DONE_FILE)) {
    Write-Log "Dependencies already initialized (MD5=$md5). Skipping."
    exit 0
}
Write-Log "Initializing dependencies ... (MD5=$md5)"

# -- Parse deps file (INI-like, same format as Linux) ----------------
$targets  = @{}          # target-name -> repo URL
$sections = [ordered]@{} # section-name -> string[] of package filenames

$curSection = ""
$curLines   = [System.Collections.Generic.List[string]]::new()

function Save-Section {
    if ($curSection -eq "") { return }
    if ($curSection -match "^target-(.+)$") {
        $tName = $Matches[1]
        foreach ($l in $curLines) {
            if ($l -match "^repo=(.+)$") {
                $script:targets[$tName] = $Matches[1].Trim()
                Write-Log "  target: $tName  repo: $($Matches[1].Trim())"
            }
        }
    } else {
        $script:sections[$curSection] = @($curLines)
    }
}

foreach ($rawLine in Get-Content $DEP_FILE -Encoding UTF8) {
    $line = $rawLine.Trim()
    if ($line -match "^\[(.+)\]$") {
        Save-Section
        $curSection = $Matches[1]
        $curLines   = [System.Collections.Generic.List[string]]::new()
    }
    elseif ($line -ne "" -and -not $line.StartsWith("#")) {
        $curLines.Add($line)
    }
}
Save-Section

$defaultRepo = if ($targets.ContainsKey("default")) { $targets["default"] } else { "" }
if ($defaultRepo -eq "") {
    Write-Err "No [target-default] repo defined in $DEP_FILE"
    exit 3
}

# -- Prepare deps/3rd ------------------------------------------------
if (Test-Path $DEPS_3RD) {
    Write-Log "Removing existing deps/3rd ..."
    Remove-Item -Recurse -Force $DEPS_3RD
}
New-Item -ItemType Directory -Path "$DEPS_3RD\pkg" -Force | Out-Null

# -- Download & extract ----------------------------------------------
$totalPkgs  = ($sections.Values | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum
$currentPkg = 0

foreach ($sect in $sections.Keys) {
    Write-Log "-- section [$sect] --"
    foreach ($entry in $sections[$sect]) {
        $parts   = $entry -split '\s+'
        $pkgFile = $parts[0]

        # Optional per-package target override: "pkg.tar.gz target=xxx"
        $tName = "default"
        foreach ($p in $parts) {
            if ($p -match "^target=(.+)$") { $tName = $Matches[1] }
        }

        $repo = if ($targets.ContainsKey($tName)) { $targets[$tName] } else { $defaultRepo }
        $url     = "$repo/$pkgFile"
        $pkgPath = "$DEPS_3RD\pkg\$pkgFile"

        $currentPkg++
        Write-Log "[$currentPkg/$totalPkgs] $pkgFile"

        # -- Download ------------------------------------------------
        if (Test-Path $pkgPath) {
            Write-Log "  cached"
        } else {
            Write-Log "  downloading from $url ..."
            # Schannel/CI: use --ssl-no-revoke (avoids curl 35 / revocation offline).
            $tmpPath = "$pkgPath.tmp"
            if (Test-Path $tmpPath) { Remove-Item -Force $tmpPath -ErrorAction SilentlyContinue }
            & curl.exe -L -f -sS --connect-timeout 120 --ssl-no-revoke -o $tmpPath $url
            if ($LASTEXITCODE -ne 0) {
                if (Test-Path $tmpPath) { Remove-Item -Force $tmpPath -ErrorAction SilentlyContinue }
                Write-Err "Failed to download: $url (curl exit $LASTEXITCODE)"
                exit 4
            }
            Move-Item -Force $tmpPath $pkgPath
        }

        # -- Extract -------------------------------------------------
        Write-Log "  extracting ..."
        & tar -xzf $pkgPath -C $DEPS_3RD --strip-components=1
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Failed to extract: $pkgFile"
            exit 5
        }
        Write-Log "  done"
    }
}

# -- Mark done --------------------------------------------------------
New-Item -ItemType File -Path $MD5_FILE  -Force | Out-Null
New-Item -ItemType File -Path $DONE_FILE -Force | Out-Null

Write-Log ""
Write-Log "Dependencies initialized successfully."
Write-Log ""
Write-Log "Layout:"
Write-Log "  deps/3rd/vcpkg/x64-windows/    vcpkg packages"
Write-Log "  deps/3rd/openssl/              OpenSSL"
Write-Log "  deps/3rd/tools/cmake/          CMake"
Write-Log "  deps/3rd/tools/ninja/          Ninja"
Write-Log "  deps/3rd/tools/llvm18/         LLVM 18"
Write-Log "  deps/3rd/tools/win_flex_bison/ Flex & Bison"
