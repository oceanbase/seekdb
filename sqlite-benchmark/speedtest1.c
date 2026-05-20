/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
** Originally derived from SQLite's speedtest1
**
** A program for performance testing.
**
** To build this program against an historical version of SQLite for comparison
** testing:
**
**    Unix:
**
**        ./configure --all
**        make clean speedtest1
**        mv speedtest1 speedtest1-current
**        cp $HISTORICAL_SQLITE3_C_H .
**        touch sqlite3.c sqlite3.h .target_source
**        make speedtest1
**        mv speedtest1 speedtest1-baseline
**
**    Windows:
**
**        nmake /f Makefile.msc clean speedtest1.exe
**        mv speedtest1.exe speedtest1-current.exe
**        cp $HISTORICAL_SQLITE_C_H .
**        touch sqlite3.c sqlite3.h .target_source
**        nmake /f Makefile.msc speedtest1.exe
**        mv speedtest1.exe speedtest1-baseline.exe
**
** The available command-line options are described below:
*/
static const char zHelp[] =
  "Usage: %s [--options] DATABASE\n"
  "Options:\n"
  "  --autovacuum        Enable AUTOVACUUM mode\n"
  "  --big-transactions  Add BEGIN/END around all large tests\n"
  "  --cachesize N       Set PRAGMA cache_size=N. Note: N is pages, not bytes\n"
  "  --checkpoint        Run PRAGMA wal_checkpoint after each test case\n"
  "  --exclusive         Enable locking_mode=EXCLUSIVE\n"
  "  --explain           Like --sqlonly but with added EXPLAIN keywords\n"
  "  --fullfsync         Enable fullfsync=TRUE\n"
  "  --heap SZ MIN       Memory allocator uses SZ bytes & min allocation MIN\n"
  "  --incrvacuum        Enable incremenatal vacuum mode\n"
  "  --journal M         Set the journal_mode to M\n"
  "  --key KEY           Set the encryption key to KEY\n"
  "  --lookaside N SZ    Configure lookaside for N slots of SZ bytes each\n"
  "  --memdb             Use an in-memory database\n"
  "  --mmap SZ           MMAP the first SZ bytes of the database file\n"
  "  --multithread       Set multithreaded mode\n"
  "  --nomemstat         Disable memory statistics\n"
  "  --nomutex           Open db with SQLITE_OPEN_NOMUTEX\n"
  "  --nosync            Set PRAGMA synchronous=OFF\n"
  "  --notnull           Add NOT NULL constraints to table columns\n"
  "  --output FILE       Store SQL output in FILE\n"
  "  --pagesize N        Set the page size to N\n"
  "  --pcache N SZ       Configure N pages of pagecache each of size SZ bytes\n"
  "  --primarykey        Use PRIMARY KEY instead of UNIQUE where appropriate\n"
  "  --repeat N          Repeat each SELECT N times (default: 1)\n"
  "  --reprepare         Reprepare each statement upon every invocation\n"
  "  --script FILE       Write an SQL script for the test into FILE\n"
  "  --serialized        Set serialized threading mode\n"
  "  --singlethread      Set single-threaded mode - disables all mutexing\n"
  "  --sqlonly           No-op.  Only show the SQL that would have been run.\n"
  "  --shrink-memory     Invoke sqlite3_db_release_memory() frequently.\n"
  "  --size N            Relative test size.  Default=100\n"
  "  --strict            Use STRICT table where appropriate\n"
  "  --stats             Show statistics at the end\n"
  "  --stmtscanstatus    Activate SQLITE_DBCONFIG_STMT_SCANSTATUS\n"
  "  --temp N            N from 0 to 9.  0: no temp table. 9: all temp tables\n"
  "  --testset T         Run test-set T (only \"main\" is supported)\n"
  "  --trace             Turn on SQL tracing\n"
  "  --threads N         Use up to N threads for sorting\n"
  "  --utf16be           Set text encoding to UTF-16BE\n"
  "  --utf16le           Set text encoding to UTF-16LE\n"
  "  --verify            Run additional verification steps\n"
  "  --vfs NAME          Use the given (preinstalled) VFS\n"
  "  --without-rowid     Use WITHOUT ROWID where appropriate\n"
;

#include "db_adapter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
# include <unistd.h>
#else
# include <io.h>
#endif
#define ISSPACE(X) isspace((unsigned char)(X))
#define ISDIGIT(X) isdigit((unsigned char)(X))

#if SQLITE_VERSION_NUMBER<3005000
# define sqlite3_int64 sqlite_int64
#endif

typedef sqlite3_uint64 u64;

/*
** State structure for a Hash hash in progress
*/
typedef struct HashContext HashContext;
struct HashContext {
  unsigned char isInit;          /* True if initialized */
  unsigned char i, j;            /* State variables */
  unsigned char s[256];          /* State variables */
  unsigned char r[32];           /* Result */
};


/* All global state is held in this structure */
static struct Global {
  sqlite3 *db;               /* The open database connection */
  sqlite3_stmt *pStmt;       /* Current SQL statement */
  sqlite3_int64 iStart;      /* Start-time for the current test */
  sqlite3_int64 iTotal;      /* Total time */
  int bWithoutRowid;         /* True for --without-rowid */
  int bReprepare;            /* True to reprepare the SQL on each rerun */
  int bSqlOnly;              /* True to print the SQL once only */
  int bExplain;              /* Print SQL with EXPLAIN prefix */
  int bVerify;               /* Try to verify that results are correct */
  int bMemShrink;            /* Call sqlite3_db_release_memory() often */
  int eTemp;                 /* 0: no TEMP.  9: always TEMP. */
  int szTest;                /* Scale factor for test iterations */
  int szBase;                /* Base size prior to testset scaling */
  int nRepeat;               /* Repeat selects this many times */
  int doCheckpoint;          /* Run PRAGMA wal_checkpoint after each trans */
  int stmtScanStatus;        /* True to activate Stmt ScanStatus reporting */
  int doBigTransactions;     /* Enable transactions on tests 410 and 510 */
  const char *zWR;           /* Might be WITHOUT ROWID */
  const char *zNN;           /* Might be NOT NULL */
  const char *zPK;           /* Might be UNIQUE or PRIMARY KEY */
  unsigned int x, y;         /* Pseudo-random number generator state */
  u64 nResByte;              /* Total number of result bytes */
  int nResult;               /* Size of the current result */
  char zResult[3000];        /* Text of the current result */
  FILE *pScript;             /* Write an SQL script into this file */
#ifndef SPEEDTEST_OMIT_HASH
  FILE *hashFile;            /* Store all hash results in this file */
  HashContext hash;          /* Hash of all output */
#endif
} g;

/* Return " TEMP" or "", as appropriate for creating a table.
*/
static const char *isTemp(int N){
  return g.eTemp>=N ? " TEMP" : "";
}

/* Print an error message and exit */
static void fatal_error(const char *zMsg, ...){
  va_list ap;
  va_start(ap, zMsg);
  vfprintf(stderr, zMsg, ap);
  va_end(ap);
  fflush(stderr);
  fflush(stdout);
  _exit(1);
}

#ifndef SPEEDTEST_OMIT_HASH
/****************************************************************************
** Hash algorithm used to verify that compilation is not miscompiled
** in such a was as to generate an incorrect result.
*/

/*
** Initialize a new hash.  iSize determines the size of the hash
** in bits and should be one of 224, 256, 384, or 512.  Or iSize
** can be zero to use the default hash size of 256 bits.
*/
static void HashInit(void){
  unsigned int k;
  g.hash.i = 0;
  g.hash.j = 0;
  for(k=0; k<256; k++) g.hash.s[k] = k;
}

/*
** Make consecutive calls to the HashUpdate function to add new content
** to the hash
*/
static void HashUpdate(
  const unsigned char *aData,
  unsigned int nData
){
  unsigned char t;
  unsigned char i = g.hash.i;
  unsigned char j = g.hash.j;
  unsigned int k;
  if( g.hashFile ) fwrite(aData, 1, nData, g.hashFile);
  for(k=0; k<nData; k++){
    j += g.hash.s[i] + aData[k];
    t = g.hash.s[j];
    g.hash.s[j] = g.hash.s[i];
    g.hash.s[i] = t;
    i++;
  }
  g.hash.i = i;
  g.hash.j = j;
}

/*
** After all content has been added, invoke HashFinal() to compute
** the final hash.  The hash result is stored in g.hash.r[].
*/
static void HashFinal(void){
  unsigned int k;
  unsigned char t, i, j;
  i = g.hash.i;
  j = g.hash.j;
  for(k=0; k<32; k++){
    i++;
    t = g.hash.s[i];
    j += t;
    g.hash.s[i] = g.hash.s[j];
    g.hash.s[j] = t;
    t += g.hash.s[i];
    g.hash.r[k] = g.hash.s[t];
  }
}

/* End of the Hash hashing logic
*****************************************************************************/
#endif /* SPEEDTEST_OMIT_HASH */

/*
** Return the value of a hexadecimal digit.  Return -1 if the input
** is not a hex digit.
*/
static int hexDigitValue(char c){
  if( c>='0' && c<='9' ) return c - '0';
  if( c>='a' && c<='f' ) return c - 'a' + 10;
  if( c>='A' && c<='F' ) return c - 'A' + 10;
  return -1;
}

/* Provide an alternative to sqlite3_stricmp() in older versions of
** SQLite */
#if SQLITE_VERSION_NUMBER<3007011
# define sqlite3_stricmp strcmp
#endif

/*
** Interpret zArg as an integer value, possibly with suffixes.
*/
static int integerValue(const char *zArg){
  sqlite3_int64 v = 0;
  static const struct { char *zSuffix; int iMult; } aMult[] = {
    { "KiB", 1024 },
    { "MiB", 1024*1024 },
    { "GiB", 1024*1024*1024 },
    { "KB",  1000 },
    { "MB",  1000000 },
    { "GB",  1000000000 },
    { "K",   1000 },
    { "M",   1000000 },
    { "G",   1000000000 },
  };
  int i;
  int isNeg = 0;
  if( zArg[0]=='-' ){
    isNeg = 1;
    zArg++;
  }else if( zArg[0]=='+' ){
    zArg++;
  }
  if( zArg[0]=='0' && zArg[1]=='x' ){
    int x;
    zArg += 2;
    while( (x = hexDigitValue(zArg[0]))>=0 ){
      v = (v<<4) + x;
      zArg++;
    }
  }else{
    while( isdigit(zArg[0]) ){
      v = v*10 + zArg[0] - '0';
      zArg++;
    }
  }
  for(i=0; i<sizeof(aMult)/sizeof(aMult[0]); i++){
    if( sqlite3_stricmp(aMult[i].zSuffix, zArg)==0 ){
      v *= aMult[i].iMult;
      break;
    }
  }
  if( v>0x7fffffff ) fatal_error("parameter too large - max 2147483648");
  return (int)(isNeg? -v : v);
}

/* Return the current wall-clock time, in milliseconds */
sqlite3_int64 speedtest1_timestamp(void){
#if SQLITE_VERSION_NUMBER<3005000
  return 0;
#else
  static sqlite3_vfs *clockVfs = 0;
  sqlite3_int64 t;
  if( clockVfs==0 ) clockVfs = sqlite3_vfs_find(0);
#if SQLITE_VERSION_NUMBER>=3007000
  if( clockVfs->iVersion>=2 && clockVfs->xCurrentTimeInt64!=0 ){
    clockVfs->xCurrentTimeInt64(clockVfs, &t);
  }else
#endif
  {
    double r;
    clockVfs->xCurrentTime(clockVfs, &r);
    t = (sqlite3_int64)(r*86400000.0);
  }
  return t;
#endif
}

/* Return a pseudo-random unsigned integer */
unsigned int speedtest1_random(void){
  g.x = (g.x>>1) ^ ((1+~(g.x&1)) & 0xd0000001);
  g.y = g.y*1103515245 + 12345;
  return g.x ^ g.y;
}

/* Map the value in within the range of 1...limit into another
** number in a way that is chatic and invertable.
*/
unsigned swizzle(unsigned in, unsigned limit){
  unsigned out = 0;
  while( limit ){
    out = (out<<1) | (in&1);
    in >>= 1;
    limit >>= 1;
  }
  return out;
}

/* Round up a number so that it is a power of two minus one
*/
unsigned roundup_allones(unsigned limit){
  unsigned m = 1;
  while( m<limit ) m = (m<<1)+1;
  return m;
}

/* The speedtest1_numbername procedure below converts its argment (an integer)
** into a string which is the English-language name for that number.
** The returned string should be freed with sqlite3_free().
**
** Example:
**
**     speedtest1_numbername(123)   ->  "one hundred twenty three"
*/
int speedtest1_numbername(unsigned int n, char *zOut, int nOut){
  static const char *ones[] = {  "zero", "one", "two", "three", "four", "five", 
                  "six", "seven", "eight", "nine", "ten", "eleven", "twelve", 
                  "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
                  "eighteen", "nineteen" };
  static const char *tens[] = { "", "ten", "twenty", "thirty", "forty",
                 "fifty", "sixty", "seventy", "eighty", "ninety" };
  int i = 0;

  if( n>=1000000000 ){
    i += speedtest1_numbername(n/1000000000, zOut+i, nOut-i);
    sqlite3_snprintf(nOut-i, zOut+i, " billion");
    i += (int)strlen(zOut+i);
    n = n % 1000000000;
  }
  if( n>=1000000 ){
    if( i && i<nOut-1 ) zOut[i++] = ' ';
    i += speedtest1_numbername(n/1000000, zOut+i, nOut-i);
    sqlite3_snprintf(nOut-i, zOut+i, " million");
    i += (int)strlen(zOut+i);
    n = n % 1000000;
  }
  if( n>=1000 ){
    if( i && i<nOut-1 ) zOut[i++] = ' ';
    i += speedtest1_numbername(n/1000, zOut+i, nOut-i);
    sqlite3_snprintf(nOut-i, zOut+i, " thousand");
    i += (int)strlen(zOut+i);
    n = n % 1000;
  }
  if( n>=100 ){
    if( i && i<nOut-1 ) zOut[i++] = ' ';
    sqlite3_snprintf(nOut-i, zOut+i, "%s hundred", ones[n/100]);
    i += (int)strlen(zOut+i);
    n = n % 100;
  }
  if( n>=20 ){
    if( i && i<nOut-1 ) zOut[i++] = ' ';
    sqlite3_snprintf(nOut-i, zOut+i, "%s", tens[n/10]);
    i += (int)strlen(zOut+i);
    n = n % 10;
  }
  if( n>0 ){
    if( i && i<nOut-1 ) zOut[i++] = ' ';
    sqlite3_snprintf(nOut-i, zOut+i, "%s", ones[n]);
    i += (int)strlen(zOut+i);
  }
  if( i==0 ){
    sqlite3_snprintf(nOut-i, zOut+i, "zero");
    i += (int)strlen(zOut+i);
  }
  return i;
}


/* Start a new test case */
#define NAMEWIDTH 60
static const char zDots[] =
  ".......................................................................";
static int iTestNumber = 0;  /* Current test # for begin/end_test(). */
void speedtest1_begin_test(int iTestNum, const char *zTestName, ...){
  int n = (int)strlen(zTestName);
  char *zName;
  va_list ap;
  iTestNumber = iTestNum;
  va_start(ap, zTestName);
  zName = sqlite3_vmprintf(zTestName, ap);
  va_end(ap);
  n = (int)strlen(zName);
  if( n>NAMEWIDTH ){
    zName[NAMEWIDTH] = 0;
    n = NAMEWIDTH;
  }
  if( g.pScript ){
    fprintf(g.pScript,"-- begin test %d %.*s\n", iTestNumber, n, zName)
      /* maintenance reminder: ^^^ code in ext/wasm expects %d to be
      ** field #4 (as in: cut -d' ' -f4). */;
  }
  if( g.bSqlOnly ){
    printf("/* %4d - %s%.*s */\n", iTestNum, zName, NAMEWIDTH-n, zDots);
  }else{
    printf("%4d - %s%.*s ", iTestNum, zName, NAMEWIDTH-n, zDots);
    fflush(stdout);
  }
  sqlite3_free(zName);
  g.nResult = 0;
  g.iStart = speedtest1_timestamp();
  g.x = 0xad131d0b;
  g.y = 0x44f9eac8;
}

/* Forward reference */
void speedtest1_exec(const char*,...);

/* Complete a test case */
void speedtest1_end_test(void){
  sqlite3_int64 iElapseTime = speedtest1_timestamp() - g.iStart;
  if( g.doCheckpoint ) speedtest1_exec("PRAGMA wal_checkpoint;");
  assert( iTestNumber > 0 );
  if( g.pScript ){
    fprintf(g.pScript,"-- end test %d\n", iTestNumber);
  }
  if( !g.bSqlOnly ){
    g.iTotal += iElapseTime;
    printf("%4d.%03ds\n", (int)(iElapseTime/1000), (int)(iElapseTime%1000));
    fflush(stdout);
  }
  if( g.pStmt ){
    sqlite3_finalize(g.pStmt);
    g.pStmt = 0;
  }
  iTestNumber = 0;
}

/* Report end of testing */
void speedtest1_final(void){
  if( !g.bSqlOnly ){
    printf("       TOTAL%.*s %4d.%03ds\n", NAMEWIDTH-5, zDots,
           (int)(g.iTotal/1000), (int)(g.iTotal%1000));
    fflush(stdout);
  }
  if( g.bVerify ){
#ifndef SPEEDTEST_OMIT_HASH
    int i;
#endif
    printf("Verification Hash: %llu ", g.nResByte);
#ifndef SPEEDTEST_OMIT_HASH
    HashUpdate((const unsigned char*)"\n", 1);
    HashFinal();
    for(i=0; i<24; i++){
      printf("%02x", g.hash.r[i]);
    }
    if( g.hashFile && g.hashFile!=stdout ) fclose(g.hashFile);
#endif
    printf("\n");
  }
}

/* Print an SQL statement to standard output */
static void printSql(const char *zSql){
  int n = (int)strlen(zSql);
  while( n>0 && (zSql[n-1]==';' || ISSPACE(zSql[n-1])) ){ n--; }
  if( g.bExplain ) printf("EXPLAIN ");
  printf("%.*s;\n", n, zSql);
  if( g.bExplain
#if SQLITE_VERSION_NUMBER>=3007017 
   && ( sqlite3_strglob("CREATE *", zSql)==0
     || sqlite3_strglob("DROP *", zSql)==0
     || sqlite3_strglob("ALTER *", zSql)==0
      )
#endif
  ){
    printf("%.*s;\n", n, zSql);
  }
}

/* Shrink memory used, if appropriate and if the SQLite version is capable
** of doing so.
*/
void speedtest1_shrink_memory(void){
#if SQLITE_VERSION_NUMBER>=3007010
  if( g.bMemShrink ) sqlite3_db_release_memory(g.db);
#endif
}

/* Run SQL */
void speedtest1_exec(const char *zFormat, ...){
  va_list ap;
  char *zSql;
  va_start(ap, zFormat);
  zSql = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  if( g.bSqlOnly ){
    printSql(zSql);
  }else{
    char *zErrMsg = 0;
    int rc;
    if( g.pScript ){
      fprintf(g.pScript,"%s;\n",zSql);
    }
    rc = sqlite3_exec(g.db, zSql, 0, 0, &zErrMsg);
    if( zErrMsg ) fatal_error("SQL error: %s\n%s\n", zErrMsg, zSql);
    if( rc!=SQLITE_OK ) fatal_error("exec error: %s\n", sqlite3_errmsg(g.db));
  }
  sqlite3_free(zSql);
  speedtest1_shrink_memory();
}

/* Run SQL and return the first column of the first row as a string.  The
** returned string is obtained from sqlite_malloc() and must be freed by
** the caller.
*/
char *speedtest1_once(const char *zFormat, ...){
  va_list ap;
  char *zSql;
  sqlite3_stmt *pStmt;
  char *zResult = 0;
  va_start(ap, zFormat);
  zSql = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  if( g.bSqlOnly ){
    printSql(zSql);
  }else{
    int rc = sqlite3_prepare_v2(g.db, zSql, -1, &pStmt, 0);
    if( rc ){
      fatal_error("SQL error: %s\n", sqlite3_errmsg(g.db));
    }
    if( g.pScript ){
      char *z = sqlite3_expanded_sql(pStmt);
      fprintf(g.pScript,"%s\n",z);
      sqlite3_free(z);
    }
    if( sqlite3_step(pStmt)==SQLITE_ROW ){
      const char *z = (const char*)sqlite3_column_text(pStmt, 0);
      if( z ) zResult = sqlite3_mprintf("%s", z);
    }
    sqlite3_finalize(pStmt);
  }
  sqlite3_free(zSql);
  speedtest1_shrink_memory();
  return zResult;
}

/* Prepare an SQL statement */
void speedtest1_prepare(const char *zFormat, ...){
  va_list ap;
  char *zSql;
  va_start(ap, zFormat);
  zSql = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  if( g.bSqlOnly ){
    printSql(zSql);
  }else{
    int rc;
    if( g.pStmt ) sqlite3_finalize(g.pStmt);
    rc = sqlite3_prepare_v2(g.db, zSql, -1, &g.pStmt, 0);
    if( rc ){
      fatal_error("SQL error: %s\n", sqlite3_errmsg(g.db));
    }
  }
  sqlite3_free(zSql);
}

/* Run an SQL statement previously prepared */
void speedtest1_run(void){
  int i, n, len;
  if( g.bSqlOnly ) return;
  assert( g.pStmt );
  g.nResult = 0;
  if( g.pScript ){
    char *z = sqlite3_expanded_sql(g.pStmt);
    fprintf(g.pScript,"%s\n",z);
    sqlite3_free(z);
  }
  while( sqlite3_step(g.pStmt)==SQLITE_ROW ){
    n = sqlite3_column_count(g.pStmt);
    for(i=0; i<n; i++){
      const char *z = (const char*)sqlite3_column_text(g.pStmt, i);
      if( z==0 ) z = "nil";
      len = (int)strlen(z);
#ifndef SPEEDTEST_OMIT_HASH
      if( g.bVerify ){
        int eType = sqlite3_column_type(g.pStmt, i);
        unsigned char zPrefix[2];
        zPrefix[0] = '\n';
        zPrefix[1] = "-IFTBN"[eType];
        if( g.nResByte ){
          HashUpdate(zPrefix, 2);
        }else{
          HashUpdate(zPrefix+1, 1);
        }
        if( eType==SQLITE_FLOAT ){
          /* Omit the value of floating-point results from the verification
          ** hash.  The only thing we record is the fact that the result was
          ** a floating-point value. */
          g.nResByte += 2;
        }else if( eType==SQLITE_BLOB ){
          int nBlob = sqlite3_column_bytes(g.pStmt, i);
          int iBlob;
          unsigned char zChar[2];
          const unsigned char *aBlob = sqlite3_column_blob(g.pStmt, i);
          for(iBlob=0; iBlob<nBlob; iBlob++){
            zChar[0] = "0123456789abcdef"[aBlob[iBlob]>>4];
            zChar[1] = "0123456789abcdef"[aBlob[iBlob]&15];
            HashUpdate(zChar,2);
          }
          g.nResByte += nBlob*2 + 2;
        }else{
          HashUpdate((unsigned char*)z, len);
          g.nResByte += len + 2;
        }
      }
#endif
      if( g.nResult+len<sizeof(g.zResult)-2 ){
        if( g.nResult>0 ) g.zResult[g.nResult++] = ' ';
        memcpy(g.zResult + g.nResult, z, len+1);
        g.nResult += len;
      }
    }
  }
#if SQLITE_VERSION_NUMBER>=3006001
  if( g.bReprepare ){
    sqlite3_stmt *pNew;
    sqlite3_prepare_v2(g.db, sqlite3_sql(g.pStmt), -1, &pNew, 0);
    sqlite3_finalize(g.pStmt);
    g.pStmt = pNew;
  }else
#endif
  {
    sqlite3_reset(g.pStmt);
  }
  speedtest1_shrink_memory();
}

#ifndef SQLITE_OMIT_DEPRECATED
/* The sqlite3_trace() callback function */
static void traceCallback(void *NotUsed, const char *zSql){
  int n = (int)strlen(zSql);
  while( n>0 && (zSql[n-1]==';' || ISSPACE(zSql[n-1])) ) n--;
  fprintf(stderr,"%.*s;\n", n, zSql);
}
#endif /* SQLITE_OMIT_DEPRECATED */

/* Substitute random() function that gives the same random
** sequence on each run, for repeatability. */
static void randomFunc(
  sqlite3_context *context,
  int NotUsed,
  sqlite3_value **NotUsed2
){
  sqlite3_result_int64(context, (sqlite3_int64)speedtest1_random());
}

/* Estimate the square root of an integer */
static int est_square_root(int x){
  int y0 = x/2;
  int y1;
  int n;
  for(n=0; y0>0 && n<10; n++){
    y1 = (y0 + x/y0)/2;
    if( y1==y0 ) break;
    y0 = y1;
  }
  return y0;
}


#if SQLITE_VERSION_NUMBER<3005004
/*
** An implementation of group_concat().  Used only when testing older
** versions of SQLite that lack the built-in group_concat().
*/
struct groupConcat {
  char *z;
  int nAlloc;
  int nUsed;
};
static void groupAppend(struct groupConcat *p, const char *z, int n){
  if( p->nUsed+n >= p->nAlloc ){
    int n2 = (p->nAlloc+n+1)*2;
    char *z2 = sqlite3_realloc(p->z, n2);
    if( z2==0 ) return;
    p->z = z2;
    p->nAlloc = n2;
  }
  memcpy(p->z+p->nUsed, z, n);
  p->nUsed += n;
}
static void groupStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zVal;
  struct groupConcat *p;
  const char *zSep;
  int nVal, nSep;
  assert( argc==1 || argc==2 );
  if( sqlite3_value_type(argv[0])==SQLITE_NULL ) return;
  p= (struct groupConcat*)sqlite3_aggregate_context(context, sizeof(*p));

  if( p ){
    int firstTerm = p->nUsed==0;
    if( !firstTerm ){
      if( argc==2 ){
        zSep = (char*)sqlite3_value_text(argv[1]);
        nSep = sqlite3_value_bytes(argv[1]);
      }else{
        zSep = ",";
        nSep = 1;
      }
      if( nSep ) groupAppend(p, zSep, nSep);
    }
    zVal = (char*)sqlite3_value_text(argv[0]);
    nVal = sqlite3_value_bytes(argv[0]);
    if( zVal ) groupAppend(p, zVal, nVal);
  }
}
static void groupFinal(sqlite3_context *context){
  struct groupConcat *p;
  p = sqlite3_aggregate_context(context, 0);
  if( p && p->z ){
    p->z[p->nUsed] = 0;
    sqlite3_result_text(context, p->z, p->nUsed, sqlite3_free);
  }
}
#endif

/*
** The main and default testset
*/
void testset_main(void){
  int i;                        /* Loop counter */
  int n;                        /* iteration count */
  int sz;                       /* Size of the tables */
  int maxb;                     /* Maximum swizzled value */
  unsigned x1 = 0, x2 = 0;      /* Parameters */
  int len = 0;                  /* Length of the zNum[] string */
  char zNum[2000];              /* A number name */

  sz = n = g.szTest*500;
  zNum[0] = 0;
  maxb = roundup_allones(sz);
  speedtest1_begin_test(100, "%d INSERTs into table with no index", n);
  speedtest1_exec("BEGIN");
  speedtest1_exec("CREATE%s TABLE z1(a INTEGER %s, b INTEGER %s, c TEXT %s);",
                  isTemp(9), g.zNN, g.zNN, g.zNN);
  speedtest1_prepare("INSERT INTO z1 VALUES(?1,?2,?3); --  %d times", n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    speedtest1_numbername(x1, zNum, sizeof(zNum));
    sqlite3_bind_int64(g.pStmt, 1, (sqlite3_int64)x1);
    sqlite3_bind_int(g.pStmt, 2, i);
    sqlite3_bind_text(g.pStmt, 3, zNum, -1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = sz;
  speedtest1_begin_test(110, "%d ordered INSERTS with one index/PK", n);
  speedtest1_exec("BEGIN");
  speedtest1_exec(
     "CREATE%s TABLE z2(a INTEGER %s %s, b INTEGER %s, c TEXT %s) %s",
     isTemp(5), g.zNN, g.zPK, g.zNN, g.zNN, g.zWR);
  speedtest1_prepare("INSERT INTO z2 VALUES(?1,?2,?3); -- %d times", n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    speedtest1_numbername(x1, zNum, sizeof(zNum));
    sqlite3_bind_int(g.pStmt, 1, i);
    sqlite3_bind_int64(g.pStmt, 2, (sqlite3_int64)x1);
    sqlite3_bind_text(g.pStmt, 3, zNum, -1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = sz;
  speedtest1_begin_test(120, "%d unordered INSERTS with one index/PK", n);
  speedtest1_exec("BEGIN");
  speedtest1_exec(
      "CREATE%s TABLE t3(a INTEGER %s %s, b INTEGER %s, c TEXT %s) %s",
      isTemp(3), g.zNN, g.zPK, g.zNN, g.zNN, g.zWR);
  speedtest1_prepare("INSERT INTO t3 VALUES(?1,?2,?3); -- %d times", n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    speedtest1_numbername(x1, zNum, sizeof(zNum));
    sqlite3_bind_int(g.pStmt, 2, i);
    sqlite3_bind_int64(g.pStmt, 1, (sqlite3_int64)x1);
    sqlite3_bind_text(g.pStmt, 3, zNum, -1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();

#if SQLITE_VERSION_NUMBER<3005004
  sqlite3_create_function(g.db, "group_concat", 1, SQLITE_UTF8, 0,
                          0, groupStep, groupFinal);
#endif

  n = 25;
  speedtest1_begin_test(130, "%d SELECTS, numeric BETWEEN, unindexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT count(*), avg(b), sum(length(c)), group_concat(c) FROM z1\n"
    " WHERE b BETWEEN ?1 AND ?2; -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = speedtest1_random()%maxb;
      x2 = speedtest1_random()%10 + sz/5000 + x1;
    }
    sqlite3_bind_int(g.pStmt, 1, x1);
    sqlite3_bind_int(g.pStmt, 2, x2);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = 10;
  speedtest1_begin_test(140, "%d SELECTS, LIKE, unindexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT count(*), avg(b), sum(length(c)), group_concat(c) FROM z1\n"
    " WHERE c LIKE ?1; -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = speedtest1_random()%maxb;
      zNum[0] = '%';
      len = speedtest1_numbername(i, zNum+1, sizeof(zNum)-2);
      zNum[len] = '%';
      zNum[len+1] = 0;
    }
    sqlite3_bind_text(g.pStmt, 1, zNum, len+1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = 10;
  speedtest1_begin_test(142, "%d SELECTS w/ORDER BY, unindexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT a, b, c FROM z1 WHERE c LIKE ?1\n"
    " ORDER BY a; -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = speedtest1_random()%maxb;
      zNum[0] = '%';
      len = speedtest1_numbername(i, zNum+1, sizeof(zNum)-2);
      zNum[len] = '%';
      zNum[len+1] = 0;
    }
    sqlite3_bind_text(g.pStmt, 1, zNum, len+1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();

  n = 10; /* g.szTest/5; */
  speedtest1_begin_test(145, "%d SELECTS w/ORDER BY and LIMIT, unindexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT a, b, c FROM z1 WHERE c LIKE ?1\n"
    " ORDER BY a LIMIT 10; -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = speedtest1_random()%maxb;
      zNum[0] = '%';
      len = speedtest1_numbername(i, zNum+1, sizeof(zNum)-2);
      zNum[len] = '%';
      zNum[len+1] = 0;
    }
    sqlite3_bind_text(g.pStmt, 1, zNum, len+1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  speedtest1_begin_test(150, "CREATE INDEX five times");
  speedtest1_exec("BEGIN;");
  speedtest1_exec("CREATE UNIQUE INDEX t1b ON z1(b);");
  speedtest1_exec("CREATE INDEX t1c ON z1(c);");
  speedtest1_exec("CREATE UNIQUE INDEX t2b ON z2(b);");
  speedtest1_exec("CREATE INDEX t2c ON z2(c DESC);");
  speedtest1_exec("CREATE INDEX t3bc ON t3(b,c);");
  speedtest1_exec("COMMIT;");
  speedtest1_end_test();


  n = sz/5;
  speedtest1_begin_test(160, "%d SELECTS, numeric BETWEEN, indexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT count(*), avg(b), sum(length(c)), group_concat(a) FROM z1\n"
    " WHERE b BETWEEN ?1 AND ?2; -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = speedtest1_random()%maxb;
      x2 = speedtest1_random()%10 + sz/5000 + x1;
    }
    sqlite3_bind_int(g.pStmt, 1, x1);
    sqlite3_bind_int(g.pStmt, 2, x2);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = sz/5;
  speedtest1_begin_test(161, "%d SELECTS, numeric BETWEEN, PK", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT count(*), avg(b), sum(length(c)), group_concat(a) FROM z2\n"
    " WHERE a BETWEEN ?1 AND ?2; -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = speedtest1_random()%maxb;
      x2 = speedtest1_random()%10 + sz/5000 + x1;
    }
    sqlite3_bind_int(g.pStmt, 1, x1);
    sqlite3_bind_int(g.pStmt, 2, x2);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  /* Disabled: SeekDB incorrectly converts TEXT to DECIMAL in BETWEEN comparison
  n = sz/5;
  speedtest1_begin_test(170, "%d SELECTS, text BETWEEN, indexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT count(*), avg(b), sum(length(c)), group_concat(a) FROM z1\n"
    " WHERE c BETWEEN ?1 AND (?1||'~'); -- %d times", n
  );
  for(i=1; i<=n; i++){
    if( (i-1)%g.nRepeat==0 ){
      x1 = swizzle(i, maxb);
      len = speedtest1_numbername(x1, zNum, sizeof(zNum)-1);
    }
    sqlite3_bind_text(g.pStmt, 1, zNum, len, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();
  */

  n = sz;
  speedtest1_begin_test(180, "%d INSERTS with three indexes", n);
  speedtest1_exec("BEGIN");
  speedtest1_exec(
    "CREATE%s TABLE t4(\n"
    "  a INTEGER %s %s,\n"
    "  b INTEGER %s,\n"
    "  c TEXT %s\n"
    ") %s",
    isTemp(1), g.zNN, g.zPK, g.zNN, g.zNN, g.zWR);
  speedtest1_exec("CREATE INDEX t4b ON t4(b)");
  speedtest1_exec("CREATE INDEX t4c ON t4(c)");
  speedtest1_exec("INSERT INTO t4 SELECT * FROM z1");
  speedtest1_exec("COMMIT");
  speedtest1_end_test();

  n = sz;
  speedtest1_begin_test(190, "DELETE and REFILL one table", n);
  speedtest1_exec("DELETE FROM z2;");
  speedtest1_exec("INSERT INTO z2 SELECT * FROM z1;");
  speedtest1_end_test();


  speedtest1_begin_test(200, "VACUUM");
  speedtest1_exec("VACUUM");
  speedtest1_end_test();


  speedtest1_begin_test(210, "ALTER TABLE ADD COLUMN, and query");
  speedtest1_exec("ALTER TABLE z2 ADD COLUMN d INT DEFAULT 123");
  speedtest1_exec("SELECT sum(d) FROM z2");
  speedtest1_end_test();


  n = sz/5;
  speedtest1_begin_test(230, "%d UPDATES, numeric BETWEEN, indexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "UPDATE z2 SET d=b*2 WHERE b BETWEEN ?1 AND ?2; -- %d times", n
  );
  for(i=1; i<=n; i++){
    x1 = speedtest1_random()%maxb;
    x2 = speedtest1_random()%10 + sz/5000 + x1;
    sqlite3_bind_int(g.pStmt, 1, x1);
    sqlite3_bind_int(g.pStmt, 2, x2);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = sz;
  speedtest1_begin_test(240, "%d UPDATES of individual rows", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "UPDATE z2 SET d=b*3 WHERE a=?1; -- %d times", n
  );
  for(i=1; i<=n; i++){
    x1 = speedtest1_random()%sz + 1;
    sqlite3_bind_int(g.pStmt, 1, x1);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();

  speedtest1_begin_test(250, "One big UPDATE of the whole %d-row table", sz);
  speedtest1_exec("UPDATE z2 SET d=b*4");
  speedtest1_end_test();


  speedtest1_begin_test(260, "Query added column after filling");
  speedtest1_exec("SELECT sum(d) FROM z2");
  speedtest1_end_test();



  n = sz/5;
  speedtest1_begin_test(270, "%d DELETEs, numeric BETWEEN, indexed", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "DELETE FROM z2 WHERE b BETWEEN ?1 AND ?2; -- %d times", n
  );
  for(i=1; i<=n; i++){
    x1 = speedtest1_random()%maxb + 1;
    x2 = speedtest1_random()%10 + sz/5000 + x1;
    sqlite3_bind_int(g.pStmt, 1, x1);
    sqlite3_bind_int(g.pStmt, 2, x2);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  n = sz;
  speedtest1_begin_test(280, "%d DELETEs of individual rows", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "DELETE FROM t3 WHERE a=?1; -- %d times", n
  );
  for(i=1; i<=n; i++){
    x1 = speedtest1_random()%sz + 1;
    sqlite3_bind_int(g.pStmt, 1, x1);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();


  speedtest1_begin_test(290, "Refill two %d-row tables using REPLACE", sz);
  speedtest1_exec("REPLACE INTO z2(a,b,c) SELECT a,b,c FROM z1");
  speedtest1_exec("REPLACE INTO t3(a,b,c) SELECT a,b,c FROM z1");
  speedtest1_end_test();

  speedtest1_begin_test(300, "Refill a %d-row table using (b&1)==(a&1)", sz);
  speedtest1_exec("DELETE FROM z2;");
  speedtest1_exec("INSERT INTO z2(a,b,c)\n"
                  " SELECT a,b,c FROM z1  WHERE (b&1)==(a&1);");
  speedtest1_exec("INSERT INTO z2(a,b,c)\n"
                  " SELECT a,b,c FROM z1  WHERE (b&1)<>(a&1);");
  speedtest1_end_test();


  n = sz/5;
  speedtest1_begin_test(310, "%d four-ways joins", n);
  speedtest1_exec("BEGIN");
  speedtest1_prepare(
    "SELECT z1.c FROM z1, z2, t3, t4\n"
    " WHERE t4.a BETWEEN ?1 AND ?2\n"
    "   AND t3.a=t4.b\n"
    "   AND z2.a=t3.b\n"
    "   AND z1.c=z2.c;"
  );
  for(i=1; i<=n; i++){
    x1 = speedtest1_random()%sz + 1;
    x2 = speedtest1_random()%10 + x1 + 4;
    sqlite3_bind_int(g.pStmt, 1, x1);
    sqlite3_bind_int(g.pStmt, 2, x2);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();

  /* test 320 (subquery with rowid) removed — rowid is SQLite-specific */

  sz = n = g.szTest*700;
  zNum[0] = 0;
  maxb = roundup_allones(sz/3);
  speedtest1_begin_test(400, "%d REPLACE ops on an IPK", n);
  speedtest1_exec("BEGIN");
  speedtest1_exec("CREATE%s TABLE t5(a INTEGER PRIMARY KEY, b %s);",
                  isTemp(9), g.zNN);
  speedtest1_prepare("REPLACE INTO t5 VALUES(?1,?2); --  %d times",n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    speedtest1_numbername(i, zNum, sizeof(zNum));
    sqlite3_bind_int(g.pStmt, 1, (sqlite3_int64)x1);
    sqlite3_bind_text(g.pStmt, 2, zNum, -1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();
  speedtest1_begin_test(410, "%d SELECTS on an IPK", n);
  if( g.doBigTransactions ){
    /* Historical note: tests 410 and 510 have historically not used
    ** explicit transactions. The --big-transactions flag was added
    ** 2022-09-08 to support the WASM/OPFS build, as the run-times
    ** approach 1 minute for each of these tests if they're not in an
    ** explicit transaction. The run-time effect of --big-transaciions
    ** on native builds is negligible. */
    speedtest1_exec("BEGIN");
  }
  speedtest1_prepare("SELECT b FROM t5 WHERE a=?1; --  %d times",n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    sqlite3_bind_int(g.pStmt, 1, (sqlite3_int64)x1);
    speedtest1_run();
  }
  if( g.doBigTransactions ){
    speedtest1_exec("COMMIT");
  }
  speedtest1_end_test();

  sz = n = g.szTest*700;
  zNum[0] = 0;
  maxb = roundup_allones(sz/3);
  speedtest1_begin_test(500, "%d REPLACE on TEXT PK", n);
  speedtest1_exec("BEGIN");
  speedtest1_exec("CREATE%s TABLE t6(a TEXT PRIMARY KEY, b %s)%s;",
                  isTemp(9), g.zNN,
                  sqlite3_libversion_number()>=3008002 ? "WITHOUT ROWID" : "");
  speedtest1_prepare("REPLACE INTO t6 VALUES(?1,?2); --  %d times",n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    speedtest1_numbername(x1, zNum, sizeof(zNum));
    sqlite3_bind_int(g.pStmt, 2, i);
    sqlite3_bind_text(g.pStmt, 1, zNum, -1, SQLITE_STATIC);
    speedtest1_run();
  }
  speedtest1_exec("COMMIT");
  speedtest1_end_test();
  speedtest1_begin_test(510, "%d SELECTS on a TEXT PK", n);
  if( g.doBigTransactions ){
    /* See notes for test 410. */
    speedtest1_exec("BEGIN");
  }
  speedtest1_prepare("SELECT b FROM t6 WHERE a=?1; --  %d times",n);
  for(i=1; i<=n; i++){
    x1 = swizzle(i,maxb);
    speedtest1_numbername(x1, zNum, sizeof(zNum));
    sqlite3_bind_text(g.pStmt, 1, zNum, -1, SQLITE_STATIC);
    speedtest1_run();
  }
  if( g.doBigTransactions ){
    speedtest1_exec("COMMIT");
  }
  speedtest1_end_test();
  speedtest1_begin_test(520, "%d SELECT DISTINCT", n);
  speedtest1_exec("SELECT DISTINCT b FROM t5;");
  speedtest1_exec("SELECT DISTINCT b FROM t6;");
  speedtest1_end_test();
}


static int xCompileOptions(void *pCtx, int nVal, char **azVal, char **azCol){
  printf("-- Compile option: %s\n", azVal[0]);
  return SQLITE_OK;
}
int main(int argc, char **argv){
  int doAutovac = 0;            /* True for --autovacuum */
  int cacheSize = 0;            /* Desired cache size.  0 means default */
  int doExclusive = 0;          /* True for --exclusive */
  int doFullFSync = 0;          /* True for --fullfsync */
  int nHeap = 0, mnHeap = 0;    /* Heap size from --heap */
  int doIncrvac = 0;            /* True for --incrvacuum */
  const char *zJMode = "wal";   /* Journal mode (default WAL for durability) */
  const char *zKey = 0;         /* Encryption key */
  int nLook = -1, szLook = 0;   /* --lookaside configuration */
  int noSync = 0;               /* True for --nosync */
  int pageSize = 0;             /* Desired page size.  0 means default */
  int nPCache = 0, szPCache = 0;/* --pcache configuration */
  int doPCache = 0;             /* True if --pcache is seen */
  int showStats = 0;            /* True for --stats */
  int nThread = 0;              /* --threads value */
  int mmapSize = 0;             /* How big of a memory map to use */
  int memDb = 0;                /* --memdb.  Use an in-memory database */
  int openFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
    ;                           /* SQLITE_OPEN_xxx flags. */
  char *zTSet = "main";         /* Which --testset torun */
  const char * zVfs = 0;        /* --vfs NAME */
  int doTrace = 0;              /* True for --trace */
  const char *zEncoding = 0;    /* --utf16be or --utf16le */
  const char *zDbName = 0;      /* Name of the test database */

  void *pHeap = 0;              /* Allocated heap space */
  void *pLook = 0;              /* Allocated lookaside space */
  void *pPCache = 0;            /* Allocated storage for pcache */
  int iCur, iHi;                /* Stats values, current and "highwater" */
  int i;                        /* Loop counter */
  int rc;                       /* API return code */

#ifdef SQLITE_SPEEDTEST1_WASM
  /* Resetting all state is important for the WASM build, which may
  ** call main() multiple times. */
  memset(&g, 0, sizeof(g));
  iTestNumber = 0;
#endif
#ifdef SQLITE_CKSUMVFS_STATIC
  sqlite3_register_cksumvfs(0);
#endif
  /*
  ** Confirms that argc has at least N arguments following argv[i]. */
#define ARGC_VALUE_CHECK(N)                                       \
  if( i>=argc-(N) ) fatal_error("missing argument on %s\n", argv[i])
  /* Debug output */
  fprintf(stderr, "DEBUG: speedtest1_main started, argc=%d\n", argc);
  fflush(stderr);
  /* Display the version of SQLite being tested */
  printf("-- Speedtest1 for %s %s %.48s\n",
         SPEEDTEST_BACKEND_NAME, sqlite3_libversion(), sqlite3_sourceid());
  fflush(stdout);
  fprintf(stderr, "DEBUG: after printf version\n");
  fflush(stderr);

  /* Process command-line arguments */
  fprintf(stderr, "DEBUG: setting g.zWR\n");
  fflush(stderr);
  g.zWR = "";
  fprintf(stderr, "DEBUG: setting g.zNN\n");
  fflush(stderr);
  g.zNN = "";
  fprintf(stderr, "DEBUG: setting g.zPK\n");
  fflush(stderr);
  g.zPK = "UNIQUE";
  fprintf(stderr, "DEBUG: setting g.szTest\n");
  fflush(stderr);
  g.szTest = 100;
  fprintf(stderr, "DEBUG: setting g.szBase\n");
  fflush(stderr);
  g.szBase = 100;
  fprintf(stderr, "DEBUG: setting g.nRepeat\n");
  fflush(stderr);
  g.nRepeat = 1;
  fprintf(stderr, "DEBUG: entering argument loop\n");
  fflush(stderr);
  for(i=1; i<argc; i++){
    fprintf(stderr, "DEBUG: processing argv[%d]=%s\n", i, argv[i]);
    fflush(stderr);
    const char *z = argv[i];
    if( z[0]=='-' ){
      do{ z++; }while( z[0]=='-' );
      if( strcmp(z,"autovacuum")==0 ){
        doAutovac = 1;
      }else if( strcmp(z,"big-transactions")==0 ){
        g.doBigTransactions = 1;
      }else if( strcmp(z,"cachesize")==0 ){
        ARGC_VALUE_CHECK(1);
        cacheSize = integerValue(argv[++i]);
      }else if( strcmp(z,"exclusive")==0 ){
        doExclusive = 1;
      }else if( strcmp(z,"fullfsync")==0 ){
        doFullFSync = 1;
      }else if( strcmp(z,"checkpoint")==0 ){
        g.doCheckpoint = 1;
      }else if( strcmp(z,"explain")==0 ){
        g.bSqlOnly = 1;
        g.bExplain = 1;
      }else if( strcmp(z,"heap")==0 ){
        ARGC_VALUE_CHECK(2);
        nHeap = integerValue(argv[i+1]);
        mnHeap = integerValue(argv[i+2]);
        i += 2;
      }else if( strcmp(z,"incrvacuum")==0 ){
        doIncrvac = 1;
      }else if( strcmp(z,"journal")==0 ){
        ARGC_VALUE_CHECK(1);
        zJMode = argv[++i];
      }else if( strcmp(z,"key")==0 ){
        ARGC_VALUE_CHECK(1);
        zKey = argv[++i];
      }else if( strcmp(z,"lookaside")==0 ){
        ARGC_VALUE_CHECK(2);
        nLook = integerValue(argv[i+1]);
        szLook = integerValue(argv[i+2]);
        i += 2;
      }else if( strcmp(z,"memdb")==0 ){
        memDb = 1;
#if SQLITE_VERSION_NUMBER>=3006000
      }else if( strcmp(z,"multithread")==0 ){
        sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
      }else if( strcmp(z,"nomemstat")==0 ){
        sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0);
#endif
#if SQLITE_VERSION_NUMBER>=3007017
      }else if( strcmp(z, "mmap")==0 ){
        ARGC_VALUE_CHECK(1);
        mmapSize = integerValue(argv[++i]);
 #endif
      }else if( strcmp(z,"nomutex")==0 ){
        openFlags |= SQLITE_OPEN_NOMUTEX;
      }else if( strcmp(z,"nosync")==0 ){
        noSync = 1;
      }else if( strcmp(z,"notnull")==0 ){
        g.zNN = "NOT NULL";
      }else if( strcmp(z,"output")==0 ){
#ifdef SPEEDTEST_OMIT_HASH
        fatal_error("The --output option is not supported with"
                    " -DSPEEDTEST_OMIT_HASH\n");
#else
        ARGC_VALUE_CHECK(1);
        i++;
        if( strcmp(argv[i],"-")==0 ){
          g.hashFile = stdout;
        }else{
          g.hashFile = fopen(argv[i], "wb");
          if( g.hashFile==0 ){
            fatal_error("cannot open \"%s\" for writing\n", argv[i]);
          }
        }
#endif
      }else if( strcmp(z,"pagesize")==0 ){
        ARGC_VALUE_CHECK(1);
        pageSize = integerValue(argv[++i]);
      }else if( strcmp(z,"pcache")==0 ){
        ARGC_VALUE_CHECK(2);
        nPCache = integerValue(argv[i+1]);
        szPCache = integerValue(argv[i+2]);
        doPCache = 1;
        i += 2;
      }else if( strcmp(z,"primarykey")==0 ){
        g.zPK = "PRIMARY KEY";
      }else if( strcmp(z,"repeat")==0 ){
        ARGC_VALUE_CHECK(1);
        g.nRepeat = integerValue(argv[++i]);
      }else if( strcmp(z,"reprepare")==0 ){
        g.bReprepare = 1;
#if SQLITE_VERSION_NUMBER>=3006000
      }else if( strcmp(z,"serialized")==0 ){
        sqlite3_config(SQLITE_CONFIG_SERIALIZED);
      }else if( strcmp(z,"singlethread")==0 ){
        sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
#endif
      }else if( strcmp(z,"script")==0 ){
        ARGC_VALUE_CHECK(1);
        if( g.pScript ) fclose(g.pScript);
        g.pScript = fopen(argv[++i], "wb");
        if( g.pScript==0 ){
          fatal_error("unable to open output file \"%s\"\n", argv[i]);
        }
      }else if( strcmp(z,"sqlonly")==0 ){
        g.bSqlOnly = 1;
      }else if( strcmp(z,"shrink-memory")==0 ){
        g.bMemShrink = 1;
      }else if( strcmp(z,"size")==0 ){
        ARGC_VALUE_CHECK(1);
        g.szTest = g.szBase = integerValue(argv[++i]);
      }else if( strcmp(z,"stats")==0 ){
        showStats = 1;
      }else if( strcmp(z,"temp")==0 ){
        ARGC_VALUE_CHECK(1);
        i++;
        if( argv[i][0]<'0' || argv[i][0]>'9' || argv[i][1]!=0 ){
          fatal_error("argument to --temp should be integer between 0 and 9");
        }
        g.eTemp = argv[i][0] - '0';
      }else if( strcmp(z,"testset")==0 ){
        ARGC_VALUE_CHECK(1);
        zTSet = argv[++i];
      }else if( strcmp(z,"trace")==0 ){
        doTrace = 1;
      }else if( strcmp(z,"threads")==0 ){
        ARGC_VALUE_CHECK(1);
        nThread = integerValue(argv[++i]);
      }else if( strcmp(z,"utf16le")==0 ){
        zEncoding = "utf16le";
      }else if( strcmp(z,"utf16be")==0 ){
        zEncoding = "utf16be";
      }else if( strcmp(z,"verify")==0 ){
        g.bVerify = 1;
#ifndef SPEEDTEST_OMIT_HASH
        HashInit();
#endif
      }else if( strcmp(z,"vfs")==0 ){
        ARGC_VALUE_CHECK(1);
        zVfs = argv[++i];
      }else if( strcmp(z,"stmtscanstatus")==0 ){
        g.stmtScanStatus = 1;
      }else if( strcmp(z,"without-rowid")==0 ){
        if( strstr(g.zWR,"WITHOUT")!=0 ){
          /* no-op */
        }else if( strstr(g.zWR,"STRICT")!=0 ){
          g.zWR = "WITHOUT ROWID,STRICT";
        }else{
          g.zWR = "WITHOUT ROWID";
        }
        g.zPK = "PRIMARY KEY";
      }else if( strcmp(z,"strict")==0 ){
        if( strstr(g.zWR,"STRICT")!=0 ){
          /* no-op */
        }else if( strstr(g.zWR,"WITHOUT")!=0 ){
          g.zWR = "WITHOUT ROWID,STRICT";
        }else{
          g.zWR = "STRICT";
        }
      }else if( strcmp(z, "help")==0 || strcmp(z,"?")==0 ){
        printf(zHelp, argv[0]);
        exit(0);
      }else{
        fatal_error("unknown option: %s\nUse \"%s -?\" for help\n",
                    argv[i], argv[0]);
      }
    }else if( zDbName==0 ){
      fprintf(stderr, "DEBUG: setting zDbName to argv[%d]=%s\n", i, argv[i]);
      fflush(stderr);
      zDbName = argv[i];
    }else{
      fatal_error("surplus argument: %s\nUse \"%s -?\" for help\n",
                  argv[i], argv[0]);
    }
  }
#undef ARGC_VALUE_CHECK
  fprintf(stderr, "DEBUG: argument loop done, zDbName=%s\n", zDbName ? zDbName : "NULL");
  fflush(stderr);
#if SQLITE_VERSION_NUMBER>=3006001
  if( nHeap>0 ){
    pHeap = malloc( nHeap );
    if( pHeap==0 ) fatal_error("cannot allocate %d-byte heap\n", nHeap);
    rc = sqlite3_config(SQLITE_CONFIG_HEAP, pHeap, nHeap, mnHeap);
    if( rc ) fatal_error("heap configuration failed: %d\n", rc);
  }
  if( doPCache ){
    if( nPCache>0 && szPCache>0 ){
      pPCache = malloc( nPCache*(sqlite3_int64)szPCache );
      if( pPCache==0 ) fatal_error("cannot allocate %lld-byte pcache\n",
                                   nPCache*(sqlite3_int64)szPCache);
    }
    rc = sqlite3_config(SQLITE_CONFIG_PAGECACHE, pPCache, szPCache, nPCache);
    if( rc ) fatal_error("pcache configuration failed: %d\n", rc);
  }
  if( nLook>=0 ){
    sqlite3_config(SQLITE_CONFIG_LOOKASIDE, 0, 0);
  }
#endif
  sqlite3_initialize();

  if( zDbName!=0 ){
    sqlite3_vfs *pVfs = sqlite3_vfs_find(zVfs);
    /* For some VFSes, e.g. opfs, unlink() is not sufficient. Use the
    ** selected (or default) VFS's xDelete method to delete the
    ** database. This is specifically important for the "opfs" VFS
    ** when running from a WASM build of speedtest1, so that the db
    ** can be cleaned up properly. For historical compatibility, we'll
    ** also simply unlink(). */
    if( pVfs!=0 ){
      pVfs->xDelete(pVfs, zDbName, 1);
    }
    unlink(zDbName);
  }

  /* Open the database and the input file */
  if( sqlite3_open_v2(memDb ? ":memory:" : zDbName, &g.db,
                      openFlags, zVfs) ){
    fatal_error("Cannot open database file: %s\n", zDbName);
  }
#if SQLITE_VERSION_NUMBER>=3006001
  if( nLook>0 && szLook>0 ){
    pLook = malloc( nLook*szLook );
    rc = sqlite3_db_config(g.db, SQLITE_DBCONFIG_LOOKASIDE,pLook,szLook,nLook);
    if( rc ) fatal_error("lookaside configuration failed: %d\n", rc);
  }
#endif
#ifdef SQLITE_DBCONFIG_STMT_SCANSTATUS
  if( g.stmtScanStatus ){
    sqlite3_db_config(g.db, SQLITE_DBCONFIG_STMT_SCANSTATUS, 1, 0);
  }
#endif

  /* Set database connection options */
  sqlite3_create_function(g.db, "random", 0, SQLITE_UTF8, 0, randomFunc, 0, 0);
#ifndef SQLITE_OMIT_DEPRECATED
  if( doTrace ) sqlite3_trace(g.db, traceCallback, 0);
#endif
  if( memDb>0 ){
    speedtest1_exec("PRAGMA temp_store=memory");
  }
  if( mmapSize>0 ){
    speedtest1_exec("PRAGMA mmap_size=%d", mmapSize);
  }
  speedtest1_exec("PRAGMA threads=%d", nThread);
  if( zKey ){
    speedtest1_exec("PRAGMA key('%s')", zKey);
  }
  if( zEncoding ){
    speedtest1_exec("PRAGMA encoding=%s", zEncoding);
  }
  if( doAutovac ){
    speedtest1_exec("PRAGMA auto_vacuum=FULL");
  }else if( doIncrvac ){
    speedtest1_exec("PRAGMA auto_vacuum=INCREMENTAL");
  }
  if( pageSize ){
    speedtest1_exec("PRAGMA page_size=%d", pageSize);
  }
  if( cacheSize ){
    speedtest1_exec("PRAGMA cache_size=%d", cacheSize);
  }
  if( noSync ){
    speedtest1_exec("PRAGMA synchronous=OFF");
  }else if( doFullFSync ){
    speedtest1_exec("PRAGMA fullfsync=ON");
  }
  if( doExclusive ){
    speedtest1_exec("PRAGMA locking_mode=EXCLUSIVE");
  }
  if( zJMode ){
    speedtest1_exec("PRAGMA journal_mode=%s", zJMode);
  }

  if( g.bExplain ) printf(".explain\n.echo on\n");
  if( strcmp(zTSet,"main")!=0 ){
    fatal_error("unknown testset: \"%s\"\nOnly \"main\" is supported.\n", zTSet);
  }
  testset_main();
  speedtest1_final();

  if( showStats ){
    sqlite3_exec(g.db, "PRAGMA compile_options", xCompileOptions, 0, 0);
  }

  /* Database connection statistics printed after both prepared statements
  ** have been finalized */
#if SQLITE_VERSION_NUMBER>=3007009
  if( showStats ){
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_USED, &iCur, &iHi, 0);
    printf("-- Lookaside Slots Used:        %d (max %d)\n", iCur,iHi);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_HIT, &iCur, &iHi, 0);
    printf("-- Successful lookasides:       %d\n", iHi);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE, &iCur,&iHi,0);
    printf("-- Lookaside size faults:       %d\n", iHi);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL, &iCur,&iHi,0);
    printf("-- Lookaside OOM faults:        %d\n", iHi);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_CACHE_USED, &iCur, &iHi, 0);
    printf("-- Pager Heap Usage:            %d bytes\n", iCur);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_CACHE_HIT, &iCur, &iHi, 1);
    printf("-- Page cache hits:             %d\n", iCur);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_CACHE_MISS, &iCur, &iHi, 1);
    printf("-- Page cache misses:           %d\n", iCur);
#if SQLITE_VERSION_NUMBER>=3007012
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_CACHE_WRITE, &iCur, &iHi, 1);
    printf("-- Page cache writes:           %d\n", iCur); 
#endif
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_SCHEMA_USED, &iCur, &iHi, 0);
    printf("-- Schema Heap Usage:           %d bytes\n", iCur); 
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_STMT_USED, &iCur, &iHi, 0);
    printf("-- Statement Heap Usage:        %d bytes\n", iCur); 
  }
#endif

  sqlite3_close(g.db);

#if SQLITE_VERSION_NUMBER>=3006001
  /* Global memory usage statistics printed after the database connection
  ** has closed.  Memory usage should be zero at this point. */
  if( showStats ){
    sqlite3_status(SQLITE_STATUS_MEMORY_USED, &iCur, &iHi, 0);
    printf("-- Memory Used (bytes):         %d (max %d)\n", iCur,iHi);
#if SQLITE_VERSION_NUMBER>=3007000
    sqlite3_status(SQLITE_STATUS_MALLOC_COUNT, &iCur, &iHi, 0);
    printf("-- Outstanding Allocations:     %d (max %d)\n", iCur,iHi);
#endif
    sqlite3_status(SQLITE_STATUS_PAGECACHE_OVERFLOW, &iCur, &iHi, 0);
    printf("-- Pcache Overflow Bytes:       %d (max %d)\n", iCur,iHi);
    sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &iCur, &iHi, 0);
    printf("-- Largest Allocation:          %d bytes\n",iHi);
    sqlite3_status(SQLITE_STATUS_PAGECACHE_SIZE, &iCur, &iHi, 0);
    printf("-- Largest Pcache Allocation:   %d bytes\n",iHi);
  }
#endif

  if( g.pScript ){
    fclose(g.pScript);
  }

  /* Release memory */
  free( pLook );
  free( pPCache );
  free( pHeap );

  _exit(0);
}

#ifdef SQLITE_SPEEDTEST1_WASM
/*
** A workaround for some inconsistent behaviour with how
** main() does (or does not) get exported to WASM.
*/
int wasm_main(int argc, char **argv){
  return main(argc, argv);
}
#endif
