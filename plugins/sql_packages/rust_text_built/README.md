# Transaction-built Rust text Extension

This control-only SQL package binds the already-installed, ACTIVE
`org.seekdb.rust-text` module. Its catalog SPI 1.1 build callback creates
`rust_built_length(TEXT)` and then `rust_built_nonempty(TEXT)`, referencing the
first routine through the transaction-local schema view. There is no placeholder
base SQL and no catalog write during module init/start.

The callback also uses the optional build-context v2 lookup capability: it
checks absence before creation, then resolves the uppercase function name back
to the reserved ID and checks that the procedure namespace is separate. Hosts
without that capability reject this package's build; it does not silently skip
lookup or adopt pre-existing objects.

With experimental plugins and the administrator's extension directory configured:

```sql
CREATE EXTENSION rust_text_built;
SELECT rust_built_length('海洋'), rust_built_nonempty('海洋');
-- Expected: 2, 1. Empty string gives 0, 0; NULL propagates.
DROP EXTENSION rust_text_built;
```

Normal owner/database/CREATE ROUTINE checks apply. Returned build IDs are only
reservations until installation commits. This example supplies only version 1.0;
it does not advertise an update path. The kernel fixture runs the real Rust DSO,
parser, PL resolver and identity reservations with controlled schemas, not a live
database transaction. See the [builder contract](../../../docs/developer-guide/zh/plugin-catalog-builder.md).
