# gen_parser_win.ps1
# PowerShell version: generates SQL parser on Windows only (FTS + MySQL mode), Oracle modules excluded.
# Requires: WinFlexBison (win_bison, win_flex must be in PATH or devtools/bin)

$ErrorActionPreference = "Stop"
$ParserDir = $PSScriptRoot
$RepoRoot = (Resolve-Path (Join-Path $ParserDir "../../..")).Path
$DevToolsBin = Join-Path $RepoRoot "deps/3rd/usr/local/oceanbase/devtools/bin"
$BisonShare = Join-Path $RepoRoot "deps/3rd/usr/local/oceanbase/devtools/share/bison"

$env:PATH = "$DevToolsBin;$env:PATH"
if (Test-Path $BisonShare) { $env:BISON_PKGDATADIR = $BisonShare }

$BISON_BIN = "win_bison"
$FLEX_BIN = "win_flex"

function Find-Exe($name) {
    $exe = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $exe) { throw "$name not found in PATH. Install WinFlexBison and add to PATH or deps/3rd/.../devtools/bin." }
    return $exe.Source
}

function Bison-Parser {
    param([string]$InputY, [string]$OutputC, [string]$ReportFile = $null)
    $bisonArgs = @("-v", "-Wno-deprecated", "-Wno-other", "-d", $InputY, "-o", $OutputC)
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

function Patch-File {
    param([string]$Path, [string]$Old, [string]$New)
    if (-not (Test-Path $Path)) { return }
    $c = [System.IO.File]::ReadAllText($Path)
    if ($c -match [regex]::Escape($Old)) {
        $c = $c.Replace($Old, $New)
        [System.IO.File]::WriteAllText($Path, $c)
    }
}

function Patch-FileRegex {
    param([string]$Path, [string]$Pattern, [string]$Replacement)
    if (-not (Test-Path $Path)) { return }
    $c = [System.IO.File]::ReadAllText($Path)
    $c = [regex]::Replace($c, $Pattern, $Replacement)
    [System.IO.File]::WriteAllText($Path, $c)
}

function Insert-AfterLine {
    param([string]$Path, [string]$AfterPattern, [string]$LineToInsert)
    $lines = [System.IO.File]::ReadAllLines($Path)
    $newLines = New-Object System.Collections.ArrayList
    $inserted = $false
    foreach ($line in $lines) {
        $null = $newLines.Add($line)
        if (-not $inserted -and $line -match [regex]::Escape($AfterPattern)) {
            $null = $newLines.Add($LineToInsert)
            $inserted = $true
        }
    }
    [System.IO.File]::WriteAllLines($Path, $newLines)
}

# Remove all lines containing the given substring
function Remove-LinesContaining {
    param([string]$Path, [string]$Substring)
    if (-not (Test-Path $Path)) { return }
    $lines = [System.IO.File]::ReadAllLines($Path) | Where-Object { $_ -notlike "*$Substring*" }
    [System.IO.File]::WriteAllLines($Path, $lines)
}

# Ensure "struct yyguts_t * yyg = ..." is declared before "yylval = yylval_param;" inside yylex.
# Targets only yylex by anchoring on the unique "int yy_act;" context that precedes yylval.
# Inherently idempotent: after insertion the blank-line pattern no longer matches.
function Ensure-YygInYylex {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    $content = [System.IO.File]::ReadAllText($Path)
    # Pattern: "int yy_act;" <newline> <blank line> <optional indent> "yylval = yylval_param;"
    # This only exists in yylex (not in yyset_lval which also has yylval = yylval_param).
    # After insertion the blank line is followed by the yyg decl, breaking the pattern (idempotent).
    $pattern = '(int yy_act;[^\r\n]*\r?\n)([ \t]*\r?\n)([ \t]*)(yylval = yylval_param;)'
    if ($content -match $pattern) {
        $replacement = '$1$2$3struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;' + "`r`n" + '$3$4'
        $content = $content -replace $pattern, $replacement
        [System.IO.File]::WriteAllText($Path, $content)
        Write-Host "Inserted yyg declaration before yylval in yylex ($Path)"
    } else {
        Write-Host "yyg declaration already present in yylex or pattern not found ($Path)"
    }
}

# Remove the "struct yyguts_t * yyg = ..." line inside yy_fatal_error body (mirrors sh sed range-delete)
function Remove-YygDeclarationInYyFatalError {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    $lines = [System.IO.File]::ReadAllLines($Path)
    $inBlock = $false
    $newLines = New-Object System.Collections.ArrayList
    $yygLine = 'struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;'
    foreach ($line in $lines) {
        if ($line -match 'static void yynoreturn yy_fatal_error') { $inBlock = $true }
        if ($inBlock) {
            if ($line.Trim() -eq $yygLine.Trim()) { continue }
            if ($line -match '^\s*\}\s*$') { $inBlock = $false }
        }
        $null = $newLines.Add($line)
    }
    [System.IO.File]::WriteAllLines($Path, $newLines)
}

$CACHE_MD5_FILE = Join-Path $ParserDir "_MD5"
$RequiredOutputs = @(
    (Join-Path $ParserDir "ftsparser_tab.c"),
    (Join-Path $ParserDir "ftsparser_tab.h"),
    (Join-Path $ParserDir "ftsblex_lex.c"),
    (Join-Path $ParserDir "sql_parser_mysql_mode_tab.c"),
    (Join-Path $ParserDir "sql_parser_mysql_mode_tab.h"),
    (Join-Path $ParserDir "sql_parser_mysql_mode_lex.c"),
    (Join-Path $ParserDir "type_name.c")
)

function Get-ParserInputHash {
    $mysqlY = Join-Path $ParserDir "sql_parser_mysql_mode.y"
    $mysqlL = Join-Path $ParserDir "sql_parser_mysql_mode.l"
    $obItemH = Join-Path $RepoRoot "src/objit/include/objit/common/ob_item_type.h"
    $combined = [System.IO.File]::ReadAllText($mysqlY) + [System.IO.File]::ReadAllText($mysqlL)
    if (Test-Path $obItemH) { $combined += [System.IO.File]::ReadAllText($obItemH) }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($combined)
    $hash = [System.Security.Cryptography.MD5]::Create().ComputeHash($bytes)
    return ([System.BitConverter]::ToString($hash) -replace '-','').ToLower()
}

function Test-OutputsMissing {
    foreach ($f in $RequiredOutputs) {
        if (-not (Test-Path $f) -or (Get-Item $f).Length -eq 0) { return $true }
    }
    return $false
}

function Generate-Parser {
    $bison = Find-Exe $BISON_BIN
    $flex = Find-Exe $FLEX_BIN
    Write-Host "Using bison: $bison"
    Write-Host "Using flex:  $flex"

    # ---- FTS parser ----
    Write-Host "Generating FTS parser..."
    Bison-Parser (Join-Path $ParserDir "ftsparser.y") (Join-Path $ParserDir "ftsparser_tab.c")

    $ftsparserTabH = Join-Path $ParserDir "ftsparser_tab.h"
    # Bison-generated include guard varies by path (e.g. YY_OBSQL_FTS_YY_C_..._FTSPARSER_TAB_H_INCLUDED); match fixed suffix only
    Insert-AfterLine $ftsparserTabH 'FTSPARSER_TAB_H_INCLUDED' '#include "fts_base.h"'

    $ftsparserTabC = Join-Path $ParserDir "ftsparser_tab.c"
    Patch-FileRegex $ftsparserTabC 'yychar = yylex \(&yylval, &yylloc, yyscanner\);' 'yychar = yylex (&yylval, &yylloc, YYLEX_PARAM);'
    Patch-FileRegex $ftsparserTabC 'yychar = yylex \(&yylval, &yylloc\);' 'yychar = yylex (&yylval, &yylloc, YYLEX_PARAM);'

    & $FLEX_BIN -Cfa -B -8 -o (Join-Path $ParserDir "ftsblex_lex.c") (Join-Path $ParserDir "ftsblex.l") $ftsparserTabH

    $ftslex = Join-Path $ParserDir "ftsblex_lex.c"
    Remove-LinesContaining $ftslex "This var may be unused depending upon options"
    # Replace all (void)yyg; with (void)yyscanner; (literal replace, matches sh sed s/.../.../g)
    $lexContent = [System.IO.File]::ReadAllText($ftslex)
    $lexContent = $lexContent.Replace('(void)yyg;', '(void)yyscanner;')
    [System.IO.File]::WriteAllText($ftslex, $lexContent)
    Remove-YygDeclarationInYyFatalError $ftslex
    Ensure-YygInYylex $ftslex
    # memcpy replacement (Setup the input buffer state...): remove "int i;" line and for-loop line, replace buf[i]=yybytes[i] with memcpy
    $content = [System.IO.File]::ReadAllText($ftslex)
    $pat1 = '(?s)(Setup the input buffer state to scan the given bytes.*?)\s+int i\s*;[^\r\n]*[\r\n]+'
    $content = $content -replace $pat1, '$1'
    $pat2 = '(?s)(Setup the input buffer state to scan the given bytes.*?)' + [regex]::Escape('for ( i = 0; i < _yybytes_len; ++i )') + '[^\r\n]*[\r\n]+'
    $content = $content -replace $pat2, '$1'
    $content = $content -replace "(\t)buf\[i\] = yybytes\[i\]", '${1}memcpy(buf, yybytes, _yybytes_len)'
    [System.IO.File]::WriteAllText($ftslex, $content)

    # ---- MySQL mode parser ----
    Write-Host "Generating MySQL mode parser..."
    $mysqlTabC = Join-Path $ParserDir "sql_parser_mysql_mode_tab.c"
    $mysqlOutput = Join-Path $ParserDir "sql_parser_mysql_mode.output"
    Bison-Parser (Join-Path $ParserDir "sql_parser_mysql_mode.y") $mysqlTabC $mysqlOutput

    & $FLEX_BIN -Cfa -B -8 -o (Join-Path $ParserDir "sql_parser_mysql_mode_lex.c") (Join-Path $ParserDir "sql_parser_mysql_mode.l") (Join-Path $ParserDir "sql_parser_mysql_mode_tab.h")

    $mysqlLex = Join-Path $ParserDir "sql_parser_mysql_mode_lex.c"
    $mysqlContent = [System.IO.File]::ReadAllText($mysqlLex)
    # Remove "int i;" line and for-loop line (mirrors gen_parser_win.sh sed /int i/d and /for ( i = 0.../d)
    $pat1 = '(?s)(Setup the input buffer state to scan the given bytes.*?)\s+int i\s*;[^\r\n]*[\r\n]+'
    $mysqlContent = $mysqlContent -replace $pat1, '$1'
    $pat2 = '(?s)(Setup the input buffer state to scan the given bytes.*?)' + [regex]::Escape('for ( i = 0; i < _yybytes_len; ++i )') + '[^\r\n]*[\r\n]+'
    $mysqlContent = $mysqlContent -replace $pat2, '$1'
    $mysqlContent = $mysqlContent -replace "(\t)buf\[i\] = yybytes\[i\]", '${1}memcpy(buf, yybytes, _yybytes_len)'
    $mysqlContent = $mysqlContent -replace '(?s)(obsql_mysql_yylex_init is special because it creates the scanner itself.*?Initialization is the same as for the non-reentrant scanner.*?)return 1\b', '${1}return errno'
    [System.IO.File]::WriteAllText($mysqlLex, $mysqlContent)

    Patch-File $mysqlTabC 'yychar = yylex (&yylval, &yylloc, yyscanner);' 'yychar = yylex (&yylval, &yylloc, result->yyscan_info_);'
    Patch-File $mysqlTabC 'yychar = yylex (&yylval, &yylloc);' 'yychar = yylex (&yylval, &yylloc, result->yyscan_info_);'

    # ---- type_name.c (PowerShell implementation, replaces gen_type_name.sh) ----
    Write-Host "Generating type_name.c..."
    $obItemTypeH = Join-Path $RepoRoot "src/objit/include/objit/common/ob_item_type.h"
    $typeNameC = Join-Path $ParserDir "type_name.c"
    $lines = @(
        '#include "objit/common/ob_item_type.h"',
        "const char* get_type_name(int type)",
        "{",
        "`tswitch(type){"
    )
    $caseLines = Get-Content $obItemTypeH -Raw | Select-String -Pattern '(?m)^\s*(T_[_A-Z1-9]+)\s*[=0-9]*,' -AllMatches | ForEach-Object { $_.Matches } | ForEach-Object { "`tcase " + $_.Groups[1].Value + " : return `"" + $_.Groups[1].Value + "`";" }
    $lines += $caseLines
    $lines += "`tdefault:return `"Unknown`";", "`t}", "}"
    $lines | Set-Content $typeNameC -Encoding UTF8

    Write-Host "Done. Generated: ftsparser_tab.c/h, ftsblex_lex.c, sql_parser_mysql_mode_tab.c/h, sql_parser_mysql_mode_lex.c, type_name.c"
}

# MD5 cache check (mirrors gen_parser_win.sh): skip generation when NEED_PARSER_CACHE=ON and inputs are unchanged with all outputs present
$md5Value = Get-ParserInputHash
if ($env:NEED_PARSER_CACHE -eq "ON") {
    Write-Host "generate sql parser with cache"
    $originMd5 = $null
    if (Test-Path $CACHE_MD5_FILE) { $originMd5 = (Get-Content $CACHE_MD5_FILE -Raw).Trim() }
    if ($md5Value -eq $originMd5 -and -not (Test-OutputsMissing)) {
        Write-Host "hit the md5 cache"
        # Apply patches even on cache hit to ensure generated files are always patched (same as after a full regeneration)
        $ftsparserTabH = Join-Path $ParserDir "ftsparser_tab.h"
        $hContent = [System.IO.File]::ReadAllText($ftsparserTabH)
        if ($hContent -notmatch 'fts_base\.h') { Insert-AfterLine $ftsparserTabH 'FTSPARSER_TAB_H_INCLUDED' '#include "fts_base.h"' }
        $ftslex = Join-Path $ParserDir "ftsblex_lex.c"
        if (Test-Path $ftslex) {
            $lexContent = [System.IO.File]::ReadAllText($ftslex)
            $lexContent = $lexContent.Replace('(void)yyg;', '(void)yyscanner;')
            [System.IO.File]::WriteAllText($ftslex, $lexContent)
            Remove-YygDeclarationInYyFatalError $ftslex
        }
        $mysqlTabC = Join-Path $ParserDir "sql_parser_mysql_mode_tab.c"
        Patch-File $mysqlTabC 'yychar = yylex (&yylval, &yylloc, yyscanner);' 'yychar = yylex (&yylval, &yylloc, result->yyscan_info_);'
        Patch-File $mysqlTabC 'yychar = yylex (&yylval, &yylloc);' 'yychar = yylex (&yylval, &yylloc, result->yyscan_info_);'
    } else {
        Generate-Parser
        $md5Value | Set-Content $CACHE_MD5_FILE -NoNewline
    }
} else {
    Write-Host "generate sql parser without cache"
    Generate-Parser
    $md5Value | Set-Content $CACHE_MD5_FILE -NoNewline
}

# Apply final patches unconditionally (both generate and cache paths) to guarantee .c/.h files on disk are always patched
function Apply-FinalParserPatches {
    $ftsparserTabH = Join-Path $ParserDir "ftsparser_tab.h"
    if (Test-Path $ftsparserTabH) {
        $hContent = [System.IO.File]::ReadAllText($ftsparserTabH)
        if ($hContent -notmatch 'fts_base\.h') { Insert-AfterLine $ftsparserTabH 'FTSPARSER_TAB_H_INCLUDED' '#include "fts_base.h"' }
    }
    $ftslex = Join-Path $ParserDir "ftsblex_lex.c"
    if (Test-Path $ftslex) {
        $lexContent = [System.IO.File]::ReadAllText($ftslex)
        $lexContent = $lexContent.Replace('(void)yyg;', '(void)yyscanner;')
        [System.IO.File]::WriteAllText($ftslex, $lexContent)
        Remove-YygDeclarationInYyFatalError $ftslex
        Ensure-YygInYylex $ftslex
    }
    $mysqlTabC = Join-Path $ParserDir "sql_parser_mysql_mode_tab.c"
    if (Test-Path $mysqlTabC) {
        Patch-File $mysqlTabC 'yychar = yylex (&yylval, &yylloc, yyscanner);' 'yychar = yylex (&yylval, &yylloc, result->yyscan_info_);'
        Patch-File $mysqlTabC 'yychar = yylex (&yylval, &yylloc);' 'yychar = yylex (&yylval, &yylloc, result->yyscan_info_);'
    }
}
Apply-FinalParserPatches
