# Native-source Rust Extension example

This package has **no base SQL file**. Its control declares `install_source =
'native'` and the already activated `org.seekdb.rust-text` module. The module's
installation callback creates the entire initial object set: the ordinary INVOKER
routine `rust_native_length(TEXT)`, backed by `seekdb_rust_char_count`.

Deploy the matching Rust plugin library/manifest (`rust-text-native-install-v1`)
and install that module first. Place this directory under the configured
Extension root, select the target database, then use:

```sql
CREATE EXTENSION rust_text_native;
SELECT rust_native_length('A中🙂');
ALTER EXTENSION rust_text_native UPDATE TO '1.1';
SELECT rust_native_nonempty('A中🙂');
DROP EXTENSION rust_text_native RESTRICT;
```

These are intended server usage examples; the regression runs the real parser
and PL resolver with a loaded Rust DSO, but uses a Root/catalog fixture rather
than a live database commit. Native-source installation uses the same owner,
permissions, dependencies, membership and transaction path as SQL-source packages.

Fresh installation accepts the control's declared default version (1.0), not an
arbitrary version inferred from filenames. The explicit 1.0→1.1 SQL migration
does not rerun the installation callback. Missing service, empty declarations,
invalid SQL or unsupported object admission fails installation; there is no
fallback to a placeholder file or independent system-table write.

Generate these files using the Rust SDK's explicit native source mode:

```bash
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- schema \
  --manifest-path plugins/rust_text/Cargo.toml --example seekdb_native_schema \
  --output /tmp/new-rust-text-native
```

The example only writes control and an explicit migration; it does not invoke
the plugin callback. Kernel regression compares the generated files byte-for-byte
with the installed package before testing the native-only installation path.
