# Embedding response parser

Safe Rust implementation of the parsing responsibility extracted into
`src/query/vector/embedding_response_parser.cpp`. It parses `data[].embedding`
in float-array or base64 format. HTTP, credentials, retries and task publication
remain the responsibility of the existing C++ task.

The server calls this core through the synchronous `embedding-response-ffi`
adapter. `ObEmbeddingTask::parse_embedding_response` delegates to
`EmbeddingResponseParser::parse`, which calls `seekdb_embedding_response_parse`.
The previous C++ JSON/base64 implementation has been removed from that path.
No production Rust speedup is claimed.

```rust
use embedding_response::{parse_into, Encoding};

let mut vectors = Vec::new();
parse_into(
    br#"{"data":[{"embedding":[1.0,-2.5]}]}"#,
    2,
    Encoding::Float,
    &mut vectors,
)?;
assert_eq!(vectors, vec![vec![1.0, -2.5]]);
# Ok::<(), embedding_response::ParseError>(())
```

## Behavior and ownership

- The caller owns `Vec<Vec<f32>>`; all buffers use Rust's allocator and are
  released normally on drop. No input reference survives the call. The crate
  forbids unsafe code and exposes no foreign pointers.
- Existing output is retained. Complete vectors append in response order;
  `index` does not reorder them. A later semantic error preserves earlier
  complete vectors. Invalid JSON anywhere appends nothing.
- Missing fields, wrong types, dimension mismatches, invalid JSON and base64
  buffer errors retain their distinct OceanBase error codes through
  `ParseError::ob_error_code()`.
- Empty `data` succeeds. An empty float vector with dimension zero returns
  allocation failure, matching the task's arena allocator on a zero-byte request.
- Integer tokens convert directly to `f32`; decimal/exponent tokens follow the
  bundled RapidJSON normal-precision conversion. Integer `-0` becomes positive
  zero; decimal negative zero retains its sign. Finite `f64` values may overflow
  to `f32` infinity, matching the original conversion.
- Base64 preserves native-endian float bits, including NaNs. Legacy padding
  acceptance, ignored padding bits and residual-buffer errors are intentional.
- Duplicate object keys use the last value, but overwritten values are still
  validated. The byte-oriented parser preserves the existing raw-NUL terminator,
  unvalidated UTF-8 string bytes, Unicode escape checks and 101-container limit.

The private JSON/base64 parsers deliberately reproduce those compatibility
requirements. Substituting a strict base64 decoder or a JSON library's default
number conversion would change existing results. Numeric conversion is adapted
from the bundled RapidJSON; its MIT notice is in `LICENSE-RAPIDJSON`.

Growing buffers use fallible reservation. Resource exhaustion returns an error;
the amount and timing of allocations differ from the C++ arena. The C ABI emits
one complete vector at a time; C++ copies it into the task allocator. Rust drops
its temporary vector after each callback and all JSON scratch before returning.
Failed output appends free the unpublished C++ allocation. The bridge scopes
platform malloc-hook allocations under `EmbRustTmp`; platforms/backends without
that hook use their normal Rust system allocator for scratch. This does not add
a new per-call scratch quota. Dimension byte arithmetic is checked rather than wrapped.

The production archive remains `libsql_nio.a` (`sql_nio.lib` on MSVC). Its Cargo
dependency on the FFI crate keeps one Rust runtime in the server. CMake tracks
both parsing crates' sources/manifests; Bazel includes them in the archive action.
The FFI crate's standalone staticlib exists for isolated ABI tests and must not be
linked alongside the production archive. See `../embedding-response-ffi/include/embedding_response.h`
for the complete pointer, callback and error contract.

## Validation

Run from `rust/` to select the repository's pinned toolchain:

```sh
cargo test -p embedding-response
cargo test -p embedding-response-ffi
cargo clippy -p embedding-response --all-targets -- -D warnings
cargo fmt -p embedding-response --check
python3 embedding-response/tests/compare_cpp.py
```

The first test run also resolves the workspace lockfile. The differential runner
uses `--offline --locked` and requires a C++17 compiler plus the project's bundled
RapidJSON headers, installed by the normal build initialization. `CXX` and
`--rapidjson-include` can override their locations.

The differential corpus has a fixed random seed and compares every error code,
vector count, partial result and float bit. It covers integer boundaries,
decimal values around float midpoints, underflow/overflow, malformed JSON,
duplicate/escaped keys, nesting, non-UTF-8 bytes and permissive base64 padding.
The oracle compiles the actual `ObBase64Encoder` and bundled RapidJSON, with a
small adapter for the old task's extraction rules. The core comparison alone
does not exercise the C ABI. For production integration, from the repository root:

```sh
cmake --build build_release --target seekdb embedding_response_integration -j16
build_release/src/observer/embedding_response_integration
python3 rust/embedding-response/tests/compare_cpp.py --adapter-probe build_release/src/observer/embedding_response_integration
python3 rust/embedding-response-ffi/tests/check_c_abi.py --archive build_release/rust-target/release/libsql_nio.a
```

The integration executable links the production C++ adapter and Rust archive,
injects allocator/append failures using the real C++ interfaces, and runs the
same differential corpus through the real callback path. It does not exercise
HTTP retries or the task scheduler, whose code is unchanged. Rust and isolated
C ABI checks run in `rust-checks`; the production integration runs in `buildbase`.
Performance remains covered by the existing daily regression process.
