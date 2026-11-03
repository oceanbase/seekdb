# @@NAME@@ seekdb plugin

Generated public C ABI Rust `cdylib`, plugin ID `@@PLUGIN_ID@@`. It registers
`@@NAME@@_chars(TEXT)`, which counts Unicode scalar values (not grapheme clusters),
preserves NULL/empty inputs and rejects malformed UTF-8. No SQL is executed during
project creation. The copied toolchain file pins Rust to the supplied seekdb tree.

From this project directory:

```bash
cargo test --offline
cargo clippy --offline --all-targets -- -D warnings
cargo seekdb schema --manifest-path Cargo.toml --output schema
cmake -S . -B build
cmake --build build --target seekdb_@@NAME@@_plugin --parallel 2
cargo seekdb package --build-dir build --target seekdb_@@NAME@@_plugin --output package
```

The `cargo seekdb` commands require the optional CLI installation. Without
it, run the tool through `cargo run --offline --manifest-path
/path/to/seekdb/rust/cargo-seekdb/Cargo.toml -- schema ...` or `-- package ...`.
Use the matching checkout configured by `SEEKDB_SOURCE_DIR` in CMakeLists.txt.
Cargo.toml references that checkout's SDK; update both paths if it moves.

`schema` executes the trusted `examples/seekdb_schema.rs` generator and writes
`@@NAME@@_ops.control` and `@@NAME@@_ops--1.0.sql` into a new directory. The SQL
wrapper `@@NAME@@_length` uses the native declaration's signature and volatility,
with an explicit TEXT mapping and NO SQL assertion. Add handwritten SQL and
explicit version-update scripts through the SDK schema Package; generated SQL
is reviewable. `rlib` exists for the build-time example, not host runtime linking.
The wrapper needs native module `@@PLUGIN_ID@@` already active during installation.
It is not PG LANGUAGE C DDL or runtime-session catalog registration. Native
`package` still produces only the DSO/manifest; SQL files are a separate package.
Do not install output containing `.seekdb-schema-incomplete`. Generation is not
SQL validation, server authorization, signing or atomic deployment.

CMake uses the host's existing resolved Cargo dependency check and binary
export/core-import audit. A direct `cargo build` is useful for development but
does not replace these package gates. The generated entrypoint filename matches
the platform where this project was created; verify it when changing platforms.
Project creation does not download dependencies, run Git, configure builds or
load code. Building requires the pinned toolchain and normal host build tools.

The package is not installed into a running server. Deploy using the server's
trusted plugin directory and normal authorization/catalog workflow, then call
`SELECT @@NAME@@_chars('A中🙂');` (expected 3). Package creation is not evidence of
server-version compatibility or real database transaction/permission validation.

The template uses the same loader and lifecycle as C/C++ GIS. Registration is
transactional within activation; it is not a runtime-session SQL catalog API.
Extend the normal SDK declarations, callbacks and manifest together. Change
volatility/thread-safety flags when adding I/O or mutable state. Rust panic
conversion requires unwind mode; native plugins remain trusted in-process code.

When placed under seekdb/plugins/, explicitly add this directory to the parent's
CMakeLists.txt to integrate it with the monorepo build. Project creation does not
edit the parent's build files. For an external directory the generated standalone
CMake project is ready to configure as shown above.
