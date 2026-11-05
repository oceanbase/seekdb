<#
.SYNOPSIS
    SeekDB Windows management script -- install, start, stop, restart, status, uninstall.

.EXAMPLE
    .\seekdb_manage.ps1 install
    .\seekdb_manage.ps1 start
    .\seekdb_manage.ps1 stop
    .\seekdb_manage.ps1 restart
    .\seekdb_manage.ps1 status
    .\seekdb_manage.ps1 uninstall
#>

param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet("install", "start", "stop", "restart", "status", "uninstall", "help")]
    [string]$Action,

    [string]$ServiceName = "seekdb",

    [string]$BaseDir = "C:\ProgramData\seekdb",

    [string]$ConfigFile = "",

    [int]$Port = 0,

    [string]$MemoryLimit = "",

    [int]$CpuCount = 0
)

$ErrorActionPreference = "Stop"

# ── Resolve paths ────────────────────────────────────────────────────
$ScriptDir = $PSScriptRoot
$BinDir = $ScriptDir

if (-not (Test-Path "$BinDir\seekdb.exe")) {
    $ParentBin = Join-Path (Split-Path $ScriptDir -Parent) "bin"
    if (Test-Path "$ParentBin\seekdb.exe") {
        $BinDir = $ParentBin
    }
}

$SeekdbExe = Join-Path $BinDir "seekdb.exe"

function Write-Log  { param([string]$msg) Write-Host "[seekdb] $msg" }
function Write-Err  { param([string]$msg) Write-Host "[seekdb][ERROR] $msg" -ForegroundColor Red }
function Write-Ok   { param([string]$msg) Write-Host "[seekdb] $msg" -ForegroundColor Green }

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Err "This operation requires Administrator privileges."
        Write-Err "Please run this script from an elevated PowerShell prompt."
        exit 1
    }
}

function Assert-SeekdbExists {
    if (-not (Test-Path $SeekdbExe)) {
        Write-Err "seekdb.exe not found at: $SeekdbExe"
        Write-Err "Please run this script from the seekdb bin/ directory or ensure seekdb is installed."
        exit 1
    }
}

function Read-Config {
    param([string]$Path)
    $params = @{}
    if (-not (Test-Path $Path)) { return $params }
    Get-Content $Path | ForEach-Object {
        $line = $_.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { return }
        $eqIdx = $line.IndexOf("=")
        if ($eqIdx -gt 0) {
            $key = $line.Substring(0, $eqIdx).Trim()
            $val = $line.Substring($eqIdx + 1).Trim()
            $params[$key] = $val
        }
    }
    return $params
}

# ── install ──────────────────────────────────────────────────────────
function Do-Install {
    Assert-Admin
    Assert-SeekdbExists

    Write-Log "Installing SeekDB..."

    # Resolve config
    if ($ConfigFile -and (Test-Path $ConfigFile)) {
        $cfg = Read-Config $ConfigFile
        if ($cfg["base-dir"]) { $BaseDir = $cfg["base-dir"] }
    }
    $BaseDir = $BaseDir -replace "\\", "/"

    # Create data directories
    if (-not (Test-Path $BaseDir)) {
        New-Item -ItemType Directory -Path $BaseDir -Force | Out-Null
        Write-Log "Created base directory: $BaseDir"
    }

    # Build install args: pass through all original args except install action
    $installArgs = @("--install-service", $ServiceName, "--base-dir=$BaseDir")
    if ($Port -gt 0)      { $installArgs += "--port=$Port" }
    if ($MemoryLimit)      { $installArgs += "--parameter"; $installArgs += "memory_limit=$MemoryLimit" }
    if ($CpuCount -gt 0)  { $installArgs += "--parameter"; $installArgs += "cpu_count=$CpuCount" }

    # Check if already initialized
    $metaFile = Join-Path $BaseDir ".meta"
    $dataDir  = Join-Path $BaseDir "store"
    $needInit = (-not (Test-Path $metaFile)) -and (-not (Test-Path $dataDir) -or (Get-ChildItem $dataDir -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0)

    if ($needInit) {
        Write-Log "Initializing database (first run)..."
        $initArgs = @("--base-dir=$BaseDir", "--nodaemon")
        if ($Port -gt 0)      { $initArgs += "--port=$Port" }
        if ($MemoryLimit)      { $initArgs += "--parameter"; $initArgs += "memory_limit=$MemoryLimit" }
        if ($CpuCount -gt 0)  { $initArgs += "--parameter"; $initArgs += "cpu_count=$CpuCount" }

        Write-Log "  $SeekdbExe $($initArgs -join ' ')"
        & $SeekdbExe @initArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Database initialization failed (exit code $LASTEXITCODE)."
            exit $LASTEXITCODE
        }
        Write-Ok "Database initialized successfully."
    } else {
        Write-Log "Database already initialized at $BaseDir, skipping initialization."
    }

    Write-Log "Registering Windows service '$ServiceName'..."
    Write-Log "  $SeekdbExe $($installArgs -join ' ')"
    & $SeekdbExe @installArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Service installation failed (exit code $LASTEXITCODE)."
        exit $LASTEXITCODE
    }
    Write-Ok "Service '$ServiceName' installed successfully."
    Write-Log ""
    Write-Log "Next steps:"
    Write-Log "  .\seekdb_manage.ps1 start"
    Write-Log "  mysql -h 127.0.0.1 -P 2881 -uroot"
}

# ── start ────────────────────────────────────────────────────────────
function Do-Start {
    Assert-Admin
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $svc) {
        Write-Err "Service '$ServiceName' is not installed. Run 'install' first."
        exit 1
    }
    if ($svc.Status -eq "Running") {
        Write-Log "Service '$ServiceName' is already running."
        return
    }
    Write-Log "Starting service '$ServiceName'..."
    Start-Service -Name $ServiceName
    Write-Ok "Service '$ServiceName' started."
}

# ── stop ─────────────────────────────────────────────────────────────
function Do-Stop {
    Assert-Admin
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $svc) {
        Write-Err "Service '$ServiceName' is not installed."
        exit 1
    }
    if ($svc.Status -eq "Stopped") {
        Write-Log "Service '$ServiceName' is already stopped."
        return
    }
    Write-Log "Stopping service '$ServiceName'..."
    Stop-Service -Name $ServiceName -Force
    Write-Ok "Service '$ServiceName' stopped."
}

# ── restart ──────────────────────────────────────────────────────────
function Do-Restart {
    Assert-Admin
    Do-Stop
    Start-Sleep -Seconds 2
    Do-Start
}

# ── status ───────────────────────────────────────────────────────────
function Do-Status {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $svc) {
        Write-Log "Service '$ServiceName' is not installed."
        return
    }
    Write-Log "Service: $($svc.DisplayName)"
    Write-Log "Status:  $($svc.Status)"

    if ($svc.Status -eq "Running") {
        $proc = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'" -ErrorAction SilentlyContinue
        if ($proc -and $proc.ProcessId) {
            Write-Log "PID:     $($proc.ProcessId)"
        }
    }
}

# ── uninstall ────────────────────────────────────────────────────────
function Do-Uninstall {
    Assert-Admin
    Assert-SeekdbExists

    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $svc) {
        Write-Log "Service '$ServiceName' is not installed."
        return
    }

    if ($svc.Status -ne "Stopped") {
        Write-Log "Stopping service first..."
        Stop-Service -Name $ServiceName -Force
        Start-Sleep -Seconds 2
    }

    Write-Log "Removing service '$ServiceName'..."
    & $SeekdbExe --remove-service $ServiceName
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Service removal failed (exit code $LASTEXITCODE)."
        exit $LASTEXITCODE
    }
    Write-Ok "Service '$ServiceName' removed."
    Write-Log "Note: Database files in '$BaseDir' are preserved. Delete manually if no longer needed."
}

# ── help ─────────────────────────────────────────────────────────────
function Do-Help {
    Write-Host @"

SeekDB Windows Management Script

Usage:
    .\seekdb_manage.ps1 <Action> [Options]

Actions:
    install     Initialize database and register Windows service
    start       Start the seekdb service
    stop        Stop the seekdb service
    restart     Restart the seekdb service
    status      Show service status
    uninstall   Stop and remove the Windows service
    help        Show this help message

Options:
    -ServiceName <name>     Service name (default: seekdb)
    -BaseDir <path>         Base data directory (default: C:\ProgramData\seekdb)
    -ConfigFile <path>      Path to seekdb.cnf config file
    -Port <port>            MySQL port (default: 2881)
    -MemoryLimit <size>     Memory limit (e.g. 2G, 4G)
    -CpuCount <n>           CPU count

Examples:
    # Full install with defaults
    .\seekdb_manage.ps1 install

    # Install with custom settings
    .\seekdb_manage.ps1 install -BaseDir D:\seekdb -Port 3306 -MemoryLimit 4G

    # Service management
    .\seekdb_manage.ps1 start
    .\seekdb_manage.ps1 stop
    .\seekdb_manage.ps1 status

    # Connect after start
    mysql -h 127.0.0.1 -P 2881 -uroot

"@
}

# ── Main ─────────────────────────────────────────────────────────────
switch ($Action) {
    "install"   { Do-Install }
    "start"     { Do-Start }
    "stop"      { Do-Stop }
    "restart"   { Do-Restart }
    "status"    { Do-Status }
    "uninstall" { Do-Uninstall }
    "help"      { Do-Help }
}
