# SQL Extension composition

`text_composed` requires an already installed `text_ops` in the same database.
It has no native module: its routine bodies call the provider's ordinary SQL
functions. Package loading uses the Rust source reader; object creation still
uses the kernel's normal routine resolver, permissions and schema transaction.

On a disposable experimental server with these packages in `--extension-dir`:

```sql
CREATE EXTENSION text_ops;
CREATE EXTENSION text_composed;
SELECT seekdb_text_is_ascii('ASCII'), seekdb_text_is_ascii('海洋');
-- 1, 0 under a UTF-8 connection; NULL input returns NULL, empty input returns 1.
ALTER EXTENSION text_composed UPDATE TO '1.1';
SELECT seekdb_text_extra_bytes('海洋'); -- 4 under UTF-8
DROP EXTENSION text_ops RESTRICT; -- rejected while text_composed depends on it
DROP EXTENSION text_composed;
DROP EXTENSION text_ops;
```

Install `text_ops` first. `requires` does not auto-install, grant EXECUTE, search
other databases or load libraries. The dependency stores the provider's stable
Extension ID, not a module generation. Consumer updates retain this edge;
consumer removal deletes it, while provider removal checks incoming edges before
detaching members. A versioned update can change the declared set if the resulting
graph is acyclic; same-version no-ops cannot. This example preserves its provider
because both versions call its routines. Removing a declaration does not remove
the routines' actual object dependencies. CASCADE remains unsupported.

CMake delivers all three files via the `plugins` component without building a
DSO. The kernel regression checks the installed bytes and resolves these actual
routine bodies against staged provider schemas, including their real routine-ID
dependencies. Staging is a controlled fixture, not live database installation.
The opt-in `extension_install_server.py` additionally checks calls, dependency
rows, missing/cross-database providers, update/no-op, RESTRICT and database cleanup;
these real-server checks must be run separately and are not CTest evidence.

The Rust SDK can generate this same package using `PackageOptions::requires`.
From the repository root, choose a new output directory:

```bash
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- schema \
  --manifest-path plugins/rust_text/Cargo.toml \
  --example seekdb_composed_schema --output /tmp/new-text-composed-package
```

The example lives in the Rust text project for developer-tool reuse, but declares
`native_module: None`. Generating or installing this SQL package does not require
loading the Rust text library. The CLI validates the generated control through
the actual Rust host reader; kernel tests compare the output byte-for-byte with
the three CMake-installed files.
