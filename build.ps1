<#
.SYNOPSIS
    OceanBase Lite Windows build script - mirrors build.sh on Linux/macOS.

.EXAMPLE
    .\build.ps1 -h
    .\build.ps1 init
    .\build.ps1 release
    .\build.ps1 release --ninja
    .\build.ps1 release --ninja -j 16
    .\build.ps1 release --ninja --init
    .\build.ps1 debug
    .\build.ps1 clean
#>

# Manual arg parsing to support both -flag and --flag styles (like build.sh)
$Action = "debug"
$Ninja  = $false
$Init   = $false
$Jobs   = 0
$h             = $false
$NinjaTarget  = "observer"
$ExtraCmake   = @()

$i = 0
while ($i -lt $args.Count) {
    $a = "$($args[$i])"
    switch -Wildcard ($a) {
        { $_ -in "-h", "--help", "-help" } { $h = $true }
        { $_ -in "--ninja", "-ninja" }     { $Ninja = $true }
        { $_ -in "--init", "-init" }       { $Init = $true }
        { $_ -in "--target" } {
            $i++
            if ($i -lt $args.Count) { $NinjaTarget = "$($args[$i])" }
        }
        { $_ -in "-j", "--jobs" } {
            $i++
            if ($i -lt $args.Count) { $Jobs = [int]$args[$i] }
        }
        default {
            if (-not $a.StartsWith("-")) { $Action = $a }
            elseif ($a.StartsWith("-D")) { $ExtraCmake += $a }
            else { Write-Host "[build.ps1][WARN] Unknown flag: $a" -ForegroundColor Yellow }
        }
    }
    $i++
}

$ErrorActionPreference = "Stop"
$TOPDIR = $PSScriptRoot

# Force all tool output to English (avoids encoding issues on non-English systems)
$env:DOTNET_CLI_UI_LANGUAGE = "en"
$env:VSLANG                = "1033"
[Console]::OutputEncoding   = [System.Text.Encoding]::UTF8
$OutputEncoding             = [System.Text.Encoding]::UTF8

# -- Dependency paths -------------------------------------------------
# Priority: env var > deps/3rd (if initialized via `init`) > system defaults
$DEPS_3RD  = "$TOPDIR\deps\3rd"
$TOOLS_DIR = "$DEPS_3RD\tools"
$DepsInitialized = Test-Path "$DEPS_3RD\DONE"

$DefaultVcpkgDir = if ($env:OB_VCPKG_DIR) { $env:OB_VCPKG_DIR }
    elseif ($DepsInitialized) { "$DEPS_3RD\vcpkg\x64-windows" }
    else { "C:/VcpkgInstalled/x64-windows" }

$DefaultOpenSSLDir = if ($env:OB_OPENSSL_DIR) { $env:OB_OPENSSL_DIR }
    elseif ($DepsInitialized) { "$DEPS_3RD\openssl" }
    else { "C:/Program Files/OpenSSL-Win64" }

$DefaultLLVMDir = if ($env:OB_LLVM_DIR) { $env:OB_LLVM_DIR }
    elseif ($DepsInitialized) { "$TOOLS_DIR\llvm18" }
    else { "C:/Program Files/LLVM18" }

# When deps/3rd is initialized, add tools to PATH
if ($DepsInitialized) {
    $toolPaths = @(
        "$TOOLS_DIR\cmake\bin",
        "$TOOLS_DIR\ninja",
        "$TOOLS_DIR\llvm18\bin",
        "$TOOLS_DIR\win_flex_bison"
    )
    foreach ($tp in $toolPaths) {
        if ((Test-Path $tp) -and ($env:PATH -notlike "*$tp*")) {
            $env:PATH = "$tp;$env:PATH"
        }
    }
}

# -- Helpers ---------------------------------------------------------
function Write-Log  { param([string]$msg) Write-Host "[build.ps1] $msg" }
function Write-Err  { param([string]$msg) Write-Host "[build.ps1][ERROR] $msg" -ForegroundColor Red }

# -- Code signing (DigiCert Software Trust Manager) -----------------
# Enabled automatically when SM_API_KEY env var is present.
# Required env vars: SM_API_KEY, SM_CLIENT_CERT_FILE,
#                    SM_CLIENT_CERT_PASSWORD, SM_HOST
# Required tools:    smctl, signtool (Windows SDK)

$script:SigningReady = $null   # $null = not checked, $true/$false = result

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $sdkGlobs = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe",
        "$env:ProgramFiles\Windows Kits\10\bin\*\x64\signtool.exe"
    )
    foreach ($g in $sdkGlobs) {
        $found = Get-ChildItem -Path $g -ErrorAction SilentlyContinue |
                 Sort-Object { [version]($_.Directory.Parent.Name) } -Descending |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

function Initialize-CodeSigning {
    if ($null -ne $script:SigningReady) { return $script:SigningReady }

    if (-not $env:SM_API_KEY) {
        Write-Log "Code signing: SM_API_KEY not set, skipping."
        $script:SigningReady = $false
        return $false
    }

    $smctl = Get-Command smctl -ErrorAction SilentlyContinue
    if (-not $smctl) {
        Write-Err "SM_API_KEY is set but smctl not found in PATH. Signing disabled."
        $script:SigningReady = $false
        return $false
    }

    $script:SignToolPath = Find-SignTool
    if (-not $script:SignToolPath) {
        Write-Err "signtool.exe not found (need Windows SDK). Signing disabled."
        $script:SigningReady = $false
        return $false
    }

    Write-Log "Code signing: syncing certificates from DigiCert STM..."
    & smctl windows certsync | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Err "smctl windows certsync failed. Signing disabled."
        $script:SigningReady = $false
        return $false
    }

    Write-Log "Code signing: ready (signtool=$($script:SignToolPath))"
    $script:SigningReady = $true
    return $true
}

function Do-CodeSign {
    param([string[]]$Files)

    if (-not (Initialize-CodeSigning)) { return }

    foreach ($f in $Files) {
        if (-not (Test-Path $f)) {
            Write-Err "Cannot sign, file not found: $f"
            continue
        }
        $name = Split-Path $f -Leaf
        Write-Log "Signing $name ..."
        & $script:SignToolPath sign `
            /tr http://timestamp.digicert.com /td sha256 `
            /fd sha256 /a $f | Out-Host
        if ($LASTEXITCODE -eq 0) {
            Write-Log "  Signed: $name"
        } else {
            Write-Err "  Signing failed for $name (exit code $LASTEXITCODE)"
        }
    }
}

function Show-Usage {
    Write-Host @"

Usage:
    .\build.ps1 -h                              Show this help
    .\build.ps1 init                            Download & extract deps (from HTTP)
    .\build.ps1 pack                            Pack deps from this machine into tar.gz
    .\build.ps1 clean                           Remove build_* directories
    .\build.ps1 [BuildType]                       Configure only (cmake)
    .\build.ps1 [BuildType] --ninja               Configure + compile (ninja)
    .\build.ps1 [BuildType] --ninja -j 16         Compile with 16 jobs
    .\build.ps1 [BuildType] --ninja --init          Init deps, then build
    .\build.ps1 release --ninja --target libseekdb   Build embed DLL (example)
    .\build.ps1 package                           Build release + MSI/ZIP installer

BuildType:
    debug           Debug build (default)
    release         RelWithDebInfo build
    relwithdebinfo  Alias for release

Flags:
    --ninja         Configure + compile with Ninja
    --init          Run dependency init before building (like build.sh --init)
    --target NAME   Ninja target (default: observer); e.g. libseekdb
    -DVAR=VALUE     Extra CMake cache entries (repeatable); e.g. -DBUILD_EMBED_MODE=ON

Environment variables (override dependency paths):
    OB_VCPKG_DIR      vcpkg install root   (default: deps/3rd or C:/VcpkgInstalled)
    OB_OPENSSL_DIR    OpenSSL root          (default: deps/3rd or C:/Program Files/OpenSSL-Win64)
    OB_LLVM_DIR       LLVM 18 root          (default: deps/3rd or C:/Program Files/LLVM18)

"@
}

if ($Jobs -eq 0) {
    $cpuCount = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
    if (-not $cpuCount -or $cpuCount -lt 1) { $cpuCount = 4 }
    $totalMemGB = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    $memJobs = [math]::Max(1, [math]::Floor($totalMemGB / 3))
    $Jobs = [math]::Min($cpuCount, $memJobs)
    Write-Log "Auto jobs: $Jobs (cpus=$cpuCount, mem=${totalMemGB}GB, ~3GB/job)"
}

# -- init: download & extract dependencies (mirrors build.sh init) ---
function Do-Init {
    $depCreateScript = "$TOPDIR\deps\init\dep_create.ps1"
    if (-not (Test-Path $depCreateScript)) {
        Write-Err "dep_create.ps1 not found: $depCreateScript"
        exit 1
    }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Log "Running dep_create.ps1 ..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File $depCreateScript
    if ($LASTEXITCODE -ne 0) {
        Write-Err "dep_create.ps1 failed (exit code $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
    $sw.Stop()
    $min = [math]::Floor($sw.Elapsed.TotalSeconds / 60)
    $sec = $sw.Elapsed.Seconds
    Write-Log "dep_create.ps1 completed in ${min}m${sec}s"

    # Refresh path defaults now that deps/3rd is populated
    $script:DepsInitialized = $true
    $script:DefaultVcpkgDir  = "$DEPS_3RD\vcpkg\x64-windows"
    $script:DefaultOpenSSLDir = "$DEPS_3RD\openssl"
    $script:DefaultLLVMDir   = "$TOOLS_DIR\llvm18"

    $toolPaths = @(
        "$TOOLS_DIR\cmake\bin",
        "$TOOLS_DIR\ninja",
        "$TOOLS_DIR\llvm18\bin",
        "$TOOLS_DIR\win_flex_bison"
    )
    foreach ($tp in $toolPaths) {
        if ((Test-Path $tp) -and ($env:PATH -notlike "*$tp*")) {
            $env:PATH = "$tp;$env:PATH"
        }
    }
}

# -- pack: create tar.gz dep archives from current machine -----------
function Do-Pack {
    $packScript = "$TOPDIR\deps\init\pack_win_deps.ps1"
    if (-not (Test-Path $packScript)) {
        Write-Err "pack_win_deps.ps1 not found: $packScript"
        exit 1
    }

    $outDir = "$TOPDIR\win_deps_archives"
    if (-not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Path $outDir | Out-Null
    }

    Write-Log "Running pack_win_deps.ps1 -> $outDir ..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File $packScript -OutputDir $outDir
    if ($LASTEXITCODE -ne 0) {
        Write-Err "pack_win_deps.ps1 failed (exit code $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

# -- clean -----------------------------------------------------------
function Do-Clean {
    Write-Log "Cleaning build directories ..."
    Get-ChildItem -Path $TOPDIR -Directory -Filter "build*" | ForEach-Object {
        Write-Log "  Removing $($_.Name)"
        Remove-Item -Recurse -Force $_.FullName
    }
    Write-Log "Clean done."
}

# -- cmake configure ------------------------------------------------
function Do-Build {
    param(
        [string]$BuildType,
        [string[]]$ExtraCMakeArgs = @()
    )

    # Align directory names with build.sh: release -> build_release, debug -> build_debug
    $folderName = switch ($BuildType) {
        "RelWithDebInfo" { "release" }
        "Debug"          { "debug" }
        default          { $BuildType.ToLower() }
    }
    $buildDir = "$TOPDIR\build_$folderName"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    $cmakeArgs = @(
        $TOPDIR,
        "-G", "Ninja",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=1",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DOB_USE_LLD=ON",
        "-DOB_VCPKG_DIR=$DefaultVcpkgDir",
        "-DOB_OPENSSL_DIR=$DefaultOpenSSLDir",
        "-DOB_LLVM_DIR=$DefaultLLVMDir"
    ) + $ExtraCMakeArgs + $ExtraCmake

    Write-Log "CMake configure: build_$folderName"
    Write-Log "  Build type : $BuildType"
    Write-Log "  VcpkgDir   : $DefaultVcpkgDir"
    Write-Log "  OpenSSLDir : $DefaultOpenSSLDir"
    Write-Log "  LLVMDir    : $DefaultLLVMDir"
    Write-Log ""

    Push-Location $buildDir
    try {
        & cmake @cmakeArgs | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Err "CMake configure failed (exit code $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
        Write-Log "CMake configure succeeded."

        # Copy compile_commands.json to project root for IDE support
        $ccJson = "$buildDir\compile_commands.json"
        if (Test-Path $ccJson) {
            Copy-Item $ccJson "$TOPDIR\compile_commands.json" -Force
            Write-Log "compile_commands.json copied to project root."
        }
    }
    finally {
        Pop-Location
    }

    return $buildDir
}

# -- ninja build -----------------------------------------------------
function Do-Ninja {
    param(
        [string]$BuildDir,
        [string]$Target = "observer"
    )

    Write-Log "Building with Ninja (-j $Jobs) target=$Target in $BuildDir ..."
    Push-Location $BuildDir
    try {
        & ninja -j $Jobs $Target | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Build failed (exit code $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
        Write-Log "Build succeeded!"
    }
    finally {
        Pop-Location
    }
}

# -- build seekdb Configurator (.NET WPF wizard) ---------------------
function Do-BuildConfigurator {
    $projDir = "$TOPDIR\tools\windows\seekdbConfigurator"
    $proj    = "$projDir\seekdbConfigurator.csproj"
    $pubDir  = "$projDir\publish"

    if (-not (Test-Path $proj)) {
        Write-Err "Configurator project not found: $proj"
        return $false
    }

    $dotnetCmd = Get-Command dotnet -ErrorAction SilentlyContinue
    if (-not $dotnetCmd) {
        Write-Err ".NET SDK not found. Install .NET 8 SDK to build the Configurator."
        Write-Log "  The MSI will be created without the Configurator wizard."
        return $false
    }

    Write-Log "Building seekdb Configurator (self-contained, single-file) ..."
    if (Test-Path $pubDir) { Remove-Item -Recurse -Force $pubDir }

    & dotnet publish $proj `
        -c Release `
        -r win-x64 `
        --self-contained `
        -p:PublishSingleFile=true `
        -p:IncludeNativeLibrariesForSelfExtract=true `
        -o $pubDir | Out-Host

    if ($LASTEXITCODE -ne 0) {
        Write-Err "Configurator build failed (exit code $LASTEXITCODE)"
        return $false
    }

    if (Test-Path "$pubDir\seekdbConfigurator.exe") {
        Write-Log "Configurator built: $pubDir\seekdbConfigurator.exe"
        Do-CodeSign "$pubDir\seekdbConfigurator.exe"
        return $true
    } else {
        Write-Err "seekdbConfigurator.exe not found after publish."
        return $false
    }
}

# -- package: build release + create installer -----------------------
function Do-Package {
    # Build the Configurator first so it is available when cpack runs
    $cfgOk = Do-BuildConfigurator
    if (-not $cfgOk) {
        Write-Log "Proceeding without Configurator in the MSI."
    }

    $buildDir = Do-Build -BuildType "RelWithDebInfo" -ExtraCMakeArgs @("-DOB_BUILD_PACKAGE=ON")
    Do-Ninja -BuildDir $buildDir -Target observer

    # Sign binaries before they are packaged into the MSI
    $exesToSign = @(
        "$buildDir\src\observer\seekdb.exe"
    ) | Where-Object { Test-Path $_ }
    if ($exesToSign) { Do-CodeSign $exesToSign }

    Write-Log "Creating installer package in $buildDir ..."
    Push-Location $buildDir
    try {
        $wixFound = Get-Command wix -ErrorAction SilentlyContinue
        if ($wixFound) {
            Write-Log "WiX v4 found, generating MSI..."
            & cpack -G WIX -C RelWithDebInfo | Out-Host
            if ($LASTEXITCODE -ne 0) {
                Write-Log "WiX MSI generation failed, falling back to ZIP..."
                & cpack -G ZIP -C RelWithDebInfo | Out-Host
            }
        } else {
            Write-Log "WiX not found, generating ZIP package..."
            Write-Log "  To generate MSI: dotnet tool install --global wix"
            & cpack -G ZIP -C RelWithDebInfo | Out-Host
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Package generation failed (exit code $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
        $packages = @(
            Get-ChildItem -Path "$buildDir\seekdb-*.msi" -File -ErrorAction SilentlyContinue
            Get-ChildItem -Path "$buildDir\seekdb-*.zip" -File -ErrorAction SilentlyContinue
        )
        if ($packages) {
            # Sign MSI installers
            $msiFiles = $packages | Where-Object { $_.Extension -eq ".msi" }
            if ($msiFiles) { Do-CodeSign ($msiFiles | ForEach-Object { $_.FullName }) }

            Write-Log "Package(s) created:"
            foreach ($pkg in $packages) {
                Write-Log "  $($pkg.FullName)"
            }
        }
        Write-Log "Package build succeeded!"
    }
    finally {
        Pop-Location
    }
}

# -- Main ------------------------------------------------------------
if ($h) {
    Show-Usage
    exit 0
}

switch ($Action.ToLower()) {
    "init" {
        Do-Init
    }
    "pack" {
        Do-Pack
    }
    "package" {
        if ($Init) { Do-Init }
        Do-Package
    }
    "clean" {
        Do-Clean
    }
    { $_ -in "release", "relwithdebinfo" } {
        if ($Init) { Do-Init }
        $buildDir = Do-Build -BuildType "RelWithDebInfo"
        if ($Ninja) { Do-Ninja -BuildDir $buildDir -Target $NinjaTarget }
    }
    { $_ -in "debug", "" } {
        if ($Init) { Do-Init }
        $buildDir = Do-Build -BuildType "Debug"
        if ($Ninja) { Do-Ninja -BuildDir $buildDir -Target $NinjaTarget }
    }
    "-h" {
        Show-Usage
    }
    default {
        Write-Err "Unknown action: $Action"
        Show-Usage
        exit 1
    }
}
