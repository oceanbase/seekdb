# Catalog-driven SQL extension example

This plugin demonstrates PostgreSQL-style SQL object contributions through the
stable seekdb C ABI. The core does not contain a dedicated expression class or
factory registration for any function exported by this package.

The plugin contributes:

- `seekdb_add_one(BIGINT)`: a scalar SQL function.
- `seekdb_identity(BIGINT)` and `seekdb_identity(BLOB)`: typed overloads.
- `seekdb_generate_series(BIGINT, BIGINT)`: a table function returning a
  `value BIGINT` column through an opaque, generation-leased cursor.
- `seekdb_payload`: a persistent opaque type with a separately registered
  binary codec and versioned physical format.

Build the package and its standalone integration tests:

```bash
cmake --build build_release --target seekdb_sql_extension_plugin \
  seekdb_plugin_runtime_tests seekdb_plugin_catalog_tests -j4
build_release/unittest/seekdb_plugin_runtime_tests
build_release/unittest/seekdb_plugin_catalog_tests
```

The catalog suite links the actual seekdb server runtime and verifies SQL
grammar, trusted-directory installation, normalized catalog objects, scalar
execution, table cursors, transactional persistent-type dependencies,
restrictive uninstall, and restart recovery with a fresh generation.

Install the shared object and `plugin.toml` in the server's trusted plugin
directory under `sql_extension/`, then run:

```sql
INSTALL PLUGIN sql_extension SONAME 'sql_extension/seekdb_sql_extension.so';

SELECT seekdb_add_one(41);
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
