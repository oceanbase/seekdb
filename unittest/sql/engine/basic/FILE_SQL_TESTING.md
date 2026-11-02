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

```sh
cd tools/deploy/mysql_test
python3 psmalltest.py -t file_sql -c file_sql_basic
```

The suite deploys `test_suite/file_sql/data/` into
`$OBSERVER_DIR/data_for_mysqltest_file_sql`, sets `secure_file_priv` to that
directory and removes it afterwards. `file_sql_basic.test` covers the feature
switch, the three reader functions, aggregates, `DESCRIBE`, `glob()` and the
`COPY` export/read-back round trip.

## Additional SQL checks

The following statements were verified on a local build
(clang-cl 18 / MSVC 2022, Windows, `enable_file_sql = ON`). They are grouped by
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
  memory grows with the file: a 500 000-row / 11.7 MB CSV peaks at roughly
  511 MB RSS. Very large files can still exhaust memory.
- A single CSV field larger than the line buffer fails with
  `OB_SIZE_OVERFLOW` (no partial output).
- `secure_file_priv` is only enforced when it is not empty; it is a GLOBAL
  variable that can be set at startup but not over a TCP session on Windows,
  so path-escape checks were only reviewed, not exercised locally.
