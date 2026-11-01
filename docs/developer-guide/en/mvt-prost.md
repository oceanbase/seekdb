# MVT encoding with Rust prost

`_st_asmvt` still produces Mapbox vector tiles. `_st_asmvtgeom` still prepares
geometry for tile coordinates. The SQL resolver, casts, LOB reads, JSON traversal,
clipping and geometry command visitor remain in C++. Rust owns the MVT messages,
typed value dictionary, key indices, tags, retained feature data and encoding.

## Interfaces and ownership

The `mvt` crate has no dependency on tonic, Tokio or the RPC crates. Native Rust
callers use `LayerBuilder`, `FeatureInput` and `Value`. Generated message types
are available under `mvt::protocol`. The optional `cxx-bridge` feature, enabled
by default for the database build, exposes the compatibility interface under
`oceanbase::mvt`.

The SQL adapter creates one builder per aggregate, seeds SQL column keys in
column order, and adds geometry commands and typed properties for each non-null
geometry. The builder copies row data before the C++ row allocator is released.
Variable-sized builder copies and collection growth use fallible reservations.
Native callers receive `BuilderError`; the CXX adapter retains the first error
and stops subsequent mutations. The SQL adapter propagates allocation failure
as `OB_ALLOCATE_MEMORY_FAILED`. Use `try_encode` or caller-owned `encode_to`
when fallible output allocation is required; standard fixed constructor and
convenience encoding allocations follow the existing Rust allocation policy.

Dropping the aggregate drops its Rust-owned data. At finalization, C++ allocates
the output using its query allocator and prost writes into that borrowed buffer
through `encoded_len` and `encode_to`. There is no allocator callback or
protobuf-c pointer graph at the interface.

The chosen design retains features in one Rust builder. An alternative that
passes every feature and property to Rust only at finalization would duplicate
retained aggregate data and defer errors. A separate row DTO would also add
another owned representation before dictionary encoding. The incremental
interface fits the existing SQL row loop and keeps that loop's error handling.

## Schema and generation

`src/share/geo/ob_vector_tile.proto` remains the single schema. Generate its Rust
bindings, the standby bindings and CXX headers with:

```sh
./build.py build //rust:generated_sources
```

The MVT output is `build/bin/rust/mvt/generated/vector_tile.rs`. The production
Cargo action receives its declared path through `MVT_PROTO_RS`. For standalone
Cargo commands, set that variable to the absolute generated path. Editing the
schema invalidates its Bazel generation action. Generated Rust is not checked in.

The shared `prost_generate` rule uses `services = False` for MVT. It invokes the
host Rust generator: `protox` 0.9.1 parses the schema into descriptors, then
`prost-build` 0.14.3 emits Rust messages, matching the runtime `prost` version.
No external `protoc` executable is required. `tonic-prost-build` remains a dependency of the shared host generator
for standby services; it is not a dependency of the MVT runtime.

MVT's name, keys and string values use explicit `raw_strings` storage overrides.
The generator loads the schema descriptor, changes only those selected fields'
Rust storage from strings to bytes, and calls prost-build. Their field numbers,
cardinality and length-delimited wire encoding do not change. This preserves
SQL bytes that are not UTF-8. The C++ compatibility interface preserves the old
NUL-terminated output behavior; native Rust byte values can retain embedded NUL.
The schema's unused extension ranges compile without removal or renumbering.

## Compatibility details

- Layer version remains 2 and the default extent remains 4096. Required fields
  are emitted even when their value equals the schema default.
- `Feature.id` retains optional presence. An absent ID differs from an explicit
  zero. SQL negative signed IDs are still omitted.
- Tags and geometry remain packed repeated `uint32` fields. Geometry command
  order comes from the unchanged C++ visitor.
- The value oneof retains strings, float, double, signed, unsigned, ZigZag
  signed and boolean representations, including zero and false presence.
  Float and double dictionaries compare exact bits, including signed zero and
  NaN payloads.
- Seeded duplicate keys and unused keys remain in order. Lookup selects the
  first case-insensitive match. JSON adds a key before ignoring a null, array
  or object value. The C++ adapter also preserves `ObString::case_compare`'s
  NUL-prefix and full-length behavior.
- Values use owned typed dictionary keys. The old dictionary compared untyped
  memory and retained pointers to scalar stack storage. Rust does not reproduce
  those lifetime defects or collisions between unrelated oneof types. Dictionary
  deduplication and wire byte order can therefore differ while decoded SQL
  properties remain the same.

The codec's generated decoder is not a strict MVT validator. In particular,
prost defaults missing proto2 required fields and drops unknown fields when
re-encoding. The product exposes MVT output, not an MVT decoder SQL function.
The interoperability checks record the legacy decoder's required-field behavior
separately from producer compatibility.

## Dependency removal and tool supply

The runtime inventory found only MVT using protobuf-c. The migration removes its
generated `.pb-c.c` and `.pb-c.h`, C wrapper types, source/header inventories,
native imports, Windows DLL input and observer link input. Package entries are
removed from Linux AL8/EL7/EL8/EL9 variants, macOS manifests, Android and Windows.
The iOS device/simulator and Wasm build recipes and Wasm dependency lock no
longer build or copy protobuf-c.

The shared Rust generator uses protox for both MVT and standby schemas. Its
Bazel action declares the schema and the host Rust executable, with no native
protoc executable or protobuf DLL inputs. Native gRPC/C++ protobuf SDK targets
and protoc aliases are removed. Linux, macOS, Android and Windows no longer request the gRPC SDK package.
Abseil remains a separate dependency. The Windows Abseil package supplies its
headers, import archives and DLL; libcurl imports zlib and system DLLs, without
a dependency on gRPC-bundled c-ares or RE2.
No installed shared SDK or cache is modified by this migration.

Parser commit `3e425f6d4` on `peg-parser-mvp` restores MVT row expansion into
option-count and value/name pairs. That representation still enters
`ObAggregateProcessor::init_asmvt_result`. This migration changes only its key
registration call to `mvt_res.add_key`; it does not require protobuf-c types in
the resolver or undo the parser restoration. The parser worktree is untouched.

## Reproducing correctness and performance checks

Run the focused Rust tests with the generated schema path:

```sh
export MVT_PROTO_RS="$PWD/build/bin/rust/mvt/generated/vector_tile.rs"
cargo test --manifest-path rust/Cargo.toml -p mvt --locked
cargo build --manifest-path rust/Cargo.toml -p mvt --example roundtrip --release --locked
```

The independent harness extracts the old generated C codec from baseline
`4b1aa306d` into its artifact directory and links it only for the comparison. It
needs a local legacy protobuf-c SDK; it does not restore a product dependency or
download one. The Rust decoder argument is the built `roundtrip` executable.

```sh
python3 unittest/share/geo/mvt_codec_compatibility.py \
  --rust-decoder /absolute/path/to/release/examples/roundtrip
```

The driver decodes old output with prost and new output with protobuf-c, then
checks both using an independent Python wire decoder. Cases cover all value
variants, exact floating-point bits, integer boundaries, optional ID presence,
explicit defaults, packed and unpacked input, geometry enums, unknown extensions
and empty layers. Its standalone construction benchmark includes dictionaries,
owned feature geometry, CXX calls, encoding and final result allocation/copy.
It excludes SQL expression evaluation and the shared geometry algorithms.

For full SQL behavior and end-to-end timing, start isolated before/after product
instances and run:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 unittest/share/geo/mvt_sql_contracts.py \
  --before-port 46181 --after-port 46191 --output /absolute/path/sql-results.json
```

The SQL driver checks points, lines, polygons, optional IDs, JSON properties,
null geometry, empty input, options, errors and `_st_asmvtgeom`. Its benchmark
includes 1,000 features, eight string properties and multipoint geometry. It
validates decoded output before timing, warms both sides, and alternates seven
sets of ten aggregate queries. Timings include the client process and SQL result
allocation. Both drivers write raw repetitions and median/range results.

The measurement question is whether complete MVT construction and encoding
changes materially; no performance gain is assumed from replacing the codec.
Report the standalone codec and SQL measurements separately. Cross-target Rust
domain checks with `--no-default-features` do not validate a complete product or
its CXX adapter on that platform.

## Validation results

Measured on the Linux development host on 2026-11-01. The final product build,
13 Rust tests and Clippy with warnings denied passed. All 16 SQL contract cases
matched the old product. Eleven independent codec cases passed, including both
interoperability directions. The final linker action and native symbol audit
contained no protobuf-c input or codec symbols. A temporary schema edit reran
generation; restoring the schema restored byte-identical generated Rust.

The standalone benchmark uses the SDK's old protobuf-c codec with a C++
construction model, not the complete old SQL aggregate class. The new side uses
the real CXX builder and release Rust archive. Both include dictionaries, copied
feature geometry, encoding, and final result allocation/copy. Each result below
is the median of seven alternating repetitions; brackets give the observed
range. The model is slower after migration, with separated ranges in every
workload. This is a remaining performance tradeoff, not a claimed speedup.

| Construction workload | protobuf-c model, microseconds/tile | CXX/prost, microseconds/tile |
| --- | --- | --- |
| 10 features, 6 properties, 12 vertices | 5.97 [5.84, 6.69] | 11.71 [11.34, 14.52] |
| 100 features, 48 properties, 12 vertices | 432.43 [417.44, 517.30] | 644.84 [613.38, 744.15] |
| 100 features, 12 properties, 2,048 vertices | 1,144.63 [1,036.60, 1,230.84] | 4,064.21 [3,981.63, 4,304.33] |
| 1,000 features, 24 properties, 128 vertices | 2,578.64 [2,450.49, 2,942.43] | 5,258.49 [4,855.62, 5,450.78] |

An unreported CPU profiling run collected 507 samples from the new codec.
Layer encoded-length calculation accounted for about 21%, encoding for 17%,
and dictionary lookup/hashing for much of the remainder. Packed integer size
calculation, encoding and dictionary work limit this construction path.

The real SQL benchmark includes 1,000 features, eight string properties and
eight-point multipoint geometry. Both servers used `memory_budget=1G`,
`cpu_count=4`, `datafile_size=1G`, `log_disk_size=4G` and `syslog_level=WARN`.
The driver verifies matching settings and decoded output before timing. It ran
seven alternating batches of ten queries per implementation, after warmup.

| SQL aggregate | Median milliseconds/tile | Observed range |
| --- | --- | --- |
| Old product | 5.161 | 4.971 to 5.551 |
| Migrated product | 5.169 | 5.049 to 5.860 |

There is no measurable end-to-end difference for that SQL workload. Its
two-sided rank-sum p-value is 0.456. This does not rule out a regression for
larger geometry streams. Server-side profiling did not collect enough SQL
samples for a useful breakdown, so the codec profile is not presented as a
profile of complete SQL execution. No throughput gain is claimed.

The Rust domain compiled for Windows GNU, Android, Wasm, iOS device and iOS
simulator with its CXX feature disabled. Full non-Linux products, native Windows
MSVC, macOS, and their CXX adapters were not built in this Linux environment.
Their package/import/link definitions were audited. The standalone legacy
comparison harness uses Linux compiler and SDK conventions.

Raw timings, output fixtures, build logs, generation proof and platform checks
are retained in `/data/1/nijia.nj/build-opt-results/mvt-migration/`, particularly
`results.json`, `sql-results.json`, `platform-checks.json`, `generation-proof.json`
and `final-link.json`. Temporary databases, the saved baseline executable and
the extra Cargo build cache are not needed to retain this evidence.
