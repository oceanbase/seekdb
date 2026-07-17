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
 * SeekDB Embedded Client
 *
 * Interactive SQL client for SeekDB embedded mode, similar to mysql-client.
 * Links against libseekdb_embed_c.so and runs the database in-process.
 *
 * Usage: embedded_client [--db-dir <path>] [--db <database>] [--port <port>]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "observer/embed/c/seekdb.h"

#define MAX_SQL_LEN (1024 * 1024)
#define DEFAULT_DB "test"

static volatile int g_interrupted = 0;

static void sigint_handler(int sig)
{
  (void)sig;
  g_interrupted = 1;
}

static void print_usage(const char *prog)
{
  fprintf(stderr, "Usage: %s [options]\n", prog);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --db-dir <path>   Data directory (required)\n");
  fprintf(stderr, "  --db <name>       Database name (default: %s)\n", DEFAULT_DB);
  fprintf(stderr, "  --port <port>     Also start MySQL-protocol listener on this port\n");
  fprintf(stderr, "  --help            Show this help\n");
}

static void print_result(seekdb_result_handle result)
{
  int cols = seekdb_result_column_count(result);
  int rows = seekdb_result_row_count(result);

  if (cols == 0) {
    int affected = seekdb_result_affected_rows(result);
    printf("Query OK, %d row(s) affected\n", affected);
    return;
  }

  /* Calculate column widths */
  int *widths = calloc(cols, sizeof(int));
  if (!widths) {
    fprintf(stderr, "out of memory\n");
    return;
  }

  for (int c = 0; c < cols; c++) {
    const char *name = seekdb_result_column_name(result, c);
    widths[c] = name ? (int)strlen(name) : 4;
  }

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const char *val = seekdb_result_value(result, r, c);
      int len = val ? (int)strlen(val) : 4; /* "NULL" */
      if (len > widths[c]) widths[c] = len;
    }
  }

  /* Cap column widths to prevent absurd output */
  for (int c = 0; c < cols; c++) {
    if (widths[c] > 80) widths[c] = 80;
  }

  /* Print separator line */
  for (int c = 0; c < cols; c++) {
    printf("+");
    for (int i = 0; i < widths[c] + 2; i++) printf("-");
  }
  printf("+\n");

  /* Print column headers */
  for (int c = 0; c < cols; c++) {
    const char *name = seekdb_result_column_name(result, c);
    printf("| %-*s ", widths[c], name ? name : "???");
  }
  printf("|\n");

  /* Separator */
  for (int c = 0; c < cols; c++) {
    printf("+");
    for (int i = 0; i < widths[c] + 2; i++) printf("-");
  }
  printf("+\n");

  /* Print rows */
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const char *val = seekdb_result_value(result, r, c);
      printf("| %-*s ", widths[c], val ? val : "NULL");
    }
    printf("|\n");
  }

  /* Bottom separator */
  for (int c = 0; c < cols; c++) {
    printf("+");
    for (int i = 0; i < widths[c] + 2; i++) printf("-");
  }
  printf("+\n");

  printf("%d row(s) in set\n", rows);
  free(widths);
}

static int is_quit_command(const char *sql)
{
  while (*sql == ' ' || *sql == '\t') sql++;
  return (strncasecmp(sql, "quit", 4) == 0 ||
          strncasecmp(sql, "exit", 4) == 0 ||
          strncasecmp(sql, "\\q", 2) == 0);
}

static char *strip_trailing(char *s)
{
  size_t len = strlen(s);
  while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' '))
    s[--len] = '\0';
  /* Strip trailing semicolon -- the engine doesn't need it */
  if (len > 0 && s[len-1] == ';')
    s[--len] = '\0';
  return s;
}

int main(int argc, char **argv)
{
  const char *db_dir = NULL;
  const char *db_name = DEFAULT_DB;
  int port = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--db-dir") == 0 && i + 1 < argc) {
      db_dir = argv[++i];
    } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
      db_name = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!db_dir) {
    fprintf(stderr, "Error: --db-dir is required\n");
    print_usage(argv[0]);
    return 1;
  }

  signal(SIGINT, sigint_handler);

  /* Initialize database */
  printf("Opening database at: %s\n", db_dir);
  fflush(stdout);

  seekdb_handle db = NULL;
  int ret;
  if (port > 0) {
    ret = seekdb_open_with_service(db_dir, port, &db);
  } else {
    ret = seekdb_open(db_dir, &db);
  }
  if (ret != 0) {
    fprintf(stderr, "Failed to open database: %s (ret=%d)\n",
            db ? seekdb_error(db) : "unknown", ret);
    if (db) seekdb_close(db);
    return 1;
  }
  printf("Database opened successfully.\n");

  /* Connect */
  seekdb_conn_handle conn = NULL;
  ret = seekdb_connect(db, db_name, &conn);
  if (ret != 0) {
    fprintf(stderr, "Failed to connect to database '%s': %s\n",
            db_name, seekdb_error(db));
    seekdb_close(db);
    return 1;
  }
  printf("Connected to database: %s\n", db_name);
  printf("Type SQL statements, or 'quit' to exit.\n\n");
  fflush(stdout);

  /* Multi-line SQL buffer */
  char *sql_buf = malloc(MAX_SQL_LEN);
  if (!sql_buf) {
    fprintf(stderr, "out of memory\n");
    seekdb_disconnect(conn);
    seekdb_close(db);
    return 1;
  }

  char line[4096];
  int in_multiline = 0;
  size_t sql_len = 0;

  while (!g_interrupted) {
    if (in_multiline) {
      printf("    -> ");
    } else {
      printf("seekdb> ");
    }
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    g_interrupted = 0;

    /* Check for quit */
    if (!in_multiline && is_quit_command(line)) {
      break;
    }

    /* Append line to buffer */
    size_t line_len = strlen(line);
    if (sql_len + line_len + 1 >= MAX_SQL_LEN) {
      fprintf(stderr, "SQL too long, discarding.\n");
      sql_len = 0;
      in_multiline = 0;
      continue;
    }
    if (in_multiline) {
      sql_buf[sql_len++] = ' ';
    }
    memcpy(sql_buf + sql_len, line, line_len);
    sql_len += line_len;
    sql_buf[sql_len] = '\0';

    /* Check if the line ends with semicolon (after stripping whitespace) */
    char *trimmed = sql_buf;
    size_t tlen = sql_len;
    while (tlen > 0 && (trimmed[tlen-1] == '\n' || trimmed[tlen-1] == '\r' || trimmed[tlen-1] == ' '))
      tlen--;

    if (tlen == 0) {
      sql_len = 0;
      in_multiline = 0;
      continue;
    }

    if (trimmed[tlen-1] != ';') {
      in_multiline = 1;
      continue;
    }

    /* We have a complete statement -- execute it */
    sql_buf[sql_len] = '\0';
    strip_trailing(sql_buf);

    if (strlen(sql_buf) == 0) {
      sql_len = 0;
      in_multiline = 0;
      continue;
    }

    seekdb_result_handle result = NULL;
    ret = seekdb_execute(conn, sql_buf, &result);
    if (ret != 0) {
      const char *err = seekdb_error(db);
      fprintf(stderr, "ERROR %d: %s\n", ret, err ? err : "unknown error");
    } else if (result) {
      print_result(result);
      seekdb_result_free(result);
    }
    printf("\n");
    fflush(stdout);

    sql_len = 0;
    in_multiline = 0;
  }

  printf("Bye\n");
  free(sql_buf);
  seekdb_disconnect(conn);
  seekdb_close(db);
  return 0;
}
