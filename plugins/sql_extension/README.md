# Catalog-driven SQL extension example

The reference module also registers two scalar implementations with
`SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY`:
`org.seekdb.sql-extension.function.native-add-one` and
`org.seekdb.sql-extension.function.unnamed-add-one`. They use the same int64
callback as `seekdb_add_one`, but contribute no module-level SQL names. The first
has the diagnostic label `seekdb_add_one` (without reserving or shadowing that
name); the second has no label. A database-local `LANGUAGE C` routine can bind
either exact object ID under owner `org.seekdb.sql_extension` (the manifest ID,
not the hyphenated service namespace). This separates
implementation registration from SQL installation; it is not a private-code
security boundary. Native creation still requires the host's privilege,
signature, catalog-column and durable dependency admission.

Both direct and snapshot registration regressions check that only the public
function is found by SQL name and that either implementation-only ID executes
through the normal leased callback. These are loader/ABI tests, not proof of
committed database installation or recovery.

This plugin demonstrates PostgreSQL-style SQL object contributions through the
stable seekdb C ABI. The core does not contain a dedicated expression class or
factory registration for any function exported by this package.

The plugin contributes:

- `seekdb_add_one(BIGINT)`: a scalar SQL function.
- `seekdb_sql_add_one(BIGINT)`: parameterized host SQL through the execution
  context v2 suffix (`SELECT CAST(? AS SIGNED) + 1`), with no native fallback.
- `seekdb_sql_exec(TEXT, BIGINT)`: invoker-rights parameterized SQL; returns
  affected rows for DML or consumed rows for SELECT (maximum 1024).
- `seekdb_identity(BIGINT)` and `seekdb_identity(BLOB)`: typed overloads.
- `seekdb_generate_series(BIGINT, BIGINT)`: a table function returning a
  `value BIGINT` column through an opaque, generation-leased cursor.
  It explicitly declares NULL propagation: either NULL input produces no rows
  without entering the table callback, after any required implicit casts.
- `seekdb_payload`: a persistent opaque type with a separately registered
  binary codec and versioned physical format.

Build the package and its standalone integration tests:

```bash
cmake --build build_release --target seekdb_sql_extension_plugin -j2
cmake -S rust/plugin-runtime/tests -B build_release/plugin-runtime-tests \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build_release/plugin-runtime-tests -j2
ctest --test-dir build_release/plugin-runtime-tests --output-on-failure
```

The standalone tests load the actual plugin DSO using the C++ loader and Rust
registration journal. Catalog commits and the SQL API executor are test doubles:
these tests prove descriptor/ABI transport, not durable SQL behavior. Full server
SQL, permission, transaction, cancellation and restart tests remain required.

Install the shared object and `plugin.toml` in the server's trusted plugin
directory under `sql_extension/`, then run:

```sql
INSTALL PLUGIN sql_extension SONAME 'sql_extension/seekdb_sql_extension.so';

SELECT seekdb_add_one(41);
SELECT seekdb_sql_add_one(41);
SELECT seekdb_identity(42), seekdb_identity('payload');
SELECT value FROM TABLE(seekdb_generate_series(2, 4));

CREATE TABLE plugin_values (payload seekdb_payload);
SHOW CREATE TABLE plugin_values;

-- This fails while plugin_values depends on the persistent payload format.
UNINSTALL PLUGIN sql_extension;

DROP TABLE plugin_values;
UNINSTALL PLUGIN sql_extension;
```

The example is intentionally limited to the public C SDK. It never exposes
core C++ objects, persists executable pointers, or bypasses service and
extension generation leases.

The SQL SPI accepts a single parameterized SELECT or DML on the calling
session, with normal privilege checks. Enclosing SELECT result sets acquire
statement savepoints before invoking SQL, so the rollback boundary is not just
one plugin callback. DDL is not enabled: extension installation still needs
transactional schema integration. Result rows are delivered synchronously and
cannot outlive the callback. See `include/seekdb/plugin/sql_spi.h` for limits,
supported value types and context lifetime. The v2 suffix is optional; a v1-only
caller receives an unavailable error from `seekdb_sql_add_one`.
The two SQL-aware scalar service tables explicitly select execution SPI minor 1
(`SEEKDB_PLUGIN_EXECUTION_SQL_CONTEXT_MINOR`). Minor-0 services continue receiving
an exact v1 context, preserving existing GIS and other binaries that compare
`struct_size` for equality. This execution revision is separate from the service
business version and the SQL API's own major/minor version.

The real-server regression is separate and opt-in. It requires PyMySQL and a
disposable loopback server with this package already installed. It creates and
removes uniquely named fixture databases/users; it does not change packages or
server configuration. A green standalone CTest does **not** mean this test ran:

```bash
python3 rust/plugin-runtime/tests/sql_spi_server.py --port 2881 \
  --confirm-disposable-server
```

Credentials, if required, come from `SEEKDB_TEST_PASSWORD`; do not pass passwords
on the command line. The regression checks cross-session commit visibility,
multi-row writes followed by a late outer error, preservation of earlier caller
writes/named savepoints, and invoker-rights reads/write denial.

Persistent `seekdb_payload` column identity has a separate two-phase restart
regression in `rust/plugin-runtime/tests/type_identity_server.py`. New column
metadata uses the generation-independent v2 marker; old v1 metadata remains
readable by the new host. This is not downgrade compatibility with old binaries.
See `docs/developer-guide/zh/plugin-type-identity.md` for the opt-in workflow and
remaining database/concurrency verification requirements.
