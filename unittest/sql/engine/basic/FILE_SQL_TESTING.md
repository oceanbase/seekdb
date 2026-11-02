# File-as-database verification

The contest specification is at
https://oceanbase.yuque.com/org-wiki-obtech-vh7w9r/hc3pio/gy2a66vag0028msk.
The implementation lives in `src/sql/engine/basic/ob_file_sql_scan.cpp`
(CSV / JSONL / glob readers and schema inference) and
`src/sql/engine/basic/ob_parquet_reader.cpp` (Parquet footer, page decode and
row reader). The resolver side is `ObDMLResolver::resolve_read_file_item()`,
which materialises the rows into an `ObValuesTableDef`; `COPY ... TO` reuses
`ObSelectIntoOp` through a desugared `T_INTO_OUTFILE` node.

Nothing here is executed against an external query engine: every statement runs
on the seekdb SQL engine, and `read_*()` only feeds it a values table.

## Running the official test

Build on Linux with the repository scripts. The parser is generated with
`bison -Werror`, so a grammar warning aborts the configure step:

```sh
./build.sh init
./build.sh release
make -C build_release -j"$(nproc)" seekdb
```

`.github/script/seekdb/mysqltest_for_seekdb.py` is the authoritative runner
(seekdb CI uses it): it starts a disposable instance from
`build_release/src/observer/seekdb` and runs every `test_suite/*/t/*.test`
case with `deps/3rd/u01/obclient/bin/mysqltest`. A single case can be replayed
against a running instance with the same environment:

```sh
export PATH=$PWD/deps/3rd/u01/obclient/bin:$PATH
python3 .github/script/seekdb/sdb.py start \
  --binary build_release/src/observer/seekdb --base-dir /tmp/seekdb-inst --port 2881
python3 .github/script/seekdb/sdb.py wait-ready --client obclient \
  --base-dir /tmp/seekdb-inst --host 127.0.0.1 --port 2881 --user root
obclient -h127.0.0.1 -P2881 -uroot -A -c -Doceanbase < tools/deploy/init.sql
obclient -h127.0.0.1 -P2881 -uroot -A -c -Dtest      < tools/deploy/init_user.sql
export OBMYSQL_PORT=2881 OBMYSQL_MS0=127.0.0.1 OBMYSQL_PWD=admin \
       OBMYSQL_USR=admin OBSERVER_DIR=/tmp/seekdb-inst IS_BUSINESS=0 TENANT=mysql
cd tools/deploy && mysqltest --host=127.0.0.1 --port=2881 --user=admin \
  --password=admin --database=test \
  --test-file=mysql_test/test_suite/file_sql/t/file_sql_basic.test \
  --result-file=mysql_test/test_suite/file_sql/r/mysql/file_sql_basic.result
```

The suite deploys `test_suite/file_sql/data/` into
`$OBSERVER_DIR/data_for_mysqltest_file_sql` with `ssh`/`scp` and removes it
afterwards, so the observer host has to be reachable from the client host. It
does not set `secure_file_priv`; the default value (`/`) allows the fixture
directory. `file_sql_basic.test` covers the feature switch, the three reader
functions, aggregates, `DESCRIBE`, `glob()` and the `COPY` export/read-back
round trip.

## Additional SQL checks

The following statements were verified on a local Linux build (Ubuntu 26.04,
clang 17 from `deps/3rd`, `enable_file_sql = ON`). They are grouped by
the scoring item they belong to.

### Interface and file discovery

- `read_csv`, `read_json` / `read_ndjson`, `read_parquet`, `glob` and
  `DESCRIBE SELECT * FROM read_*()`; table alias, column alias and named
  arguments (`header`, `delim`, `quote`, `escape`, `nullstr`, `format`,
  `sample_size`).
- `glob()` returns a single `file` column with `/` separators on every
  platform; a pattern that matches nothing yields an empty result, while
  `read_*(pattern)` that matches nothing reports `OB_FILE_NOT_EXIST`.

### CSV

- Header row, duplicate header names (`id`, `id__2`), UTF-8 text, LF and CRLF,
  doubled quotes, backslash escapes, delimiters and newlines inside quoted
  fields, empty fields as NULL, an unquoted `\N` as NULL, and the `1366`
  diagnostic naming the offending column and line.
- Lines are fed in batches of 4096 rows; `rescan()` re-opens every file, so a
  file modified between two statements is re-read with its new size.

### JSONL

- Flat objects only: nested objects and arrays are rejected with
  `OB_ERR_INVALID_JSON_TEXT`; the union schema keeps the order in which keys
  first appear (the JSON DOM iterates in hash order, so the raw line is scanned
  for top-level keys); a missing member becomes NULL; `J_INT` / `J_UINT` /
  `J_DOUBLE` / `J_BOOLEAN` / `J_STRING` widen into BIGINT, DOUBLE, BOOLEAN and
  VARCHAR.

### Parquet

- Footer metadata (Thrift compact protocol), multiple row groups, nullable
  columns, INT32 / INT64 / DOUBLE / BOOLEAN / BYTE_ARRAY, plain and dictionary
  pages, Snappy, and column pruning (only the projected columns are decoded).

### Type inference

- BIGINT, DOUBLE, BOOLEAN, VARCHAR, DATE, DATETIME and NULL; widening rules
  (int + double = double, int + text = varchar, date + datetime = datetime);
  int64 boundaries; invalid dates stay VARCHAR; an all-NULL column keeps the
  NULL type and `DESCRIBE` reports it as `NULL`.

### Single-file SQL and JOIN

- Expressions, aliases, `WHERE`, `ORDER BY`, `LIMIT`, `GROUP BY`, `HAVING` and
  `COUNT` / `SUM` / `AVG` / `MIN` / `MAX`.
- INNER JOIN, LEFT JOIN, self join and cross-format joins (CSV with Parquet and
  CSV with JSONL) all run through the native seekdb join operators.

### COPY

- `COPY (query) TO path (format csv, header, nullstr ...)` reuses
  `SELECT INTO OUTFILE`: DuckDB CSV defaults (comma separated, optionally
  double quoted), header written once on the row and batch paths, NULL written
  as the configured text (`\N` by default), an existing target rejected with
  `OB_FILE_ALREADY_EXIST`, and the output readable by `read_csv`.

## Known limitations

- Rows are materialised into the statement's values table before execution, so
  memory grows with the file: a 200 000-row / 5.6 MB CSV raises the server RSS
  by about 140 MB, and a 500 000-row / 11.7 MB CSV peaks at roughly 511 MB.
  Very large files can still exhaust memory.
- A single CSV field larger than the line buffer fails with
  `OB_SIZE_OVERFLOW` (no partial output).
- A wildcard in the directory part of a pattern (`glob('dir/**/*.csv')`, i.e.
  recursive glob) is rejected with `OB_NOT_SUPPORTED`. The wildcard must be in
  the last path component.
- A file with no data rows after the header reports
  `OB_ERR_DATA_TRUNCATED`; a row with fewer fields than the schema reads the
  missing fields as `NULL`, and a row with more fields is rejected with
  `OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD`, naming the physical line.
- `secure_file_priv` defaults to `/` and can only be changed over the local
  unix socket (`<base-dir>/run/sql.sock`), not over TCP. Enforcement was
  verified for absolute paths, `..` components, `glob()`, `COPY ... TO` and
  symbolic links that point outside the allowed directory (all rejected with
  `OB_ERR_INSUFFICIENT_PRIVILEGE_TO_DIRECT_PATH_ACCESS`).
