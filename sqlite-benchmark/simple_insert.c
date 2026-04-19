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
 * Simple Insert Benchmark for SeekDB vs SQLite
 * Usage: simple_insert <num_rows> <db_path> [--batch N] [--ps] [--threads N]
 *
 * --batch N     rows per INSERT statement (default 1)
 * --ps          use prepared statement (default: text SQL)
 * --threads N   concurrent inserter threads (default 1, single-threaded)
 *
 * The three options are fully orthogonal.
 *
 * Table schema:
 *   CREATE TABLE t1 (id INTEGER PRIMARY KEY, name VARCHAR(256),
 *                    value INTEGER, category INTEGER)
 *   CREATE INDEX idx_category ON t1(category)
 *
 * Each INSERT statement is auto-committed (no explicit transaction).
 * SQLite: WAL + synchronous=FULL for durability parity with SeekDB (clog).
 */
#include "db_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

static inline long get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static void build_ps_sql(char *buf, int size, int n) {
    int pos = snprintf(buf, size, "INSERT INTO t1 (id, name, value, category) VALUES ");
    for (int i = 0; i < n; i++)
        pos += snprintf(buf + pos, size - pos, "%s(?,?,?,?)", i > 0 ? "," : "");
}

static sqlite3_stmt *prepare_for_n(sqlite3 *db, char *buf, int buf_size, int n) {
    build_ps_sql(buf, buf_size, n);
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(db, buf, -1, &s, NULL) != SQLITE_OK)
        return NULL;
    return s;
}

/* --- per-thread context --- */
typedef struct {
    int           tid;
    const char   *db_name;
    int           row_start;   /* inclusive */
    int           row_end;     /* exclusive */
    int           batch;
    int           use_ps;
    /* output */
    long          elapsed_ns;
    long          total_build;
    long          total_exec;
    int           exec_calls;
    int           rows_done;
    int           error;
} thread_ctx;

static void *worker_func(void *arg) {
    thread_ctx *ctx = (thread_ctx *)arg;
    int batch   = ctx->batch;
    int use_ps  = ctx->use_ps;
    int sql_size = 64 + batch * 100;
    char *sql_buf = malloc(sql_size);
    if (!sql_buf) { ctx->error = 1; return NULL; }

    /* Each thread opens its own connection */
    sqlite3 *db = NULL;
    int rc = sqlite3_open(ctx->db_name, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[T%d] open failed\n", ctx->tid);
        ctx->error = 1; free(sql_buf); return NULL;
    }
    sqlite3_busy_timeout(db, 30000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);

    /* prepare statements if PS mode */
    sqlite3_stmt *stmt_full = NULL, *stmt_tail = NULL;
    if (use_ps) {
        stmt_full = prepare_for_n(db, sql_buf, sql_size, batch);
        if (!stmt_full) {
            fprintf(stderr, "[T%d] PREPARE failed: %s\n", ctx->tid, sqlite3_errmsg(db));
            ctx->error = 1; goto done;
        }
        int tail = (ctx->row_end - ctx->row_start) % batch;
        if (tail > 0) {
            stmt_tail = prepare_for_n(db, sql_buf, sql_size, tail);
            if (!stmt_tail) {
                fprintf(stderr, "[T%d] PREPARE tail failed: %s\n", ctx->tid, sqlite3_errmsg(db));
                ctx->error = 1; goto done;
            }
        }
    }

    long total_build = 0, total_exec = 0;
    int  exec_calls  = 0;
    char name_buf[64];
    long t0;
    int  num_rows = ctx->row_end - ctx->row_start;

    long start = get_time_ns();

    for (int i = ctx->row_start; i < ctx->row_end; ) {
        int remain = ctx->row_end - i;
        int chunk  = batch < remain ? batch : remain;

        if (!use_ps) {
            t0 = get_time_ns();
            if (chunk == 1) {
                snprintf(sql_buf, sql_size,
                         "INSERT INTO t1 (id, name, value, category) VALUES (%d,'name_%d',%d,%d)",
                         i, i, i * 100, i % 100);
            } else {
                int pos = snprintf(sql_buf, sql_size,
                                   "INSERT INTO t1 (id, name, value, category) VALUES ");
                for (int j = 0; j < chunk; j++) {
                    int row = i + j;
                    pos += snprintf(sql_buf + pos, sql_size - pos,
                                    "%s(%d,'name_%d',%d,%d)",
                                    j > 0 ? "," : "", row, row, row * 100, row % 100);
                }
            }
            total_build += get_time_ns() - t0;

            t0 = get_time_ns();
            rc = sqlite3_exec(db, sql_buf, NULL, NULL, NULL);
            total_exec += get_time_ns() - t0;
            if (rc != SQLITE_OK) {
                fprintf(stderr, "[T%d] INSERT failed at row %d: %s\n",
                        ctx->tid, i, sqlite3_errmsg(db));
                ctx->error = 1; goto done;
            }
        } else {
            sqlite3_stmt *s = (chunk == batch) ? stmt_full : stmt_tail;

            t0 = get_time_ns();
            for (int j = 0; j < chunk; j++) {
                int row  = i + j;
                int base = j * 4;
                snprintf(name_buf, sizeof(name_buf), "name_%d", row);
                sqlite3_bind_int64(s, base + 1, row);
                sqlite3_bind_text (s, base + 2, name_buf, -1, SQLITE_STATIC);
                sqlite3_bind_int64(s, base + 3, row * 100);
                sqlite3_bind_int64(s, base + 4, row % 100);
            }
            total_build += get_time_ns() - t0;

            t0 = get_time_ns();
            rc = sqlite3_step(s);
            total_exec += get_time_ns() - t0;
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "[T%d] PS INSERT failed at row %d: rc=%d (%s)\n",
                        ctx->tid, i, rc, sqlite3_errmsg(db));
                ctx->error = 1; goto done;
            }
            sqlite3_reset(s);
        }

        exec_calls++;
        i += chunk;
    }

    long end = get_time_ns();
    ctx->elapsed_ns  = end - start;
    ctx->total_build = total_build;
    ctx->total_exec  = total_exec;
    ctx->exec_calls  = exec_calls;
    ctx->rows_done   = num_rows;

done:
    if (stmt_full) sqlite3_finalize(stmt_full);
    if (stmt_tail) sqlite3_finalize(stmt_tail);
    free(sql_buf);
    if (db) sqlite3_close(db);
    return NULL;
}

int main(int argc, char **argv) {
    int         num_rows  = 200000;
    const char *db_name   = "test.db";
    int         batch     = 1;
    int         use_ps    = 0;
    int         n_threads = 1;

    if (argc > 1) num_rows = atoi(argv[1]);
    if (argc > 2) db_name  = argv[2];
    for (int a = 3; a < argc; a++) {
        if (strcmp(argv[a], "--ps") == 0) use_ps = 1;
        if (strcmp(argv[a], "--batch") == 0 && a + 1 < argc) batch = atoi(argv[++a]);
        if (strcmp(argv[a], "--threads") == 0 && a + 1 < argc) n_threads = atoi(argv[++a]);
    }
    if (batch < 1) batch = 1;
    if (n_threads < 1) n_threads = 1;

    printf("=== Simple Insert Benchmark ===\n");
    printf("Engine: %s | Rows: %d | Batch: %d | Mode: %s | Threads: %d\n",
           SPEEDTEST_BACKEND_NAME,
           num_rows, batch, use_ps ? "PS" : "text SQL", n_threads);
    fflush(stdout);

    /* Clean up old db */
    DB_CLEANUP(db_name);

    /* --- open setup connection: create schema, keep alive until workers finish --- */
    sqlite3 *db_setup = NULL;
    long t0 = get_time_ns();
    int rc = sqlite3_open(db_name, &db_setup);
    long open_ns = get_time_ns() - t0;
    if (rc != SQLITE_OK) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    sqlite3_exec(db_setup, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db_setup, "PRAGMA synchronous=FULL", NULL, NULL, NULL);

    t0 = get_time_ns();
    rc = sqlite3_exec(db_setup,
        "CREATE TABLE t1 (id INTEGER PRIMARY KEY, name VARCHAR(256), value INTEGER, category INTEGER)",
        NULL, NULL, NULL);
    if (rc == SQLITE_OK)
        rc = sqlite3_exec(db_setup,
            "CREATE INDEX idx_category ON t1(category)",
            NULL, NULL, NULL);
    long create_ns = get_time_ns() - t0;
    if (rc != SQLITE_OK) {
        fprintf(stderr, "CREATE TABLE/INDEX failed: %s\n", sqlite3_errmsg(db_setup));
        sqlite3_close(db_setup);
        return 1;
    }

    /* --- partition rows across threads --- */
    thread_ctx *ctxs = calloc(n_threads, sizeof(thread_ctx));
    int rows_per = num_rows / n_threads;
    for (int t = 0; t < n_threads; t++) {
        ctxs[t].tid       = t;
        ctxs[t].db_name   = db_name;
        ctxs[t].row_start = t * rows_per;
        ctxs[t].row_end   = (t == n_threads - 1) ? num_rows : (t + 1) * rows_per;
        ctxs[t].batch     = batch;
        ctxs[t].use_ps    = use_ps;
    }

    /* --- launch workers --- */
    long wall_start = get_time_ns();

    pthread_t *tids = malloc(n_threads * sizeof(pthread_t));
    for (int t = 0; t < n_threads; t++)
        pthread_create(&tids[t], NULL, worker_func, &ctxs[t]);
    for (int t = 0; t < n_threads; t++)
        pthread_join(tids[t], NULL);
    free(tids);

    long wall_end = get_time_ns();

    /* Close setup connection after all workers are done */
    sqlite3_close(db_setup);

    /* --- check errors --- */
    int any_error = 0;
    for (int t = 0; t < n_threads; t++) {
        if (ctxs[t].error) {
            fprintf(stderr, "[T%d] failed\n", t);
            any_error = 1;
        }
    }
    if (any_error) { free(ctxs); _exit(1); }

    /* --- aggregate & report --- */
    long wall_ns     = wall_end - wall_start;
    double wall_ms   = wall_ns / 1000000.0;
    long sum_build   = 0, sum_exec = 0;
    int  sum_calls   = 0, sum_rows = 0;

    for (int t = 0; t < n_threads; t++) {
        sum_build += ctxs[t].total_build;
        sum_exec  += ctxs[t].total_exec;
        sum_calls += ctxs[t].exec_calls;
        sum_rows  += ctxs[t].rows_done;
    }

    printf("\n=== Results ===\n");
    printf("Rows:    %d\n", sum_rows);
    printf("Threads: %d\n", n_threads);
    printf("Wall:    %.2f ms\n", wall_ms);
    printf("TPS:     %.0f rows/sec\n",
           wall_ms > 0 ? sum_rows * 1000.0 / wall_ms : 0);

    if (n_threads > 1) {
        printf("\n=== Per-Thread ===\n");
        printf("%-4s %10s %10s %10s\n", "T", "rows", "ms", "rows/sec");
        printf("------------------------------------------\n");
        for (int t = 0; t < n_threads; t++) {
            double ms = ctxs[t].elapsed_ns / 1000000.0;
            printf("%-4d %10d %10.2f %10.0f\n", t, ctxs[t].rows_done, ms,
                   ms > 0 ? ctxs[t].rows_done * 1000.0 / ms : 0);
        }
    }

    printf("\n=== Time Breakdown (sum of all threads) ===\n");
    printf("%-20s %10s %8s\n", "Phase", "ms", "Pct");
    printf("--------------------------------------------\n");
    long sum_thread_ns = sum_build + sum_exec;
    printf("%-20s %10.2f %7.1f%%\n", use_ps ? "BIND" : "BUILD SQL",
           sum_build / 1000000.0,
           sum_thread_ns > 0 ? 100.0 * sum_build / sum_thread_ns : 0);
    printf("%-20s %10.2f %7.1f%%\n", use_ps ? "STEP" : "EXEC",
           sum_exec / 1000000.0,
           sum_thread_ns > 0 ? 100.0 * sum_exec / sum_thread_ns : 0);
    printf("--------------------------------------------\n");
    printf("exec calls: %d  (%.1f rows/call)\n",
           sum_calls, sum_calls > 0 ? (double)sum_rows / sum_calls : 0);

    printf("\n=== Init (excl. from TPS) ===\n");
    printf("open:         %8.2f ms\n", open_ns   / 1000000.0);
    printf("CREATE+INDEX: %8.2f ms\n", create_ns / 1000000.0);
    fflush(stdout);

    free(ctxs);
    _exit(0);
}
