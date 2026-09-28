# Embedding requests and responses

This crate owns embedding request JSON serialization and float/base64 response
parsing. C++ retains HTTP transport, provider selection, batching, retries and
publication of results.

`src/ffi.rs` exposes the synchronous C ABI. Its `build.rs` and sql-nio's
`build.rs` both call `rust/build-support/ffi.rs`, extracted from sql-nio's
existing cbindgen generator. The cbindgen version is pinned once in the workspace.
This generates `include/embedding.h`; change Rust declarations instead of the
header. Safe parsing and serialization modules prohibit unsafe code. Only the
FFI module permits the pointer operations needed at the language boundary.

CMake and Bazel build `libembedding_response.a` (`embedding_response.lib` on
Windows) and explicitly link it into the server alongside `sql-nio`. The crates
do not depend on each other. Both use `add_rust_ffi_library` in CMake and
`rust_ffi_archive` in Bazel, sharing build configuration and dependency tracking. Release and CMake debug profiles use `panic=abort`
so a Rust panic cannot unwind into C++.

## Ownership and errors

Inputs are borrowed only until the ABI call returns. Callbacks borrow each
complete vector or request body only during that callback; C++ copies the data
into its task allocator. Rust drops all temporary allocations before returning.
The copied request body remains valid throughout asynchronous HTTP sending.

Rust returns an `EmbeddingResult`: a semantic status plus a separate callback
error. `src/query/vector/embedding_error.h` maps statuses to the server's `OB_*`
constants. Callback errors propagate unchanged. Rust does not hardcode server
error numbers. Allocation uses fallible reservations and the platform allocator,
without a scoped malloc hook.

## Behavior

- Requests contain `input`, `model`, `encoding_format` and positive `dimensions`.
  Serialization counts the escaped size before allocating. Quotes, backslashes
  and all control bytes are escaped; other text bytes are preserved. Callers
  provide UTF-8 text. Empty tasks complete without sending HTTP.
- Responses append complete vectors in response order, ignoring `index`. Invalid
  JSON emits nothing; a later semantic or callback error preserves prior vectors.
- Float conversion preserves the bundled RapidJSON rounding behavior. Base64
  preserves native-endian float bits and the existing padding acceptance rules.
- Duplicate object keys use the last value; overwritten values are still
  validated. The parser retains raw-NUL termination and the existing depth limit.

Numeric conversion is adapted from bundled RapidJSON; see `LICENSE-RAPIDJSON`.

## Validation

Focused unit tests live beside the source and run with the workspace checks:

```sh
# Run in rust/ to select the pinned toolchain.
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all --check
```

CI checks that regenerated C headers match the committed files. Module behavior
and performance belong to seekdb's existing regression process; this crate does
not maintain a separate C++/Python integration-test harness.
