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
 * backends/seekdb_backend.h — SeekDB backend for speedtest1 benchmark.
 *
 * Maps SQLite API calls to SeekDB equivalents.
 * Based on seekdb_sqlite_compat.h; enhancements:
 *   - Defines SPEEDTEST_BACKEND_NAME
 *   - sqlite3_prepare_v2 applies rewrite_sql_for_seekdb to handle syntax differences
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  /* for strcasestr */
#endif

#ifndef SEEKDB_BACKEND_H
#define SEEKDB_BACKEND_H
#include "seekdb.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>


#define SPEEDTEST_BACKEND_NAME "SeekDB"

/* Pretend to be SQLite 3.39.0 to avoid compatibility issues */
#ifndef SQLITE_VERSION_NUMBER
#define SQLITE_VERSION_NUMBER 3039001
#endif
#ifndef SQLITE_VERSION
#define SQLITE_VERSION "3.39.0"
#endif

/* SQLite return codes */
#define SQLITE_OK           0
#define SQLITE_ERROR        1
#define SQLITE_ROW          100
#define SQLITE_DONE         101

/* SQLite column types */
#define SQLITE_INTEGER      1
#define SQLITE_FLOAT        2
#define SQLITE_BLOB         4
#define SQLITE_NULL         5
#define SQLITE_TEXT         3

/* Open flags */
#define SQLITE_OPEN_READWRITE   0x00000002
#define SQLITE_OPEN_CREATE      0x00000004
#define SQLITE_OPEN_NOMUTEX     0x00008000

/* DB config constants */
#define SQLITE_DBCONFIG_LOOKASIDE       1001
#define SQLITE_DBCONFIG_STMT_SCANSTATUS 1004

/* DB status constants */
#define SQLITE_DBSTATUS_LOOKASIDE_USED       0
#define SQLITE_DBSTATUS_LOOKASIDE_HIT        1
#define SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE  2
#define SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL  3
#define SQLITE_DBSTATUS_CACHE_USED           4
#define SQLITE_DBSTATUS_SCHEMA_USED          5
#define SQLITE_DBSTATUS_STMT_USED            6
#define SQLITE_DBSTATUS_CACHE_HIT            7
#define SQLITE_DBSTATUS_CACHE_MISS           8
#define SQLITE_DBSTATUS_CACHE_WRITE          9

/* Status constants */
#define SQLITE_STATUS_MEMORY_USED            0
#define SQLITE_STATUS_PAGECACHE_USED         1
#define SQLITE_STATUS_PAGECACHE_OVERFLOW     2
#define SQLITE_STATUS_SCRATCH_USED           3
#define SQLITE_STATUS_SCRATCH_OVERFLOW       4
#define SQLITE_STATUS_MALLOC_SIZE            5
#define SQLITE_STATUS_PARSER_STACK           6
#define SQLITE_STATUS_PAGECACHE_SIZE         7
#define SQLITE_STATUS_SCRATCH_SIZE           8
#define SQLITE_STATUS_MALLOC_COUNT           9

/* File control constants */
#define SQLITE_FCNTL_RESERVE_BYTES      18

/* Test control constants */
#define SQLITE_TESTCTRL_PRNG_SEED       21

/* Integer types */
#define sqlite3_int64 long long
#define sqlite3_uint64 unsigned long long
#define sqlite_int64 long long
#define sqlite_uint64 unsigned long long

/* VFS structure - minimal definition for compilation */
typedef struct sqlite3_vfs sqlite3_vfs;
struct sqlite3_vfs {
  int iVersion;
  int szOsFile;
  int mxPathname;
  sqlite3_vfs *pNext;
  const char *zName;
  void *pAppData;
  int (*xOpen)(sqlite3_vfs*, const char*, void*, int, int*);
  int (*xDelete)(sqlite3_vfs*, const char*, int);
  int (*xAccess)(sqlite3_vfs*, const char*, int, int*);
  int (*xFullPathname)(sqlite3_vfs*, const char*, int, char*);
  void *(*xDlOpen)(sqlite3_vfs*, const char*);
  void (*xDlError)(sqlite3_vfs*, int, char*);
  void (*(*xDlSym)(sqlite3_vfs*, void*, const char*))(void);
  void (*xDlClose)(sqlite3_vfs*, void*);
  int (*xRandomness)(sqlite3_vfs*, int, char*);
  int (*xSleep)(sqlite3_vfs*, int);
  int (*xCurrentTime)(sqlite3_vfs*, double*);
  int (*xGetLastError)(sqlite3_vfs*, int, char*);
  int (*xCurrentTimeInt64)(sqlite3_vfs*, long long*);
};

/* Shared instance: one seekdb_open per db path, multiple connections */
#include <pthread.h>

#define SEEKDB_MAX_INSTANCES 8

typedef struct {
    char              path[512];
    seekdb_handle     db;
    int               refcount;
} seekdb_instance;

static seekdb_instance g_instances[SEEKDB_MAX_INSTANCES];
static int             g_instance_count = 0;
static pthread_mutex_t g_instance_lock = PTHREAD_MUTEX_INITIALIZER;

/* Find or create a shared instance for the given path.
 * Returns the seekdb_handle; caller must hold no lock. */
static inline seekdb_handle seekdb_instance_acquire(const char* path) {
    pthread_mutex_lock(&g_instance_lock);
    /* find existing */
    for (int i = 0; i < g_instance_count; i++) {
        if (strcmp(g_instances[i].path, path) == 0) {
            g_instances[i].refcount++;
            seekdb_handle h = g_instances[i].db;
            pthread_mutex_unlock(&g_instance_lock);
            return h;
        }
    }
    /* create new — must do seekdb_open under lock to prevent double-open */
    if (g_instance_count >= SEEKDB_MAX_INSTANCES) {
        pthread_mutex_unlock(&g_instance_lock);
        return NULL;
    }
    seekdb_instance *inst = &g_instances[g_instance_count];

    /* seekdb_open hijacks stdout; save and restore */
    int saved_fd = fcntl(1, F_DUPFD, 100);
    fflush(stdout);

    int ret = seekdb_open(path, &inst->db);

    if (saved_fd >= 0) {
        char proc_path[64];
        snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", saved_fd);
        if (!freopen(proc_path, "w", stdout))
            dup2(saved_fd, 1);
        close(saved_fd);
    }

    if (ret != 0) {
        pthread_mutex_unlock(&g_instance_lock);
        return NULL;
    }
    snprintf(inst->path, sizeof(inst->path), "%s", path);
    inst->refcount = 1;
    g_instance_count++;
    seekdb_handle h = inst->db;
    pthread_mutex_unlock(&g_instance_lock);
    return h;
}

/* Release a reference; closes instance when refcount hits 0. */
static inline void seekdb_instance_release(seekdb_handle db) {
    pthread_mutex_lock(&g_instance_lock);
    for (int i = 0; i < g_instance_count; i++) {
        if (g_instances[i].db == db) {
            if (--g_instances[i].refcount <= 0) {
                seekdb_close(g_instances[i].db);
                g_instances[i] = g_instances[--g_instance_count];
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_instance_lock);
}

/* Database handle - wrapper around seekdb handles */
typedef struct sqlite3 sqlite3;
struct sqlite3 {
  seekdb_conn_handle conn;
  seekdb_handle db;
};

/* Statement handle - wrapper around seekdb_stmt_handle */
typedef struct sqlite3_stmt sqlite3_stmt;
struct sqlite3_stmt {
  seekdb_stmt_handle handle;
};

/* Context types - stubs */
typedef void sqlite3_context;
typedef void sqlite3_value;
typedef struct sqlite3_rtree_geometry sqlite3_rtree_geometry;

/* Helper: check if a word at position p is a type keyword */
static inline int is_type_keyword(const char* p) {
    if (strncasecmp(p, "INTEGER", 7) == 0) return 7;
    if (strncasecmp(p, "INT", 3) == 0) return 3;
    if (strncasecmp(p, "VARCHAR", 7) == 0) return 7;
    if (strncasecmp(p, "CHAR", 4) == 0) return 4;
    if (strncasecmp(p, "TEXT", 4) == 0) return 4;
    if (strncasecmp(p, "REAL", 4) == 0) return 4;
    if (strncasecmp(p, "BLOB", 4) == 0) return 4;
    if (strncasecmp(p, "FLOAT", 5) == 0) return 5;
    if (strncasecmp(p, "DOUBLE", 6) == 0) return 6;
    if (strncasecmp(p, "BOOLEAN", 7) == 0) return 7;
    if (strncasecmp(p, "BIGINT", 6) == 0) return 6;
    if (strncasecmp(p, "SMALLINT", 8) == 0) return 8;
    if (strncasecmp(p, "TINYINT", 7) == 0) return 7;
    if (strncasecmp(p, "NUMERIC", 7) == 0) return 7;
    if (strncasecmp(p, "DECIMAL", 7) == 0) return 7;
    return 0;
}

/* Helper: check if a word at position p is a constraint keyword */
static inline int is_constraint_keyword(const char* p) {
    if (strncasecmp(p, "PRIMARY", 7) == 0) return 7;
    if (strncasecmp(p, "KEY", 3) == 0) return 3;
    if (strncasecmp(p, "NOT", 3) == 0) return 3;
    if (strncasecmp(p, "NULL", 4) == 0) return 4;
    if (strncasecmp(p, "UNIQUE", 6) == 0) return 6;
    if (strncasecmp(p, "DEFAULT", 7) == 0) return 7;
    if (strncasecmp(p, "CHECK", 5) == 0) return 5;
    if (strncasecmp(p, "REFERENCES", 10) == 0) return 10;
    if (strncasecmp(p, "COLLATE", 7) == 0) return 7;
    if (strncasecmp(p, "GENERATED", 9) == 0) return 9;
    if (strncasecmp(p, "AUTOINCREMENT", 13) == 0) return 13;
    return 0;
}

/* Helper: rewrite CREATE TABLE - TEXT to VARCHAR(1024), add types to untyped columns */
static inline char* rewrite_create_table(const char* sql) {
    if (!sql) return NULL;

    const char* p = sql;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
    if (strncasecmp(p, "CREATE", 6) != 0) return NULL;
    p += 6;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (strncasecmp(p, "TABLE", 5) != 0) return NULL;

    const char* paren = strchr(sql, '(');
    if (!paren) return NULL;

    size_t len = strlen(sql);
    char* new_sql = (char*)malloc(len * 3 + 1);
    if (!new_sql) return NULL;

    char* out = new_sql;
    const char* in = sql;
    while (in <= paren) {
        *out++ = *in++;
    }

    int paren_depth = 1;
    int in_string = 0;
    char string_char = 0;

    while (*in) {
        if (in_string) {
            *out++ = *in++;
            if (*in == string_char && *(in-1) != '\\') {
                in_string = 0;
            }
            continue;
        }

        if (*in == '\'' || *in == '"') {
            in_string = 1;
            string_char = *in;
            *out++ = *in++;
            continue;
        }

        if (*in == '(') {
            paren_depth++;
            *out++ = *in++;
            continue;
        }

        if (*in == ')') {
            paren_depth--;
            *out++ = *in++;
            continue;
        }

        if (*in == ' ' || *in == '\t' || *in == '\n') {
            *out++ = *in++;
            continue;
        }

        if (paren_depth == 1) {
            if (*in == ',') {
                *out++ = *in++;
                continue;
            }

            int constraint_len = is_constraint_keyword(in);
            if (constraint_len > 0) {
                while (constraint_len-- > 0 && *in) {
                    *out++ = *in++;
                }
                continue;
            }

            int type_len = is_type_keyword(in);
            if (type_len > 0) {
                if (strncasecmp(in, "TEXT", 4) == 0) {
                    memcpy(out, "VARCHAR(1024)", 13);
                    out += 13;
                    in += 4;
                    while (*in == ' ' || *in == '\t' || *in == '\n') in++;
                    if (*in == '(') {
                        int depth = 1;
                        in++;
                        while (*in && depth > 0) {
                            if (*in == '(') depth++;
                            if (*in == ')') depth--;
                            in++;
                        }
                    }
                    continue;
                }
                while (type_len-- > 0 && *in) {
                    *out++ = *in++;
                }
                while (*in == ' ' || *in == '\t' || *in == '\n') {
                    *out++ = *in++;
                }
                if (*in == '(') {
                    int depth = 1;
                    *out++ = *in++;
                    while (*in && depth > 0) {
                        if (*in == '(') depth++;
                        if (*in == ')') depth--;
                        *out++ = *in++;
                    }
                }
                continue;
            }

            /* Column name */
            while (*in && ((*in >= 'a' && *in <= 'z') ||
                           (*in >= 'A' && *in <= 'Z') ||
                           (*in >= '0' && *in <= '9') ||
                           *in == '_')) {
                *out++ = *in++;
            }

            while (*in == ' ' || *in == '\t' || *in == '\n') {
                *out++ = *in++;
            }

            if (*in == ',' || *in == ')') {
                memcpy(out, "VARCHAR(1024)", 13);
                out += 13;
                continue;
            }

            type_len = is_type_keyword(in);
            if (type_len > 0) {
                continue;
            }

            constraint_len = is_constraint_keyword(in);
            if (constraint_len > 0) {
                memcpy(out, "VARCHAR(1024) ", 14);
                out += 14;
                continue;
            }

            continue;
        }

        *out++ = *in++;
    }

    *out = '\0';
    return new_sql;
}

/* Helper: rewrite SQL for SeekDB compatibility
 *   - == -> =
 *   - ?N numbered params -> ? positional params
 *   - strip trailing -- comments and semicolons
 */
static inline char* rewrite_sql_for_seekdb(const char* sql) {
    if (!sql) return NULL;

    size_t len = strlen(sql);
    char* new_sql = (char*)malloc(len * 2 + 1);
    if (!new_sql) return NULL;

    char* out = new_sql;
    const char* in = sql;
    int in_string = 0;
    char string_char = 0;

    while (*in) {
        if (in_string) {
            *out++ = *in;
            if (*in == string_char && *(in-1) != '\\') {
                in_string = 0;
            }
            in++;
        } else if (*in == '\'' || *in == '"') {
            in_string = 1;
            string_char = *in;
            *out++ = *in++;
        } else if (*in == '=' && *(in+1) == '=') {
            /* == -> = */
            *out++ = '=';
            in += 2;
        } else if (*in == '?' && *(in+1) >= '0' && *(in+1) <= '9') {
            /* ?N -> ? (strip numbered parameter index) */
            *out++ = '?';
            in++;
            while (*in >= '0' && *in <= '9') in++;
        } else if (*in == '-' && *(in+1) == '-') {
            /* strip -- line comment to end of line/string */
            break;
        } else if (*in == ';') {
            /* strip trailing semicolons */
            const char* p = in + 1;
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            if (*p == '\0' || (*p == '-' && *(p+1) == '-')) {
                break;  /* trailing semicolon before end or comment */
            }
            *out++ = *in++;
        } else {
            *out++ = *in++;
        }
    }
    /* trim trailing whitespace */
    while (out > new_sql && (*(out-1) == ' ' || *(out-1) == '\t' || *(out-1) == '\n')) out--;
    *out = '\0';
    return new_sql;
}

/* Helper: rewrite CREATE INDEX - remove DESC */
static inline char* rewrite_create_index(const char* sql) {
    if (!sql) return NULL;

    const char* p = sql;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
    if (strncasecmp(p, "CREATE", 6) != 0) return NULL;
    p += 6;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (strncasecmp(p, "UNIQUE", 6) == 0) {
        p += 6;
        while (*p && (*p == ' ' || *p == '\t')) p++;
    }
    if (strncasecmp(p, "INDEX", 5) != 0) return NULL;

    size_t len = strlen(sql);
    char* new_sql = (char*)malloc(len + 1);
    if (!new_sql) return NULL;

    char* out = new_sql;
    const char* in = sql;
    int in_string = 0;
    char string_char = 0;

    while (*in) {
        if (in_string) {
            *out++ = *in++;
            if (*in == string_char && *(in-1) != '\\') {
                in_string = 0;
            }
            continue;
        }
        if (*in == '\'' || *in == '"') {
            in_string = 1;
            string_char = *in;
            *out++ = *in++;
            continue;
        }
        if (strncasecmp(in, "DESC", 4) == 0) {
            char prev = (in > sql) ? *(in-1) : ' ';
            char next = *(in+4);
            if ((prev == ' ' || prev == ',') &&
                (next == ' ' || next == ',' || next == ')' || next == '\0' || next == '\t' || next == '\n')) {
                in += 4;
                while (*in == ' ' || *in == '\t' || *in == '\n') in++;
                continue;
            }
        }
        *out++ = *in++;
    }
    *out = '\0';
    return new_sql;
}

/* Helper: rewrite CREATE TABLE to remove WITHOUT ROWID */
static inline char* rewrite_without_rowid(const char* sql) {
    if (!sql) return NULL;

    const char* p = sql;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
    if (strncasecmp(p, "CREATE", 6) != 0) return NULL;

    const char* wr = strcasestr(sql, "WITHOUT");
    if (!wr) return NULL;
    const char* after = wr + 7;
    while (*after == ' ' || *after == '\t' || *after == '\n') after++;
    if (strncasecmp(after, "ROWID", 5) != 0) return NULL;

    size_t len = strlen(sql);
    char* new_sql = (char*)malloc(len + 1);
    if (!new_sql) return NULL;

    char* out = new_sql;
    const char* in = sql;
    while (*in) {
        if (in == wr) {
            in = after + 5;
            while (*in == ' ' || *in == '\t' || *in == '\n') in++;
            continue;
        }
        *out++ = *in++;
    }
    *out = '\0';
    return new_sql;
}

/* Lifecycle */

/* sqlite3_open = acquire shared instance + create a new connection */
static inline int sqlite3_open(const char* dbname, sqlite3** ppDb) {
    sqlite3* db = (sqlite3*)malloc(sizeof(sqlite3));
    if (!db) return SQLITE_ERROR;

    const char* path = dbname ? dbname : "/tmp/seekdb";
    db->db = seekdb_instance_acquire(path);
    if (!db->db) { free(db); return SQLITE_ERROR; }

    /* autocommit=1 to match SQLite semantics */
    int ret = seekdb_connect_ex(db->db, "test", 1, &db->conn);
    if (ret != 0) { seekdb_instance_release(db->db); free(db); return ret; }
    *ppDb = db;
    return SQLITE_OK;
}

static inline int sqlite3_open_v2(const char* dbname, sqlite3** ppDb, int flags, const char* vfs) {
    return sqlite3_open(dbname, ppDb);
}

/* sqlite3_close = disconnect + release instance ref */
static inline int sqlite3_close(sqlite3* db) {
    if (!db) return SQLITE_OK;
    if (db->conn) seekdb_disconnect(db->conn);
    if (db->db) seekdb_instance_release(db->db);
    free(db);
    return SQLITE_OK;
}

/* Statement functions */
static inline int sqlite3_prepare_v2(sqlite3* db, const char* sql, int nByte,
                                      sqlite3_stmt** ppStmt, const char** pzTail) {
    sqlite3_stmt* stmt = (sqlite3_stmt*)malloc(sizeof(sqlite3_stmt));
    if (!stmt) return SQLITE_ERROR;

    /* Rewrite prepared SQL for SeekDB syntax compatibility (== -> =, etc.) */
    char* rewritten = rewrite_sql_for_seekdb(sql);
    const char* final_sql = rewritten ? rewritten : sql;

    int ret = seekdb_prepare(db->conn, final_sql, &stmt->handle);
    if (rewritten) free(rewritten);
    if (ret != 0) { free(stmt); return ret; }
    *ppStmt = stmt;
    return SQLITE_OK;
}

static inline int sqlite3_step(sqlite3_stmt* stmt) {
    return seekdb_step(stmt->handle);
}

static inline int sqlite3_reset(sqlite3_stmt* stmt) {
    return seekdb_reset(stmt->handle);
}

static inline int sqlite3_finalize(sqlite3_stmt* stmt) {
    if (!stmt) return SQLITE_OK;
    int ret = seekdb_finalize(stmt->handle);
    free(stmt);
    return ret;
}

static inline const char* sqlite3_sql(sqlite3_stmt* stmt) { return ""; }
static inline const char* sqlite3_expanded_sql(sqlite3_stmt* stmt) { return ""; }

/* Parameter binding */
static inline int sqlite3_bind_int(sqlite3_stmt* stmt, int col, int val) {
    return seekdb_bind_int(stmt->handle, col, val);
}
static inline int sqlite3_bind_int64(sqlite3_stmt* stmt, int col, long long val) {
    return seekdb_bind_int64(stmt->handle, col, val);
}
static inline int sqlite3_bind_double(sqlite3_stmt* stmt, int col, double val) {
    return seekdb_bind_double(stmt->handle, col, val);
}
static inline int sqlite3_bind_text(sqlite3_stmt* stmt, int col, const char* val, int len, void* dtor) {
    return seekdb_bind_text(stmt->handle, col, val);
}
static inline int sqlite3_bind_text64(sqlite3_stmt* stmt, int col, const char* val,
                                       unsigned long long len, void* dtor, unsigned char enc) {
    return seekdb_bind_text(stmt->handle, col, val);
}
static inline int sqlite3_bind_null(sqlite3_stmt* stmt, int col) {
    return seekdb_bind_null(stmt->handle, col);
}
static inline int sqlite3_bind_parameter_count(sqlite3_stmt* stmt) {
    return seekdb_bind_parameter_count(stmt->handle);
}

/* Result access */
static inline int sqlite3_column_count(sqlite3_stmt* stmt) {
    return seekdb_column_count(stmt->handle);
}
static inline const char* sqlite3_column_name(sqlite3_stmt* stmt, int col) {
    return seekdb_column_name(stmt->handle, col);
}
static inline int sqlite3_column_type(sqlite3_stmt* stmt, int col) {
    return seekdb_column_type(stmt->handle, col);
}
static inline int sqlite3_column_int(sqlite3_stmt* stmt, int col) {
    return seekdb_column_int(stmt->handle, col);
}
static inline long long sqlite3_column_int64(sqlite3_stmt* stmt, int col) {
    return seekdb_column_int64(stmt->handle, col);
}
static inline double sqlite3_column_double(sqlite3_stmt* stmt, int col) {
    return seekdb_column_double(stmt->handle, col);
}
static inline const char* sqlite3_column_text(sqlite3_stmt* stmt, int col) {
    return seekdb_column_text(stmt->handle, col);
}
static inline int sqlite3_column_bytes(sqlite3_stmt* stmt, int col) {
    return seekdb_column_bytes(stmt->handle, col);
}
static inline const void* sqlite3_column_blob(sqlite3_stmt* stmt, int col) {
    return seekdb_column_text(stmt->handle, col);
}

/* Utility */
static inline const char* sqlite3_errmsg(sqlite3* db) {
    const char* msg = seekdb_errmsg(db->conn);
    return msg ? msg : "unknown error";
}
static inline const char* sqlite3_libversion(void) { return "3.39.0"; }
static inline int sqlite3_libversion_number(void) { return 3039001; }
static inline const char* sqlite3_sourceid(void) { return "SeekDB-1.0.0"; }

/* Direct execution */
static inline int sqlite3_exec(sqlite3* db, const char* sql,
                                int (*callback)(void*,int,char**,char**),
                                void* arg, char** errmsg) {
    /* Skip SQLite-specific statements not supported by SeekDB */
    if (sql && strncasecmp(sql, "PRAGMA", 6) == 0)  return SQLITE_OK;
    if (sql && strncasecmp(sql, "VACUUM", 6) == 0)  return SQLITE_OK;
    if (sql && strncasecmp(sql, "ANALYZE", 7) == 0) return SQLITE_OK;

    char* rewritten  = rewrite_create_table(sql);
    const char* s1   = rewritten  ? rewritten  : sql;
    char* rewritten2 = rewrite_create_index(s1);
    const char* s2   = rewritten2 ? rewritten2 : s1;
    char* rewritten3 = rewrite_without_rowid(s2);
    const char* s3   = rewritten3 ? rewritten3 : s2;
    char* rewritten4 = rewrite_sql_for_seekdb(s3);
    const char* final_sql = rewritten4 ? rewritten4 : s3;

    seekdb_result_handle result;
    int ret = seekdb_execute(db->conn, final_sql, &result);
    if (ret == 0) seekdb_result_free(result);
    else if (errmsg) {
        const char* msg = seekdb_errmsg(db->conn);
        if (msg) {
            *errmsg = (char*)malloc(strlen(msg) + 1);
            if (*errmsg) strcpy(*errmsg, msg);
        } else {
            *errmsg = NULL;
        }
    }

    if (rewritten4) free(rewritten4);
    if (rewritten3) free(rewritten3);
    if (rewritten2) free(rewritten2);
    if (rewritten)  free(rewritten);
    return ret;
}

static inline int sqlite3_busy_timeout(sqlite3* db, int ms) { return SQLITE_OK; }

/* Stub implementations */
#define sqlite3_config(...)    SQLITE_OK
#define sqlite3_initialize()   SQLITE_OK
#define sqlite3_shutdown()     SQLITE_OK
static inline int sqlite3_db_status(sqlite3* db, int op, int* pCur, int* pHiwtr, int reset) {
    *pCur = 0; *pHiwtr = 0; return SQLITE_OK;
}
static inline int sqlite3_status(int op, int* pCur, int* pHiwtr, int reset) {
    *pCur = 0; *pHiwtr = 0; return SQLITE_OK;
}
static inline int sqlite3_db_release_memory(sqlite3* db) { return SQLITE_OK; }
static inline int sqlite3_db_config(sqlite3* db, int op, ...) { return SQLITE_OK; }
static inline int sqlite3_test_control(int op, ...) { return SQLITE_OK; }
static inline int sqlite3_file_control(sqlite3* db, const char* name, int op, ...) { return SQLITE_OK; }
static inline int sqlite3_register_cksumvfs(void) { return SQLITE_OK; }

/* Memory functions */
#define sqlite3_malloc(size)        malloc(size)
static inline void sqlite3_free(const void *ptr) { free((void *)ptr); }
#define sqlite3_realloc(ptr, size)  realloc(ptr, size)

/* String functions */
static inline int sqlite3_snprintf(int size, char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}
static inline char* sqlite3_mprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* buf = (char*)malloc(4096);
    if (buf) vsnprintf(buf, 4096, fmt, ap);
    va_end(ap);
    return buf;
}
static inline char* sqlite3_vmprintf(const char* fmt, va_list ap) {
    char* buf = (char*)malloc(4096);
    if (buf) vsnprintf(buf, 4096, fmt, ap);
    return buf;
}
static inline int sqlite3_stricmp(const char* s1, const char* s2) {
    return strcasecmp(s1, s2);
}
static inline int sqlite3_strglob(const char* pattern, const char* str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            while (*str) { if (sqlite3_strglob(pattern, str) == 0) return 0; str++; }
            return *pattern ? 1 : 0;
        } else if (*pattern == '?') { pattern++; str++; }
        else if (*pattern == *str)  { pattern++; str++; }
        else return 1;
    }
    while (*pattern == '*') pattern++;
    return *pattern || *str;
}

/* VFS - minimal implementation for speedtest1_timestamp() */
static inline int fake_current_time_int64(sqlite3_vfs* vfs, sqlite3_int64* t) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *t = (sqlite3_int64)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return 0;
}
static inline int fake_current_time(sqlite3_vfs* vfs, double* t) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *t = tv.tv_sec + tv.tv_usec / 1000000.0;
    return 0;
}
static inline int fake_delete(sqlite3_vfs* vfs, const char* name, int syncDir) { return 0; }
static inline int fake_access(sqlite3_vfs* vfs, const char* name, int flags, int* out) {
    *out = 0; return 0;
}
static inline int fake_fullpathname(sqlite3_vfs* vfs, const char* name, int len, char* out) {
    strncpy(out, name, len); return 0;
}

static sqlite3_vfs g_fake_vfs = {
  .iVersion           = 2,
  .szOsFile           = 0,
  .mxPathname         = 1024,
  .pNext              = NULL,
  .zName              = "seekdb_fake",
  .pAppData           = NULL,
  .xOpen              = NULL,
  .xDelete            = fake_delete,
  .xAccess            = fake_access,
  .xFullPathname      = fake_fullpathname,
  .xDlOpen            = NULL,
  .xDlError           = NULL,
  .xDlSym             = NULL,
  .xDlClose           = NULL,
  .xRandomness        = NULL,
  .xSleep             = NULL,
  .xCurrentTime       = fake_current_time,
  .xGetLastError      = NULL,
  .xCurrentTimeInt64  = fake_current_time_int64,
};

static inline sqlite3_vfs* sqlite3_vfs_find(const char* name) { return &g_fake_vfs; }

/* User-defined functions - stubs */
static inline int sqlite3_create_function(sqlite3* db, const char* name, int nArg,
                                           int enc, void* pApp, void* xFunc, void* xStep, void* xFinal) {
    return SQLITE_OK;
}
static inline void* sqlite3_aggregate_context(void* ctx, int size) { return NULL; }

static inline void  sqlite3_result_int64(void* ctx, long long val) {}

static inline void  sqlite3_result_text(void* ctx, const char* val, int len, void* dtor) {}

static inline const char* sqlite3_value_text(void* val) { return (const char*)val; }

static inline int   sqlite3_value_bytes(void* val) { return val ? (int)strlen((const char*)val) : 0; }

static inline int   sqlite3_value_type(void* val) { return SQLITE_TEXT; }

static inline int   sqlite3_rtree_geometry_callback(sqlite3* db, const char* name, void* xGeom, void* pCtx) {
    return SQLITE_OK;
}

/* Trace - stubs */
static inline int   sqlite3_trace_v2(sqlite3* db, unsigned int mask, void* xCallback, void* pCtx) { return SQLITE_OK; }
static inline void* sqlite3_trace(sqlite3* db, void* xCallback, void* pCtx) { return NULL; }

/* Constants */
#define SQLITE_STATIC ((void*)0)
#define SQLITE_UTF8   1

#endif /* SEEKDB_BACKEND_H */
