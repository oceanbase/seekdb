# PL Parser Generator for Windows (PowerShell)
# Windows-only logic using win_bison / win_flex

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }

$env:PATH = "$ScriptDir\..\..\..\deps\3rd\usr\local\oceanbase\devtools\bin;$env:PATH"
$env:BISON_PKGDATADIR = "$ScriptDir\..\..\..\deps\3rd\usr\local\oceanbase\devtools\share\bison"

$BISON_BIN = "win_bison"
$FLEX_BIN = "win_flex"
$BISON_WARN_FLAGS = @("-Wno-deprecated", "-Wno-other")

$CACHE_MD5_FILE = Join-Path $ScriptDir "_MD5"
$TEMP_FILE = [System.IO.Path]::GetTempFileName()

# If win_bison is found in PATH, prefer its bundled share/bison data directory
$bisonPath = Get-Command $BISON_BIN -ErrorAction SilentlyContinue
if ($bisonPath) {
    $WIN_BISON_DIR = Split-Path $bisonPath.Source -Parent
    $WIN_BISON_ROOT = Split-Path $WIN_BISON_DIR -Parent
    $bisonShare = Join-Path $WIN_BISON_ROOT "share\bison"
    if (Test-Path -LiteralPath $bisonShare -PathType Container) {
        $env:BISON_PKGDATADIR = $bisonShare
    } else {
        Remove-Item Env:BISON_PKGDATADIR -ErrorAction SilentlyContinue
    }
}

if (-not (Get-Command $BISON_BIN -ErrorAction SilentlyContinue)) {
    Write-Error "$BISON_BIN not found in PATH"
    exit 1
}
if (-not (Get-Command $FLEX_BIN -ErrorAction SilentlyContinue)) {
    Write-Error "$FLEX_BIN not found in PATH"
    exit 1
}

function Test-OutputsMissing {
    $required = @(
        (Join-Path $ScriptDir "pl_parser_mysql_mode_tab.c"),
        (Join-Path $ScriptDir "pl_parser_mysql_mode_tab.h"),
        (Join-Path $ScriptDir "pl_parser_mysql_mode_lex.c")
    )
    foreach ($f in $required) {
        if (-not (Test-Path -LiteralPath $f) -or (Get-Item $f).Length -eq 0) {
            return $true
        }
    }
    return $false
}

function Invoke-BisonParser {
    param([string]$InputY, [string]$OutputC, [string]$ReportFile)
    $bisonArgs = @("-v") + $BISON_WARN_FLAGS + @("-d", $InputY, "-o", $OutputC)
    if ($ReportFile) { $bisonArgs += "--report-file=$ReportFile" }
    $prevErrPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $BISON_BIN $bisonArgs 2>&1 | ForEach-Object { "$_" } | Out-String
    } finally {
        $ErrorActionPreference = $prevErrPref
    }
    Write-Host $out
    if ($LASTEXITCODE -ne 0) { throw "bison failed: $out" }
    if ($out -match "conflict") { throw "bison conflict: $out" }
}

# Compute MD5 of .y + .l files for cache comparison
$yFile = Join-Path $ScriptDir "pl_parser_mysql_mode.y"
$lFile = Join-Path $ScriptDir "pl_parser_mysql_mode.l"
Get-Content -Raw -LiteralPath $yFile | Set-Content -LiteralPath $TEMP_FILE -NoNewline
Add-Content -LiteralPath $TEMP_FILE -Value (Get-Content -Raw -LiteralPath $lFile)
$md5Algo = [System.Security.Cryptography.MD5]::Create()
$md5Hash = [BitConverter]::ToString($md5Algo.ComputeHash([System.IO.File]::ReadAllBytes($TEMP_FILE))).Replace('-', '')
Remove-Item -LiteralPath $TEMP_FILE -Force -ErrorAction SilentlyContinue

# Insert a forward declaration of ObParseCtx after the include guard in the generated .h file.
# WinBison puts "int obpl_mysql_yyparse(ObParseCtx *)" in the header but the type is not declared there.
# Inherently idempotent: the regex only matches when the forward decl is absent.
function Ensure-ObParseCtxForwardDecl {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    $content = [System.IO.File]::ReadAllText($Path)
    if ($content -match 'typedef struct _ObParseCtx ObParseCtx;') {
        Write-Host "ObParseCtx forward declaration already present in $Path"
        return
    }
    # Insert after the include guard #define line (PL_PARSER_MYSQL_MODE_TAB_H_INCLUDED)
    $pattern = '(#\s*define\s+[^\r\n]*PL_PARSER_MYSQL_MODE_TAB_H_INCLUDED[^\r\n]*\r?\n)'
    $replacement = '$1typedef struct _ObParseCtx ObParseCtx;' + "`r`n"
    $content = $content -replace $pattern, $replacement
    [System.IO.File]::WriteAllText($Path, $content)
    Write-Host "Inserted ObParseCtx forward declaration in $Path"
}

# Ensure YYID macro is defined in the generated header.
# The custom YYLLOC_DEFAULT macro uses YYID() but WinBison may not emit its definition.
function Ensure-YYIDDefined {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    $content = [System.IO.File]::ReadAllText($Path)
    if ($content -match '#\s*define\s+YYID') {
        Write-Host "YYID already defined in $Path"
        return
    }
    # Insert before YYLLOC_DEFAULT
    $pattern = '(#\s*define\s+YYLLOC_DEFAULT)'
    $insert = "#ifndef YYID`r`n# define YYID(n) (n)`r`n#endif`r`n`r`n"
    $replacement = $insert + '$1'
    $content = $content -replace $pattern, $replacement
    [System.IO.File]::WriteAllText($Path, $content)
    Write-Host "Inserted YYID definition in $Path"
}

# Replace exact string in file (idempotent: skips if Old not found)
function Patch-File {
    param([string]$Path, [string]$Old, [string]$New)
    if (-not (Test-Path $Path)) { return }
    $c = [System.IO.File]::ReadAllText($Path)
    if ($c.Contains($Old)) {
        $c = $c.Replace($Old, $New)
        [System.IO.File]::WriteAllText($Path, $c)
    }
}

# Replace by regex pattern in file
function Patch-FileRegex {
    param([string]$Path, [string]$Pattern, [string]$Replacement)
    if (-not (Test-Path $Path)) { return }
    $c = [System.IO.File]::ReadAllText($Path)
    if ($c -match $Pattern) {
        $c = $c -replace $Pattern, $Replacement
        [System.IO.File]::WriteAllText($Path, $c)
    }
}

function Invoke-GenerateParser {
    $yPath = Join-Path $ScriptDir "pl_parser_mysql_mode.y"
    $tabC = Join-Path $ScriptDir "pl_parser_mysql_mode_tab.c"
    $reportPath = Join-Path $ScriptDir "pl_parser_mysql_mode.output"
    $lPath = Join-Path $ScriptDir "pl_parser_mysql_mode.l"
    $lexC = Join-Path $ScriptDir "pl_parser_mysql_mode_lex.c"
    $tabH = Join-Path $ScriptDir "pl_parser_mysql_mode_tab.h"

    Invoke-BisonParser -InputY $yPath -OutputC $tabC -ReportFile $reportPath
    & $FLEX_BIN -o $lexC $lPath $tabH
    if ($LASTEXITCODE -ne 0) {
        Write-Error "flex failed"
        exit 1
    }

    # WinBison generates "int obpl_mysql_yyparse(ObParseCtx *parse_ctx);" in the .h file,
    # but ObParseCtx is not declared there. Insert a forward declaration after the include guard.
    Ensure-ObParseCtxForwardDecl $tabH

    # Ensure YYID macro is defined before YYLLOC_DEFAULT uses it.
    # Older Bison emits YYID but WinBison may omit the definition.
    Ensure-YYIDDefined $tabH

    # WinBison generates yylex call with only 2 args; the reentrant scanner needs the 3rd (yyscanner) arg
    Patch-File $tabC 'yychar = yylex (&yylval, &yylloc, yyscanner);' 'yychar = yylex (&yylval, &yylloc, parse_ctx->scanner_ctx_.yyscan_info_);'
    Patch-File $tabC 'yychar = yylex (&yylval, &yylloc);' 'yychar = yylex (&yylval, &yylloc, parse_ctx->scanner_ctx_.yyscan_info_);'

    # WinBison copies action verbatim; the .y source has "{ $$ = NULL }" missing a semicolon (GCC accepts, clang rejects)
    Patch-File $tabC '(yyval.node) = NULL }' '(yyval.node) = NULL; }'

    $oracleFiles = @(
        (Join-Path $ScriptDir "pl_parser_oracle_mode_lex.c"),
        (Join-Path $ScriptDir "pl_parser_oracle_mode_tab.c"),
        (Join-Path $ScriptDir "pl_parser_oracle_mode_tab.h")
    )
    foreach ($f in $oracleFiles) {
        if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
    }

    Set-Content -LiteralPath $CACHE_MD5_FILE -Value $md5Hash
}

if ($env:NEED_PARSER_CACHE -eq "ON") {
    Write-Host "generate pl parser with cache"
    if (Test-Path -LiteralPath $CACHE_MD5_FILE) {
        $originMd5 = (Get-Content -LiteralPath $CACHE_MD5_FILE -Raw).Trim()
        if ($md5Hash -eq $originMd5 -and -not (Test-OutputsMissing)) {
            Write-Host "hit the md5 cache"
            exit 0
        }
    }
    Invoke-GenerateParser
} else {
    Write-Host "generate pl parser without cache"
    Invoke-GenerateParser
}
