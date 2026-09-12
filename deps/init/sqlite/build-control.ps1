# Rebuild the original SQLite recipe in a separate task prefix.
param(
    [Parameter(Mandatory=$true)][string]$TaskDirectory,
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$ToolsDirectory,
    [Parameter(Mandatory=$true)][string]$Git,
    [int]$Jobs=2,
    [switch]$Candidate
)
$ErrorActionPreference='Stop'
$Commit='1f6bbba3da511773189e5075b6781222402d10fa'
& "$PSScriptRoot\recover-inputs.ps1" -PackageRoot $PackageRoot -CacheDirectory "$TaskDirectory\sqlite-inputs" -PortCommit $Commit
$Port="$TaskDirectory\sqlite-inputs\$Commit"
$Root=if ($Candidate) { "$TaskDirectory\sqlite-candidate" } else { "$TaskDirectory\sqlite-control" }
$CandidatePatch="$PSScriptRoot\local-extended-drive.patch"
$Source="$Root\sqlite-autoconf-3510200"
$Build="$Root\build"
$Install="$Root\install"
$CMake="$ToolsDirectory\cmake\bin\cmake.exe"
$Ninja="$ToolsDirectory\ninja\ninja.exe"
foreach($Tool in @($CMake,$Ninja,$Git)) {
    if (!(Test-Path -LiteralPath $Tool -PathType Leaf)) {throw "Missing existing tool: $Tool"}
}
# Never apply the original patches twice or overwrite an interrupted preparation.
if (!(Test-Path "$Root\prepared.json")) {
    if (Test-Path $Root) {throw "Incomplete source preparation retained at $Root"}
    New-Item -ItemType Directory -Path $Root | Out-Null
    & $CMake -E chdir $Root $CMake -E tar xzf "$TaskDirectory\sqlite-inputs\sqlite-autoconf-3510200.tar.gz"
    if ($LASTEXITCODE -ne 0) {throw 'Archive extraction failed'}
    Push-Location $Source
    try {
        foreach($Patch in @('fix-arm-uwp.patch','add-config-include.patch')) {
            & $Git apply --no-index -- "$Port\$Patch"
            if ($LASTEXITCODE -ne 0) {throw "Original patch failed: $Patch"}
        }
        if ($Candidate) {
            & $Git apply --no-index -- $CandidatePatch
            if ($LASTEXITCODE -ne 0) {throw 'Local extended drive candidate patch failed'}
        }
    } finally {Pop-Location}
    Copy-Item "$Port\CMakeLists.txt","$Port\sqlite3.pc.in" -Destination $Source
    # The installed header records the original core/json1 feature selections.
    Copy-Item "$PackageRoot\include\sqlite3-vcpkg-config.h" -Destination $Source
    if ($Candidate) {
        (Get-FileHash $CandidatePatch).Hash | Set-Content "$Root\candidate-patch.sha256"
    }
    $Files=@('sqlite3.c','sqlite3.h','shell.c','CMakeLists.txt','sqlite3.pc.in','sqlite3-vcpkg-config.h')
    @(foreach($File in $Files){[ordered]@{name=$File;sha256=(Get-FileHash "$Source\$File").Hash}}) |
        ConvertTo-Json | Set-Content "$Root\prepared.json" -Encoding UTF8
}
if ($Candidate -and ((Get-Content "$Root\candidate-patch.sha256" -Raw).Trim() -ne (Get-FileHash $CandidatePatch).Hash)) {
    throw 'Candidate patch changed; preserve this build and use a fresh task directory'
}
foreach($File in (Get-Content "$Root\prepared.json" -Raw | ConvertFrom-Json)) {
    if ((Get-FileHash "$Source\$($File.name)").Hash -ne $File.sha256) {throw "Prepared input changed: $($File.name)"}
}
# Reusing a prepared tree must also match the currently verified recipe.
foreach($RecipeFile in @('CMakeLists.txt','sqlite3.pc.in')) {
    if ((Get-FileHash "$Source\$RecipeFile").Hash -ne (Get-FileHash "$Port\$RecipeFile").Hash) {
        throw "Prepared recipe differs from verified port: $RecipeFile"
    }
}
if ((Get-FileHash "$Source\sqlite3-vcpkg-config.h").Hash -ne
    (Get-FileHash "$PackageRoot\include\sqlite3-vcpkg-config.h").Hash) {
    throw 'Installed feature configuration changed; use a fresh task directory'
}
$VsRoot=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools'
Import-Module "$VsRoot\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44.35207 -winsdk=10.0.26100.0' | Out-Null
$Compiler=(Get-Command cl.exe -ErrorAction Stop).Source
New-Item -ItemType Directory -Force -Path $Build | Out-Null
$Configure=@('-S',$Source,'-B',$Build,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_C_COMPILER=$Compiler", "-DCMAKE_CXX_COMPILER=$Compiler",'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_POLICY_DEFAULT_CMP0091=NEW','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL','-DBUILD_SHARED_LIBS=ON','-DSQLITE3_SKIP_TOOLS=ON','-DPKGCONFIG_VERSION=3.51.2',"-DCMAKE_INSTALL_PREFIX=$Install",'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
[ordered]@{arguments=$Configure;compiler=(Get-Item $Compiler).VersionInfo.FileVersion; sdk=$env:WindowsSDKVersion;toolset=$env:VCToolsVersion} | ConvertTo-Json -Depth 5 | Set-Content "$Build\inputs.json" -Encoding UTF8
foreach($Stage in @('configure','build','install')) {
    $Arguments=switch($Stage){'configure' {$Configure}; 'build' {@('--build',$Build,'--parallel',"$Jobs")}; 'install' {@('--install',$Build)}}
    $ErrorActionPreference='Continue'
    & $CMake @Arguments 2>&1 | Out-File "$Build\$Stage.log" -Encoding UTF8
    $Code=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    Set-Content "$Build\$Stage.exit" $Code
    Get-Content "$Build\$Stage.log" -Tail 15
    if ($Code -ne 0) {throw "SQLite $Stage failed: $Code"}
}
# Record a relocatable complete installation identity only after all stages succeed.
New-Item -ItemType Directory -Force -Path "$Install\share\sqlite3" | Out-Null
Copy-Item -LiteralPath "$PackageRoot\share\sqlite3\copyright" -Destination "$Install\share\sqlite3\copyright"
$InstalledFiles=@('include/sqlite3.h','include/sqlite3ext.h','include/sqlite3-vcpkg-config.h','lib/sqlite3.lib','bin/sqlite3.dll','share/sqlite3/copyright')
$Installation=[ordered]@{
    version='3.51.2'
    port_commit=$Commit
    candidate=[bool]$Candidate
    candidate_patch_sha256=$(if ($Candidate) { (Get-FileHash $CandidatePatch).Hash.ToLowerInvariant() } else { $null })
    source_sha256=(Get-FileHash "$Source\sqlite3.c").Hash.ToLowerInvariant()
    files=@(foreach($File in $InstalledFiles) {
        [ordered]@{name=$File;sha256=(Get-FileHash "$Install\$File" -ErrorAction Stop).Hash.ToLowerInvariant()}
    })
}
$Installation | ConvertTo-Json -Depth 5 | Set-Content "$Install\seekdb-sqlite-install.json" -Encoding UTF8
Get-FileHash "$Install\bin\sqlite3.dll","$Install\lib\sqlite3.lib","$Install\include\sqlite3.h"
Write-Output "SQLITE_CONTROL_BUILT=$Install"
