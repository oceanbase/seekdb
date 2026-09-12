<#
.SYNOPSIS
    Configure or build the seekdb CMake compatibility release on Windows x64.

.EXAMPLE
    .\build.ps1 release --init --ninja -j 16
    .\build.ps1 package --init -j 16
    .\build.ps1 phase0 -j 4
#>

$ErrorActionPreference = "Stop"
$TOPDIR = $PSScriptRoot
$Action = "release"
$Build = $false
$Init = $false
$Jobs = 0
$Help = $false
$ExtraCMakeArgs = @()

$i = 0
while ($i -lt $args.Count) {
    $arg = "$($args[$i])"
    switch -Wildcard ($arg) {
        { $_ -in "-h", "--help", "-help" } { $Help = $true }
        { $_ -in "--ninja", "-ninja", "--make" } { $Build = $true }
        { $_ -in "--init", "-init" } { $Init = $true }
        { $_ -in "-j", "--jobs" } {
            $i++
            if ($i -ge $args.Count) { throw "$arg requires a job count" }
            $Jobs = [int]$args[$i]
        }
        { $_.StartsWith("-D") } { $ExtraCMakeArgs += $arg }
        default {
            if ($arg.StartsWith("-")) { throw "unsupported option: $arg" }
            $Action = $arg
        }
    }
    $i++
}

function Write-Log { param([string]$Message) Write-Host "[build.ps1] $Message" }
function Write-Err { param([string]$Message) Write-Host "[build.ps1][ERROR] $Message" -ForegroundColor Red }

function Show-Usage {
    Write-Host @"
Usage:
    .\build.ps1 -h
    .\build.ps1 init
    .\build.ps1 sqlite-control
    .\build.ps1 sqlite-recover-inputs
    .\build.ps1 clean
    .\build.ps1 release [--init] [-DName=Value ...]
    .\build.ps1 release [--init] [-DName=Value ...] --ninja [-j N]
    .\build.ps1 package [--init] [-DName=Value ...] [-j N]
    .\build.ps1 phase0 [-DName=Value ...] [-j N]
    .\build.ps1 phase0-sqlite-temp [-DName=Value ...] [-j N]
    .\build.ps1 native-path [-DName=Value ...] [-j N]
    .\build.ps1 native-startup [-DName=Value ...] [-j N]
    .\build.ps1 native-sqlite-pool [-j N]
    .\build.ps1 native-log-lifecycle [-j N]
    .\build.ps1 native-rebuild [-j N]
    .\build.ps1 native-cli-smoke
    .\build.ps1 native-cli-sql
    .\build.ps1 native-cli-reader
    .\build.ps1 native-cli-preflight
    .\build.ps1 native-service
    .\build.ps1 native-sql-tls
    .\build.ps1 native-product-identity
    .\build.ps1 native-install [-j N]
    .\build.ps1 native-startup-contract
    .\build.ps1 phase0-context [-DName=Value ...] [-j N]
    .\build.ps1 phase0-nio [-DName=Value ...] [-j N]
    .\build.ps1 phase0-nio-abi -DOB_PHASE0_NIO_BASELINE_ROOT=<pinned-rust-workspace> [-DName=Value ...] [-j N]
Phase 0 build:
    Builds the Windows long-path probe and runs its UTF-16 fixture test in
    build_phase0 using the product project and existing dependencies.
    Does not initialize dependencies, run the path matrix, or change policy.
    phase0-context uses build_phase0_context for the explicit path-context
    component.

Supported compatibility build:
    Windows x64, RelWithDebInfo (-O2), Unity, seekdb production binary.

Package build:
    Builds seekdb and the Windows Configurator, then creates an MSI when
    WiX v4 is available. Otherwise it creates a ZIP package.
    MSI prerequisites:
      dotnet tool install --global wix
      wix extension add --global WixToolset.UI.wixext/<same-version-as-wix>

Bazel remains authoritative for modular dependencies, tests, architecture
checks, and non-release options. Invoke it through .\bazel.py directly.
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

$NativeArch = if ($env:PROCESSOR_ARCHITEW6432) {
    $env:PROCESSOR_ARCHITEW6432
} else {
    $env:PROCESSOR_ARCHITECTURE
}
if ($NativeArch -notin "AMD64", "x86_64") {
    Write-Err "Only Windows x64 is supported; detected $NativeArch"
    exit 2
}

$DEPS_3RD = "$TOPDIR\deps\3rd"
$TOOLS_DIR = "$DEPS_3RD\tools"

function Add-DependencyToolsToPath {
    $ToolPaths = @(
        "$TOOLS_DIR\cmake\bin",
        "$TOOLS_DIR\ninja",
        "$TOOLS_DIR\llvm18\bin",
        "$TOOLS_DIR\win_flex_bison"
    )
    foreach ($Path in $ToolPaths) {
        if ((Test-Path $Path) -and ($env:PATH -notlike "*$Path*")) {
            $env:PATH = "$Path;$env:PATH"
        }
    }
}

function Do-Init {
    $Script = "$TOPDIR\deps\init\dep_create.ps1"
    if (-not (Test-Path $Script)) {
        throw "dependency initializer not found: $Script"
    }
    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Script
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $Stopwatch.Stop()
    Write-Log "dependency initialization completed in $([int]$Stopwatch.Elapsed.TotalSeconds)s"
    Add-DependencyToolsToPath
}

function Do-Clean {
    $BuildDir = "$TOPDIR\build_release"
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Log "removed $BuildDir"
    } else {
        Write-Log "nothing to clean"
    }
}

# Optional code signing through DigiCert Software Trust Manager. Signing is
# enabled only when SM_API_KEY is present, so local unsigned package builds do
# not require DigiCert tooling or credentials.
$script:SigningReady = $null
$script:SignToolPath = $null

function Find-SignTool {
    $Command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }

    $SdkGlobs = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe",
        "$env:ProgramFiles\Windows Kits\10\bin\*\x64\signtool.exe"
    )
    foreach ($Glob in $SdkGlobs) {
        $Found = Get-ChildItem -Path $Glob -ErrorAction SilentlyContinue |
            Sort-Object { [version]($_.Directory.Parent.Name) } -Descending |
            Select-Object -First 1
        if ($Found) { return $Found.FullName }
    }
    return $null
}

function Initialize-CodeSigning {
    if ($null -ne $script:SigningReady) { return $script:SigningReady }
    if (-not $env:SM_API_KEY) {
        Write-Log "code signing disabled (SM_API_KEY is not set)"
        $script:SigningReady = $false
        return $false
    }

    $Smctl = Get-Command smctl -ErrorAction SilentlyContinue
    $script:SignToolPath = Find-SignTool
    if (-not $Smctl -or -not $script:SignToolPath) {
        Write-Err "SM_API_KEY is set, but smctl or signtool.exe is unavailable; signing disabled"
        $script:SigningReady = $false
        return $false
    }

    Write-Log "syncing DigiCert code-signing certificates"
    & $Smctl.Source windows certsync | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Err "smctl windows certsync failed; signing disabled"
        $script:SigningReady = $false
        return $false
    }
    $script:SigningReady = $true
    return $true
}

function Invoke-CodeSign {
    param([string[]]$Files)

    if (-not $Files -or -not (Initialize-CodeSigning)) { return }
    foreach ($File in $Files) {
        if (-not (Test-Path $File)) { continue }
        Write-Log "signing $(Split-Path $File -Leaf)"
        & $script:SignToolPath sign /tr http://timestamp.digicert.com /td sha256 `
            /fd sha256 /a $File | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "code signing failed for $File (exit code $LASTEXITCODE)"
        }
    }
}

function Get-BuildJobs {
    if ($Jobs -gt 0) { return $Jobs }
    $DetectedJobs = (Get-CimInstance Win32_Processor |
        Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
    if (-not $DetectedJobs -or $DetectedJobs -lt 1) { $DetectedJobs = 4 }
    return $DetectedJobs
}

function Invoke-SeekdbBuild {
    param(
        [System.Management.Automation.CommandInfo]$NinjaCommand,
        [string]$Directory
    )

    $BuildJobs = Get-BuildJobs
    Write-Log "building seekdb with Ninja (-j $BuildJobs)"
    & $NinjaCommand.Source -C $Directory -j $BuildJobs seekdb
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-ConfiguratorBuild {
    $ProjectDir = "$TOPDIR\tools\windows\seekdbConfigurator"
    $Project = "$ProjectDir\seekdbConfigurator.csproj"
    $PublishDir = "$ProjectDir\publish"
    if (-not (Test-Path $Project)) {
        Write-Err "Configurator project not found: $Project"
        return $false
    }

    $Dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if (-not $Dotnet) {
        Write-Err ".NET 8 SDK not found; the package will not contain the Configurator"
        return $false
    }

    Write-Log "building seekdb Configurator"
    if (Test-Path $PublishDir) {
        Remove-Item -Recurse -Force $PublishDir
    }
    & $Dotnet.Source publish $Project -c Release -r win-x64 --self-contained `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -o $PublishDir | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Configurator build failed (exit code $LASTEXITCODE)"
        return $false
    }

    $Executable = "$PublishDir\seekdbConfigurator.exe"
    if (-not (Test-Path $Executable)) {
        Write-Err "Configurator output not found: $Executable"
        return $false
    }
    Invoke-CodeSign @($Executable)
    return $true
}

function Test-WixUiExtension {
    param([System.Management.Automation.CommandInfo]$WixCommand)

    $PreviousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $ExtensionList = & $WixCommand.Source extension list --global 2>$null |
            Out-String
        $ExtensionListExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $PreviousErrorActionPreference
    }
    return ($ExtensionListExitCode -eq 0 -and
        $ExtensionList -match "WixToolset\.UI\.wixext")
}

function Invoke-PackageBuild {
    param(
        [System.Management.Automation.CommandInfo]$NinjaCommand,
        [string]$Directory,
        [bool]$ConfiguratorAvailable
    )

    Invoke-SeekdbBuild -NinjaCommand $NinjaCommand -Directory $Directory
    Invoke-CodeSign @("$Directory\src\observer\seekdb.exe")

    $CPack = Get-Command cpack -ErrorAction SilentlyContinue
    if (-not $CPack) { throw "cpack not found; install CMake 3.20+" }

    Push-Location $Directory
    try {
        $Wix = Get-Command wix -ErrorAction SilentlyContinue
        $GeneratedExtension = ".zip"
        $WixUiAvailable = $Wix -and (Test-WixUiExtension $Wix)
        if ($WixUiAvailable -and $ConfiguratorAvailable) {
            Write-Log "WiX v4 found; generating MSI"
            & $CPack.Source -G WIX -C RelWithDebInfo
            if ($LASTEXITCODE -ne 0) {
                Write-Log "MSI generation failed; falling back to ZIP"
                & $CPack.Source -G ZIP -C RelWithDebInfo
            } else {
                $GeneratedExtension = ".msi"
            }
        } elseif (-not $ConfiguratorAvailable) {
            Write-Log "Configurator is unavailable; generating ZIP instead of an incomplete MSI"
            & $CPack.Source -G ZIP -C RelWithDebInfo
        } elseif ($Wix -and -not $WixUiAvailable) {
            $WixVersionText = (& $Wix.Source --version | Out-String).Trim()
            $WixVersion = if ($WixVersionText -match "^(\d+\.\d+\.\d+)") {
                $Matches[1]
            } else {
                "<same-version-as-wix>"
            }
            Write-Log "WiX UI extension not found; generating ZIP"
            Write-Log "  To enable MSI: wix extension add --global WixToolset.UI.wixext/$WixVersion"
            & $CPack.Source -G ZIP -C RelWithDebInfo
        } else {
            Write-Log "WiX v4 not found; generating ZIP (install with: dotnet tool install --global wix)"
            & $CPack.Source -G ZIP -C RelWithDebInfo
        }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    finally {
        Pop-Location
    }

    $Packages = @(Get-ChildItem -Path $Directory -Filter "seekdb-*$GeneratedExtension" `
        -File -ErrorAction SilentlyContinue)
    $MsiFiles = @($Packages | Where-Object Extension -eq ".msi" |
        ForEach-Object FullName)
    Invoke-CodeSign $MsiFiles

    if (-not $Packages) { throw "CPack completed without producing an MSI or ZIP" }
    Write-Log "package(s) created:"
    foreach ($Package in $Packages) { Write-Log "  $($Package.FullName)" }
}

if ($Action.ToLower() -eq "sqlite-identity") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-identity does not accept build options" }
    & python "$TOPDIR\tools\windows\long_path_phase0\sqlite_identity.py" --dll "$DEPS_3RD\vcpkg\x64-windows\bin\sqlite3.dll" "$TOPDIR\build_phase0\sqlite-control\install\bin\sqlite3.dll" "$TOPDIR\build_phase0\sqlite-candidate\install\bin\sqlite3.dll"
    exit $LASTEXITCODE
}

if ($Action.ToLower() -in @("sqlite-process-test", "sqlite-process-long-test", "sqlite-process-long-interop-test")) {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-process-test does not accept build options" }
    $Evidence = "$TOPDIR\build_phase0\sqlite-process-results-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $Evidence | Out-Null
    Write-Output "PROCESS_EVIDENCE=$Evidence"
    $Failed = $false
    $Variant = 0
    $ProcessArgs = @()
    if ($Action.ToLower() -ne "sqlite-process-test") { $ProcessArgs += @("--path-units", "4092") }
    if ($Action.ToLower() -eq "sqlite-process-long-interop-test") { $ProcessArgs += "--child-ordinary" }
    foreach ($Dll in @("$DEPS_3RD\vcpkg\x64-windows\bin\sqlite3.dll", "$TOPDIR\build_phase0\sqlite-control\install\bin\sqlite3.dll", "$TOPDIR\build_phase0\sqlite-candidate\install\bin\sqlite3.dll")) {
        $Variant += 1
        foreach ($Round in 1..3) {
            $CaseLog = "$Evidence\dll-$Variant-round-$Round"
            $SavedPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & python "$TOPDIR\tools\windows\long_path_phase0\sqlite_process_lock.py" --source-root $TOPDIR --dll $Dll @ProcessArgs 2>&1 | Out-File "$CaseLog.log" -Encoding utf8
            $CaseExit = $LASTEXITCODE
            $ErrorActionPreference = $SavedPreference
            Set-Content "$CaseLog.exit" $CaseExit
            Get-Content "$CaseLog.log"
            if ($CaseExit -ne 0) { $Failed = $true }
        }
    }
    Set-Content "$Evidence\matrix.exit" ([int][bool]$Failed)
    exit ([int][bool]$Failed)
}

# Fixed diagnostic matrix: three fresh databases per representation and DLL.
if ($Action.ToLower() -in @("sqlite-control-test", "sqlite-control-serial-test", "sqlite-candidate-test", "sqlite-candidate-timeout-test", "sqlite-all-timeout-test", "sqlite-long-timeout-test")) {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-control-test does not accept build options" }
    $Failed = $false
    $Evidence = "$TOPDIR\build_phase0\sqlite-controls-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $Evidence | Out-Null
    Write-Output "CONTROL_EVIDENCE=$Evidence"
    $SerialArgs = @()
    if ($Action.ToLower() -eq "sqlite-control-serial-test") { $SerialArgs = @("--serial") }
    if ($Action.ToLower() -in @("sqlite-candidate-timeout-test", "sqlite-all-timeout-test", "sqlite-long-timeout-test")) { $SerialArgs = @("--timeout-before-config") }
    $Variant = 0
    $Dlls = @("$DEPS_3RD\vcpkg\x64-windows\bin\sqlite3.dll", "$TOPDIR\build_phase0\sqlite-control\install\bin\sqlite3.dll")
    if ($Action.ToLower() -in @("sqlite-candidate-test", "sqlite-all-timeout-test", "sqlite-long-timeout-test")) { $Dlls += "$TOPDIR\build_phase0\sqlite-candidate\install\bin\sqlite3.dll" }
    if ($Action.ToLower() -eq "sqlite-candidate-timeout-test") { $Dlls = @("$TOPDIR\build_phase0\sqlite-candidate\install\bin\sqlite3.dll") }
    $Cases = @("short", "unicode-short")
    $RepresentationArgs = @("--mixed")
    if ($Action.ToLower() -eq "sqlite-long-timeout-test") {
        $Cases = @("long-extended", "unicode-extended")
        $RepresentationArgs = @()
    }
    $Policy = Get-ItemPropertyValue -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled
    Set-Content "$Evidence\policy.txt" $Policy
    Write-Output "LongPathsEnabled=$Policy"
    foreach ($Dll in $Dlls) {
        $Variant += 1
        foreach ($Round in 1..3) {
            foreach ($Case in $Cases) {
                Write-Output "CONTROL_ROUND=$Round CASE=$Case DLL=$Dll"
                $CaseLog = "$Evidence\dll-$Variant-round-$Round-$Case"
                $SavedPreference = $ErrorActionPreference
                $ErrorActionPreference = "Continue"
                & python "$TOPDIR\tools\windows\long_path_phase0\sqlite_wal_concurrency.py" --source-root $TOPDIR --dll $Dll --case $Case @RepresentationArgs @SerialArgs 2>&1 | Out-File "$CaseLog.log" -Encoding utf8
                $CaseExit = $LASTEXITCODE
                $ErrorActionPreference = $SavedPreference
                Set-Content "$CaseLog.exit" $CaseExit
                Get-Content "$CaseLog.log"
                if ($CaseExit -ne 0) { $Failed = $true }
            }
        }
    }
    Set-Content "$Evidence\matrix.exit" ([int][bool]$Failed)
    if ($Failed) { exit 1 }
    exit 0
}

if ($Action.ToLower() -eq "sqlite-install-test") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-install-test does not accept build options" }
    & "$TOPDIR\tools\windows\long_path_phase0\sqlite_install_test.ps1" -SourceRoot $TOPDIR -CMake "$TOOLS_DIR\cmake\bin\cmake.exe"
    exit 0
}

# Exercise runtime bundling in a fresh task directory without rebuilding.
if ($Action.ToLower() -eq "sqlite-bundle-test") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-bundle-test does not accept build options" }
    & "$TOPDIR\tools\windows\long_path_phase0\sqlite_bundle_test.ps1" -SourceRoot $TOPDIR -CMake "$TOOLS_DIR\cmake\bin\cmake.exe"
    exit 0
}

# Validate the installation without configuring or rebuilding the product.
if ($Action.ToLower() -eq "sqlite-input-test") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-input-test does not accept build options" }
    & "$TOPDIR\tools\windows\long_path_phase0\sqlite_input_test.ps1" -SourceRoot $TOPDIR
    exit 0
}

if ($Action.ToLower() -eq "sqlite-install-check") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) { throw "sqlite-install-check does not accept build options" }
    & "$TOOLS_DIR\cmake\bin\cmake.exe" "-DCMAKE_SOURCE_DIR=$TOPDIR" -P "$TOPDIR\cmake\WindowsSQLite.cmake"
    exit $LASTEXITCODE
}

# SQLite controls use existing tools and their own install prefix.
if ($Action.ToLower() -in @("sqlite-control", "sqlite-candidate")) {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) { throw "sqlite-control does not accept product build options" }
    if (!$env:SEEKDB_SQLITE_PATCH_GIT) { throw "Set SEEKDB_SQLITE_PATCH_GIT to an existing git.exe for original patches" }
    $PackageRoot = if ($env:OB_VCPKG_DIR) { $env:OB_VCPKG_DIR } else { "$DEPS_3RD\vcpkg\x64-windows" }
    $ControlJobs = if ($Jobs -gt 0) { $Jobs } else { 2 }
    & "$TOPDIR\deps\init\sqlite\build-control.ps1" -TaskDirectory "$TOPDIR\build_phase0" -PackageRoot $PackageRoot -ToolsDirectory $TOOLS_DIR -Git $env:SEEKDB_SQLITE_PATCH_GIT -Jobs $ControlJobs -Candidate:($Action.ToLower() -eq "sqlite-candidate")
    exit 0
}

# Metadata recovery does not build or replace the installed SQLite package.
if ($Action.ToLower() -eq "sqlite-recover-inputs") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0 -or $Jobs -ne 0) {
        throw "sqlite-recover-inputs does not accept build options"
    }
    $PackageRoot = if ($env:OB_VCPKG_DIR) { $env:OB_VCPKG_DIR } else { "$DEPS_3RD\vcpkg\x64-windows" }
    & "$TOPDIR\deps\init\sqlite\recover-inputs.ps1" -PackageRoot $PackageRoot `
        -CacheDirectory "$TOPDIR\build_phase0\sqlite-inputs" `
        -PortCommit '1f6bbba3da511773189e5075b6781222402d10fa'
    exit 0
}

# Build and run only the production SQLite pool regression in an existing graph.
if ($Action.ToLower() -in @("native-sqlite-pool", "native-log-lifecycle")) {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "$Action reuses native-startup configuration"
    }
    $CheckTarget = if ($Action.ToLower() -eq "native-sqlite-pool") { 'windows_sqlite_pool_test' } else { 'windows_log_file_test' }
    $CheckOption = if ($Action.ToLower() -eq "native-sqlite-pool") { 'OB_BUILD_WINDOWS_SQLITE_POOL' } else { 'OB_BUILD_WINDOWS_LOG_LIFECYCLE' }
    $CheckDirectory = "$TOPDIR\build_phase0_nio"
    $CheckCache = Get-Content -LiteralPath "$CheckDirectory\CMakeCache.txt"
    if (!($CheckCache -match "^${CheckOption}:BOOL=ON$")) {
        throw "Configure native-startup with $CheckOption=ON first"
    }
    Add-DependencyToolsToPath
    $CheckCMake = Get-Command cmake -ErrorAction Stop
    $CheckJobs = if ($Jobs -gt 0) { $Jobs } else { 2 }
    & $CheckCMake.Source --build $CheckDirectory --target $CheckTarget --parallel $CheckJobs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $CheckVendor = @($CheckCache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
    if ($CheckVendor.Count -ne 1) { throw "Cannot identify test runtime dependency root" }
    $CheckRuntime = $CheckVendor[0].Substring($CheckVendor[0].IndexOf('=') + 1)
    $CheckSavedPath = $env:PATH
    try {
        $env:PATH = "$CheckRuntime\bin;$CheckSavedPath"
        & "$CheckDirectory\$CheckTarget.exe"
        $CheckExit = $LASTEXITCODE
    } finally { $env:PATH = $CheckSavedPath }
    exit $CheckExit
}

if ($Action.ToLower() -eq "native-install") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-install reuses the existing native product configuration"
    }
    Add-DependencyToolsToPath
    $InstallCMake = Get-Command cmake -ErrorAction Stop
    $InstallJobs = if ($Jobs -gt 0) { $Jobs } else { 2 }
    & "$TOPDIR\tools\windows\long_path_phase0\native_install.ps1" -SourceRoot $TOPDIR -CMake $InstallCMake.Source -Jobs $InstallJobs
    exit $LASTEXITCODE
}

if ($Action.ToLower() -eq "native-startup-contract") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-startup-contract validates the existing startup test binary"
    }
    $StartupRoot = "C:\s\seek533-startup-$PID-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $StartupRoot -ErrorAction Stop | Out-Null
    & "$TOPDIR\build_phase0_nio\windows_startup_test.exe" $StartupRoot
    exit $LASTEXITCODE
}

# Reuse the configured native validation graph for source-only rebuilds.
# CMake/Ninja still regenerate automatically when configuration inputs change.
if ($Action.ToLower() -eq "native-rebuild") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-rebuild reuses existing configuration; configure with native-startup first"
    }
    $NativeDirectory = "$TOPDIR\build_phase0_nio"
    if (!(Test-Path "$NativeDirectory\CMakeCache.txt")) {
        throw "Configure with native-startup first"
    }
    Add-DependencyToolsToPath
    $NativeCMake = Get-Command cmake -ErrorAction Stop
    $NativeJobs = if ($Jobs -gt 0) { $Jobs } else { 1 }
    & $NativeCMake.Source --build $NativeDirectory --target seekdb --parallel $NativeJobs
    exit $LASTEXITCODE
}

if ($Action.ToLower() -eq "native-slog-reader") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-slog-reader validates the existing test binary and accepts no build options"
    }
    $ReaderTest = "$TOPDIR\build_phase0_nio\windows_slog_reader_test.exe"
    if (!(Test-Path -LiteralPath $ReaderTest)) { throw "Build with OB_BUILD_WINDOWS_SLOG_READER=ON first" }
    $PreviousPath = $env:PATH
    try {
        $env:PATH = "$TOPDIR\build_phase0_nio\src\observer;$PreviousPath"
        & $ReaderTest
        if ($LASTEXITCODE -ne 0) { throw "Native slog reader validation failed: $LASTEXITCODE" }
    } finally {
        $env:PATH = $PreviousPath
    }
    exit 0
}

if ($Action.ToLower() -eq "native-sqlite-concurrency") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-sqlite-concurrency accepts no build options"
    }
    $ConcurrencyArgs = @()
    if ($env:SEEKDB_SQLITE_CONCURRENCY_SERIAL -eq "1") { $ConcurrencyArgs += "--serial" }
    if ($env:SEEKDB_SQLITE_CONCURRENCY_CASE) { $ConcurrencyArgs += @("--case", $env:SEEKDB_SQLITE_CONCURRENCY_CASE) }
    & python.exe "$TOPDIR\tools\windows\long_path_phase0\sqlite_wal_concurrency.py" --source-root $TOPDIR @ConcurrencyArgs
    exit $LASTEXITCODE
}

if ($Action.ToLower() -in @("native-cli-sql", "native-cli-debug", "native-cli-reader")) {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-cli-sql validates the existing native product and accepts no build options"
    }
    $DebugArgs = @()
    if ($env:SEEKDB_NATIVE_SQL_EXE) { $DebugArgs += @("--exe", $env:SEEKDB_NATIVE_SQL_EXE) }
    if ($Action.ToLower() -eq "native-cli-debug") { $DebugArgs += "--debug" }
    if ($Action.ToLower() -eq "native-cli-reader") { $DebugArgs += "--reader-self-test" }
    & python.exe "$TOPDIR\tools\windows\long_path_phase0\native_cli_sql.py" --source-root $TOPDIR @DebugArgs
    if ($LASTEXITCODE -ne 0) { throw "Native CLI SQL validation failed: $LASTEXITCODE" }
    exit 0
}

if ($Action.ToLower() -eq "native-cli-preflight") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-cli-preflight validates the existing product and accepts no build options"
    }
    & python.exe "$TOPDIR\tools\windows\long_path_phase0\native_cli_preflight.py" --source-root $TOPDIR
    if ($LASTEXITCODE -ne 0) { throw "Native CLI preflight validation failed: $LASTEXITCODE" }
    exit 0
}

if ($Action.ToLower() -eq "native-sql-tls") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-sql-tls validates the existing product and accepts no build options"
    }
    & python.exe "$TOPDIR\tools\windows\long_path_phase0\native_sql_tls.py" --source-root $TOPDIR
    if ($LASTEXITCODE -ne 0) { throw "Native SQL TLS validation failed: $LASTEXITCODE" }
    exit 0
}

if ($Action.ToLower() -eq "native-service") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-service validates the existing product and accepts no build options"
    }
    & python.exe "$TOPDIR\tools\windows\long_path_phase0\native_service.py" --source-root $TOPDIR
    if ($LASTEXITCODE -ne 0) { throw "Native service validation failed: $LASTEXITCODE" }
    exit 0
}

if ($Action.ToLower() -eq "native-product-identity") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-product-identity inspects the existing product and accepts no build options"
    }
    $IdentityArgs = @()
    if ($env:SEEKDB_NATIVE_IDENTITY_INSTANCE_RESULT) {
        $IdentityArgs += @("--instance-result", $env:SEEKDB_NATIVE_IDENTITY_INSTANCE_RESULT)
    }
    & python.exe "$TOPDIR\tools\windows\long_path_phase0\product_identity.py" --source-root $TOPDIR @IdentityArgs
    if ($LASTEXITCODE -ne 0) { throw "Native product identity validation failed: $LASTEXITCODE" }
    exit 0
}

if ($Action.ToLower() -eq "native-cli-smoke") {
    if ($Build -or $Init -or $Jobs -ne 0 -or $ExtraCMakeArgs.Count -gt 0) {
        throw "native-cli-smoke validates the existing native product and accepts no build options"
    }
    & "$TOPDIR\tools\windows\long_path_phase0\run-native-cli-smoke.ps1" -SourceRoot $TOPDIR
    exit 0
}

if ($Action.ToLower() -eq "init") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "init does not accept build options"
    }
    Do-Init
    exit 0
}
if ($Action.ToLower() -eq "clean") {
    if ($Build -or $Init -or $ExtraCMakeArgs.Count -gt 0) {
        throw "clean does not accept build options"
    }
    Do-Clean
    exit 0
}
if ($Action.ToLower() -notin "release", "relwithdebinfo", "package", "phase0", "phase0-context", "phase0-nio", "phase0-nio-abi", "phase0-sqlite-temp", "native-path", "native-startup") {
    Write-Err "Unsupported action: $Action"
    Show-Usage
    exit 2
}

$ContextBuild = $Action.ToLower() -eq "phase0-context"
$NioAbiBuild = $Action.ToLower() -eq "phase0-nio-abi"
$NativeStartupBuild = $Action.ToLower() -eq "native-startup"
$NativePathBuild = $Action.ToLower() -in "native-path", "native-startup"
$SqliteTempBuild = $Action.ToLower() -eq "phase0-sqlite-temp"
$NioBuild = $Action.ToLower() -in "phase0-nio", "phase0-nio-abi"
$Phase0Build = $Action.ToLower() -in "phase0", "phase0-context", "phase0-nio", "phase0-nio-abi", "phase0-sqlite-temp", "native-path", "native-startup"
if ($Phase0Build -and $Init) {
    throw "phase0 requires existing product dependencies; --init is not supported"
}
if ($Init) { Do-Init }
Add-DependencyToolsToPath

$CMake = Get-Command cmake -ErrorAction SilentlyContinue
$Ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $CMake) { throw "cmake not found; run with --init or install CMake 3.20+" }
if (-not $Ninja) { throw "ninja not found; run with --init or install Ninja" }

$DefaultVcpkgDir = if ($env:OB_VCPKG_DIR) { $env:OB_VCPKG_DIR } else { "$DEPS_3RD\vcpkg\x64-windows" }
$DefaultOpenSSLDir = if ($env:OB_OPENSSL_DIR) { $env:OB_OPENSSL_DIR } else { "$DEPS_3RD\openssl" }
$DefaultLLVMDir = if ($env:OB_LLVM_DIR) { $env:OB_LLVM_DIR } else { "$TOOLS_DIR\llvm18" }
$CurrentPowerShell = (Get-Process -Id $PID).Path
$BuildDir = if ($NioBuild -or $SqliteTempBuild -or $NativePathBuild) { "$TOPDIR\build_phase0_nio" } elseif ($ContextBuild) { "$TOPDIR\build_phase0_context" } elseif ($Phase0Build) { "$TOPDIR\build_phase0" } else { "$TOPDIR\build_release" }
$PackageBuild = $Action.ToLower() -eq "package"
$PackageCMakeArgs = if ($PackageBuild) {
    @(
        "-DOB_BUILD_PACKAGE=ON",
        "-DOB_BUILD_RPM=OFF",
        "-DOB_BUILD_DEB=OFF",
        "-DOB_BUILD_TGZ=OFF",
        "-DOB_BUILD_WIX=ON"
    )
} else {
    @(
        "-DOB_BUILD_PACKAGE=OFF",
        "-DOB_BUILD_RPM=OFF",
        "-DOB_BUILD_DEB=OFF",
        "-DOB_BUILD_TGZ=OFF",
        "-DOB_BUILD_WIX=OFF"
    )
}
$CMakeArgs = @(
    "-S", $TOPDIR,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DOB_ENABLE_UNITY=ON",
    "-DOB_USE_LLD=ON",
    "-DOB_VCPKG_DIR=$DefaultVcpkgDir",
    "-DOB_OPENSSL_DIR=$DefaultOpenSSLDir",
    "-DOB_LLVM_DIR=$DefaultLLVMDir",
    "-DPWSH_EXE=$CurrentPowerShell"
) + $ExtraCMakeArgs + $PackageCMakeArgs

if ($Phase0Build) {
    $CMakeArgs += "-DOB_BUILD_WINDOWS_PATH_PHASE0=ON"
    $CMakeArgs += "-DOB_BUILD_WINDOWS_NIO_ABI_MIX=$(if ($NioAbiBuild) { 'ON' } else { 'OFF' })"
    Write-Log "configuring the product project for Windows Phase 0 in $BuildDir"
    & $CMake.Source @CMakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $BuildJobs = Get-BuildJobs
    $Phase0Target = if ($NativeStartupBuild) { 'windows_native_startup' } elseif ($NativePathBuild) { 'windows_native_path' } elseif ($SqliteTempBuild) { 'windows_sqlite_temp_phase0' } elseif ($NioAbiBuild) { 'windows_nio_abi_phase0' } elseif ($NioBuild) { 'windows_nio_path_phase0' } elseif ($ContextBuild) { 'windows_path_context_phase0' } else { 'windows_path_phase0' }
    if ($NioBuild) { $env:CARGO_BUILD_JOBS = [string]$BuildJobs }
    & $Ninja.Source -C $BuildDir -j $BuildJobs $Phase0Target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & "$BuildDir\path_fixture_test.exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($NativePathBuild -and -not $NativeStartupBuild) {
        $NativePathRoot = "C:\s\seek533-native-path-$PID-$([DateTime]::UtcNow.Ticks)"
        Write-Host "NATIVE_PATH_ROOT=$NativePathRoot"
        New-Item -ItemType Directory -Path $NativePathRoot -ErrorAction Stop | Out-Null
        & "$BuildDir\windows_file_path_test.exe" $NativePathRoot
        $NativePathExit = $LASTEXITCODE
        if ($NativePathExit -ne 0) {
            & "$BuildDir\windows_file_path_test.exe" $NativePathRoot --cleanup
            Write-Host "NATIVE_PATH_CLEANUP_EXIT=$LASTEXITCODE"
            exit $NativePathExit
        }
    }
    if ($NioAbiBuild) {
        & "$TOPDIR\tools\windows\long_path_phase0\check-nio-abi-mix.ps1" -BuildDirectory $BuildDir -Ninja $Ninja.Source -Jobs $BuildJobs
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $Phase0Cache = Get-Content -LiteralPath "$BuildDir\CMakeCache.txt"
    if ($NativeStartupBuild -and ($Phase0Cache -match '^OB_BUILD_WINDOWS_LOG_LIFECYCLE:BOOL=ON$')) {
        $VendorCache = @($Phase0Cache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
        if ($VendorCache.Count -ne 1) { throw "Cannot identify log test runtime dependency root" }
        $RuntimeVendor = $VendorCache[0].Substring($VendorCache[0].IndexOf('=') + 1)
        $LogTestSavedPath = $env:PATH
        try {
            $env:PATH = "$RuntimeVendor\bin;$env:PATH"
            & "$BuildDir\windows_log_file_test.exe"
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } finally { $env:PATH = $LogTestSavedPath }
    }
    if ($NativeStartupBuild -and ($Phase0Cache -match '^OB_BUILD_WINDOWS_SQLITE_POOL:BOOL=ON$')) {
        $VendorCache = @($Phase0Cache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
        if ($VendorCache.Count -ne 1) { throw "Cannot identify log test runtime dependency root" }
        $RuntimeVendor = $VendorCache[0].Substring($VendorCache[0].IndexOf('=') + 1)
        $LogTestSavedPath = $env:PATH
        try {
            $env:PATH = "$RuntimeVendor\bin;$env:PATH"
            & "$BuildDir\windows_sqlite_pool_test.exe"
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } finally { $env:PATH = $LogTestSavedPath }
    }
    if ($NativeStartupBuild -and ($Phase0Cache -match '^OB_BUILD_WINDOWS_DATA_VERSION:BOOL=ON$')) {
        $VendorCache = @($Phase0Cache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
        if ($VendorCache.Count -ne 1) { throw "Cannot identify log test runtime dependency root" }
        $RuntimeVendor = $VendorCache[0].Substring($VendorCache[0].IndexOf('=') + 1)
        $LogTestSavedPath = $env:PATH
        try {
            $env:PATH = "$RuntimeVendor\bin;$env:PATH"
            & "$BuildDir\windows_data_version_test.exe"
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } finally { $env:PATH = $LogTestSavedPath }
    }
    if ($NativeStartupBuild -and ($Phase0Cache -match '^OB_BUILD_WINDOWS_ROUTER:BOOL=ON$')) {
        $VendorCache = @($Phase0Cache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
        if ($VendorCache.Count -ne 1) { throw "Cannot identify log test runtime dependency root" }
        $RuntimeVendor = $VendorCache[0].Substring($VendorCache[0].IndexOf('=') + 1)
        $LogTestSavedPath = $env:PATH
        try {
            $env:PATH = "$RuntimeVendor\bin;$env:PATH"
            foreach ($Units in @(100,280,2048)) {
                & "$BuildDir\windows_router_test.exe" $Units
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            }
        } finally { $env:PATH = $LogTestSavedPath }
    }
    if ($NativeStartupBuild -and ($Phase0Cache -match '^OB_BUILD_WINDOWS_BLOCK_FILE:BOOL=ON$')) {
        $VendorCache = @($Phase0Cache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
        if ($VendorCache.Count -ne 1) { throw "Cannot identify log test runtime dependency root" }
        $RuntimeVendor = $VendorCache[0].Substring($VendorCache[0].IndexOf('=') + 1)
        $LogTestSavedPath = $env:PATH
        try {
            $env:PATH = "$RuntimeVendor\bin;$env:PATH"
            foreach ($Units in @(100,280,2048,4077)) {
                $BlockTest = Start-Process -FilePath "$BuildDir\windows_block_file_test.exe" `
                    -ArgumentList @("$Units") -NoNewWindow -PassThru
                try {
                    # Retain the process handle before waiting so Windows PowerShell
                    # can retrieve the exit code after a short-lived child exits.
                    $BlockTestHandle = $BlockTest.Handle
                    if (-not $BlockTest.WaitForExit(180000)) {
                        $BlockTest.Kill()
                        $BlockTest.WaitForExit()
                        throw "Block file test timed out after 180 seconds (path units: $Units)"
                    }
                    $BlockTest.WaitForExit()
                    $BlockTestExit = $BlockTest.ExitCode
                    if ($null -eq $BlockTestExit) { throw "Block file test exit code unavailable (path units: $Units)" }
                    Write-Host "BLOCK_TEST_EXIT units=$Units code=$BlockTestExit"
                    if ($BlockTestExit -ne 0) { exit $BlockTestExit }
                } finally { $BlockTest.Dispose() }
            }
        } finally { $env:PATH = $LogTestSavedPath }
    }
    if ($NativeStartupBuild -and ($Phase0Cache -match '^OB_BUILD_WINDOWS_TELEMETRY:BOOL=ON$')) {
        $VendorCache = @($Phase0Cache | Where-Object { $_ -match '^OB_VCPKG_DIR:[^=]+=' })
        if ($VendorCache.Count -ne 1) { throw "Cannot identify log test runtime dependency root" }
        $RuntimeVendor = $VendorCache[0].Substring($VendorCache[0].IndexOf('=') + 1)
        $LogTestSavedPath = $env:PATH
        try {
            $env:PATH = "$RuntimeVendor\bin;$env:PATH"
            & "$BuildDir\windows_telemetry_test.exe"
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } finally { $env:PATH = $LogTestSavedPath }
    }
    Write-Log "Phase 0 build and fixture completed; Windows path matrix remains to be run"
    exit 0
}

if ($PackageBuild) {
    $ConfiguratorBuilt = Invoke-ConfiguratorBuild
    if (-not $ConfiguratorBuilt) {
        Write-Log "continuing without the Configurator executable"
    }
    Do-Clean
    exit 0
}
if ($Action.ToLower() -notin "release", "relwithdebinfo") {
    Write-Err "Unsupported build type: $Action (only release is maintained)"
    Show-Usage
    exit 2
}

if ($Init) { Do-Init }
Add-DependencyToolsToPath

$CMake = Get-Command cmake -ErrorAction SilentlyContinue
$Ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $CMake) { throw "cmake not found; run with --init or install CMake 3.20+" }
if (-not $Ninja) { throw "ninja not found; run with --init or install Ninja" }

$DefaultVcpkgDir = if ($env:OB_VCPKG_DIR) { $env:OB_VCPKG_DIR } else { "$DEPS_3RD\vcpkg\x64-windows" }
$DefaultOpenSSLDir = if ($env:OB_OPENSSL_DIR) { $env:OB_OPENSSL_DIR } else { "$DEPS_3RD\openssl" }
$DefaultLLVMDir = if ($env:OB_LLVM_DIR) { $env:OB_LLVM_DIR } else { "$TOOLS_DIR\llvm18" }
$BuildDir = "$TOPDIR\build_release"
$CMakeArgs = @(
    "-S", $TOPDIR,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DOB_ENABLE_UNITY=ON",
    "-DOB_USE_LLD=ON",
    "-DOB_VCPKG_DIR=$DefaultVcpkgDir",
    "-DOB_OPENSSL_DIR=$DefaultOpenSSLDir",
    "-DOB_LLVM_DIR=$DefaultLLVMDir"
) + $ExtraCMakeArgs

Write-Log "configuring Windows x64 release in $BuildDir"
& $CMake.Source @CMakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Build) {
    if ($Jobs -le 0) {
        $Jobs = (Get-CimInstance Win32_Processor |
            Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
        if (-not $Jobs -or $Jobs -lt 1) { $Jobs = 4 }
    }
    Write-Log "building seekdb with Ninja (-j $Jobs)"
    & $Ninja.Source -C $BuildDir -j $Jobs seekdb
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Log "configuring Windows x64 release in $BuildDir"
& $CMake.Source @CMakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($PackageBuild) {
    Invoke-PackageBuild -NinjaCommand $Ninja -Directory $BuildDir `
        -ConfiguratorAvailable $ConfiguratorBuilt
} elseif ($Build) {
    Invoke-SeekdbBuild -NinjaCommand $Ninja -Directory $BuildDir
}
