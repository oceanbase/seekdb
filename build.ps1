<#
.SYNOPSIS
    Configure or build the seekdb CMake compatibility release on Windows x64.

.EXAMPLE
    .\build.ps1 release --init --ninja -j 16
    .\build.ps1 package --init -j 16
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
    .\build.ps1 clean
    .\build.ps1 release [--init] [-DName=Value ...]
    .\build.ps1 release [--init] [-DName=Value ...] --ninja [-j N]
    .\build.ps1 package [--init] [-DName=Value ...] [-j N]

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
if ($Action.ToLower() -notin "release", "relwithdebinfo", "package") {
    Write-Err "Unsupported action: $Action"
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
$CurrentPowerShell = (Get-Process -Id $PID).Path
$BuildDir = "$TOPDIR\build_release"
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

if ($PackageBuild) {
    $ConfiguratorBuilt = Invoke-ConfiguratorBuild
    if (-not $ConfiguratorBuilt) {
        Write-Log "continuing without the Configurator executable"
    }
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
