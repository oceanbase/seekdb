# seekdb WebAssembly implementation

The target is the seekdb execution and storage engine running locally in a
desktop browser, with SQL, transactions, vector indexes, and recoverable local
storage. The implementation is in progress. There is no usable browser database
artifact verified in a browser yet. A real engine module and async Worker API
now pass the Node integration test described below; persistence is still MEMFS.

Requirements follow [seekdb WebAssembly 可行性调研](https://yuque.antfin.com/obopensrc/tignua/cqo1pglv4ybaovay).
Work began at `a48096008` on `feature/webassembly`, newer than the report's
`57085b5ba` source baseline. Native platform branches remain supported.

## Build and run the current platform gate

Install and activate the SDK version in `tools/wasm/emscripten-version` using
the [official Emscripten SDK instructions](https://emscripten.org/docs/getting_started/downloads.html).
Do not use native seekdb dependency archives as Wasm libraries.

```sh
git clone https://github.com/emscripten-core/emsdk.git /tmp/seekdb-emsdk
cd /tmp/seekdb-emsdk
./emsdk install 4.0.23
./emsdk activate 4.0.23
source ./emsdk_env.sh
cd /path/to/seekdb
bash tools/wasm/build-platform.sh
```

The script fails on a different SDK, a compile/link error, or a failed test.
It builds all 29 source files in the Bazel allocator inventory, selected runtime
support sources, and five pthread-enabled Wasm tests under Node. The real
context allocator now links and runs; this is still a platform/runtime gate,
not an engine bootstrap. The memory runtime uses a 128 MiB fixed linear memory,
a 64 MiB chunk budget, and four prewarmed pthreads. These are test settings,
not measured budgets for a database. The pinned SDK fetches and builds its
checksum-verified zlib 1.3.1 port for the runtime support library.

`tools/wasm/emit-runtime-sources.py` derives the allocator list without exclusions
from `oblib_source_inventory.bzl`. Additional support components are selected by
`tools/wasm/runtime-components.json`; generated CMake and JSON manifests are
written to the build directory. This selected closure is not yet the complete
SQL/storage source and dependency inventory.

For browser validation of the same executable:

```sh
python3 tools/wasm/serve.py build_wasm_platform
```

Open `http://127.0.0.1:8765/runner.html`. The server listens only on loopback and
adds COOP/COEP headers. The page must show both PASS lines and `Process exit: 0`;
the document's `data-result` becomes `passed`. Additional tests are available at
`runner.html?test=stack`, `runner.html?test=futex`,
`runner.html?test=memory_runtime`, and `runner.html?test=io`; each must exit with
code zero. The I/O test uses `PROXY_TO_PTHREAD` so C++ joins and completion waits
leave the JS runtime event loop available for filesystem proxy requests.
This is still a test harness, not the planned database Worker API.

A native reference run exercises the native branches of the shared atomics,
list, thread identity/name, and easy lock tests:

```sh
cmake -S unittest/wasm -B build_wasm_native_reference \
  -DSEEKDB_NATIVE_PLATFORM_REFERENCE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm_native_reference --parallel 4
ctest --test-dir build_wasm_native_reference --output-on-failure
```

## Build the SQL front end

The production-side Wasm CMake entry point is now `src/wasm/CMakeLists.txt`.
It currently builds the memory/thread libraries, all 15 parser sources from the
SQL inventory, five generated lexer/grammar sources, and seven charset sources.
This front-end target does not execute SQL. The full-engine lifecycle probe
described below now boots and closes the real database under Node/MEMFS;
the async Worker API now executes the real engine under Node worker_threads.

## Database module and asynchronous Worker API

After configuring the engine and its pinned dependencies, build the application
module explicitly:

```sh
cmake --build build_wasm_engine --target seekdb_wasm_database test_wasm_log_ring --parallel 3
"$EMSDK_NODE" unittest/wasm/test_database_worker.mjs build_wasm_engine/seekdb_wasm_database.mjs
"$EMSDK_NODE" build_wasm_engine/test_wasm_log_ring.js
```

The output directory contains `seekdb_wasm_database.mjs`, its `.wasm`, and the
eight companion `.mjs` files copied from `src/wasm`. Serve these together from
the same origin with COOP/COEP headers. The browser entry point is:

```js
import {Database} from './database.mjs';

const db = await Database.open({
  moduleURL: new URL('./seekdb_wasm_database.mjs', import.meta.url),
  budgets: {memoryMiB: 1024, allocatorMiB: 1536, cpuCount: 2},
});
try {
  const session = await db.connect();
  try {
    for await (const event of session.query('SELECT 1')) {
      if (event.kind === 'row') {
        console.log(event.values.map(value => value === null ? null : new TextDecoder().decode(value)));
      }
    }
  } finally { await session.close(); }
} finally { await db.close(); }
```

Each `open` creates an independent Worker and module. Sessions retain SQL
transaction state across queries. Queries produce `columns`, `row` and
`complete` events on demand; bytes and nulls remain distinct, integer result
text is not converted to JS Number, and OK counters retain BigInt through
structured clone. One session permits one active query. `SqlError` preserves
the server code and SQLSTATE and leaves the session usable. An AbortSignal
passed to `query(sql, {signal})`, or abandoning its iterator before completion,
closes that session; create another session with `connect()` to continue.
Results are copied out of Wasm and become reclaimable when the caller drops
each event. `close()` releases sessions, awaits the native stop/wait/destroy
and runtime exit, then terminates the Worker. Runtime faults reject pending
requests and terminate the Worker.

Storage is currently **MEMFS only**. Reopening means starting a fresh empty
database; it does not recover prior data. OPFS, import/export and durable commit
semantics remain implementation work. The module currently prewarms 64 pthread
workers, starts with 512 MiB linear memory, and permits growth to 2 GiB. These
are configured development budgets, not measured minimum browser requirements.
Host budgets additionally control the engine memory, allocator, cache,
memstore, vector memory, CPU count, initial datafile and log disk sizes. The
engine still validates its own constraints and rejects insufficient budgets.

The packaged-module integration test passes with Emscripten 4.0.23 and Node
24.19.0 on Wasm SHA-256
`c2ed61e0614e6e973b1939c8598ebbca4b58ce2a90ddcc356a51d6ea8c90d7b2`.
It uses actual worker_threads and the copied distribution files, and verifies
authentication, fragmented memory transport, exact uint64/Unicode/NULL,
CRUD and commit/rollback, SQL error recovery, disconnect rollback/write-lock
release, result abandonment, cancellation, full shutdown, fresh-Worker reopen,
close during a query, module-load failure, and startup failure cleanup. Browser
Worker behavior, native SQL equivalence, cancellation latency, and persistence
are not proved by this Node run.

An initial Worker run exposed a log-item destructor reading an invalid vtable.
A separate four-producer ring test reproduced partially published payloads
before the fix. Log ring entry headers and the write position now use atomic
release stores, matching the consumer's acquire reads, instead of mixed plain
bitfield/64-bit stores and atomic reads. The same 200,000-record test, including
rollback and repeated wraparound in a 4 KiB ring, passes after the change in
Wasm and a native macOS build. The complete Worker test passes after the fix.

### Browser validation entry point

For interactive SQL, open `http://127.0.0.1:8766/database-console.html` on the
same server. Choose **连接数据库**, then **执行 SQL** or **创建示例表和数据**.
The console retains one root session, so `BEGIN`, edits and `COMMIT`/`ROLLBACK`
can be submitted as separate statements. Results show column names, text and
NULL without converting integer strings to JS numbers; only the first 500 rows
are rendered, while the complete result is consumed. Each page owns its own
in-memory database. Closing the database or refreshing loses its data. Port
8766 serves the web files; it is not a MySQL TCP endpoint for external clients.

The build also copies `database-browser.html` and `database-browser-cases.mjs`.
Serve the build directory and open the database page:

```sh
python3 tools/wasm/serve.py build_wasm_engine --port 8766
```

Open `http://127.0.0.1:8766/database-browser.html` and choose **运行验证**.
Success requires all SQL/transaction, disconnect/cancel, shutdown and fresh
reopen cases to pass. The page exposes `window.seekdbTestResult`, including
browser user agent, isolation state, timestamps, individual case messages and
any error; the document's `data-result` becomes `passed` only after completion.
The portable cases were executed under Node workers before browser delivery:

```sh
"$EMSDK_NODE" unittest/wasm/test_browser_cases_node.mjs build_wasm_engine/seekdb_wasm_database.mjs
```

The Node run checks the case implementation and Worker protocol. On 2026-09-08,
the same page also passed in the Codex in-app browser on the current
`c2ed61e0` Wasm artifact: SQL/transactions, disconnect/cancellation, native
shutdown/Worker termination and fresh MEMFS reopen. Initial open took 2368 ms
in this single run; this is not a performance baseline or multibrowser coverage.

Do not open the source HTML with `file://`: its relative module graph requires
the packaged build files, and database threads require cross-origin isolation
provided by the server's COOP/COEP headers. The page now shows a launch hint for
file URLs and catches dynamic module-load errors after installing the button
handler, so a failed import no longer leaves the button silently unresponsive.

### Real SQL search integration

```sh
"$EMSDK_NODE" unittest/wasm/test_database_search.mjs build_wasm_engine/seekdb_wasm_database.mjs
```

This test passes on the same `c2ed61e0...` artifact with real SQL workers:

- A host-supplied 32-row, 3-dimensional vector fixture checks exact L2 and
  approximate top-3 results. EXPLAIN must contain `VECTOR INDEX SCAN` on the
  HNSW index. Committed update/delete and rollback preserve expected results.
- A four-document English fixture creates a real FULLTEXT index and checks
  MATCH/AGAINST results after insert, update, delete and rollback.
- A SQL join combines ANN candidates with a fulltext predicate and checks its
  result. This is a composed retrieval path, not ranking-fusion validation.
- DROP DATABASE and native shutdown complete with exit zero. The packaged
  Worker lifecycle regression also passes, including startup failure cleanup.

Two additional wasm32 varargs defects were fixed to reach these paths. The
table-history consistency query used LP64 formats before subsequent string
arguments, corrupting its generated SQL. Fulltext parser serialization passed
an int64 length as the `%.*s` precision and used `%ld` for its version. These
now use the correct precision type and PRI64 formats. Log shutdown now keeps
the ring consumer alive through module joins; stopping it earlier caused
thousands of allocation failures while background services still logged.

These fixtures do not establish high-dimensional recall, persistent index
refresh/rebuild/recovery, all parser languages or index configurations, native
SQL equivalence, browser compatibility, or workload performance. The earlier
malformed internal DDL run also timed out during cleanup; failure injection
must cover partial DDL rollback under the final persistence implementation.

### Persistence integration boundary

The pinned Emscripten 4.0.23 sources were checked before selecting a backend.
Its WasmFS `fcntl` implementation returns success for process-level lock
requests without enforcing them; `__wasi_fd_sync` also returns success for
directories without syncing directory metadata. The OPFS backend delegates
file moves to the host handle and does not implement directory moves. Therefore
enabling WasmFS/OPFS alone cannot satisfy seekdb's file, PALF and SQLite
contracts. Storage ownership, lock enforcement, namespace operations and
commit/recovery boundaries still need an explicit adapter and fault tests.

## Configure engine dependencies

With the pinned Emscripten SDK activated:

```sh
bash tools/wasm/build-deps.sh
# Install the pinned Rust toolchain described below; rustup/cargo must be on PATH.
bash tools/wasm/build-rust-nio.sh
bash tools/wasm/build-engine.sh
```

Host parser generation requires bison 2.4.1 and flex. CMake defaults to the
existing native dependency tools in `deps/3rd/usr/local/oceanbase/devtools`;
use `SEEKDB_HOST_DEVTOOLS`, `SEEKDB_HOST_BISON`, `SEEKDB_HOST_FLEX`, and
`SEEKDB_BISON_DATA` for another host installation. The selected fast_float and RapidJSON headers come from `SEEKDB_HEADER_DEPS`.
S2 and Abseil headers now come from their pinned target builds; their native
package versions are 0.10.0 and 20211102.0 respectively.

`build-deps.sh` verifies the official OpenSSL 1.1.1u source archive against
`dependencies.json` and builds static wasm32 libraries with pthreads. It needs
Python with tarfile's data extraction filter (Python 3.12+). It generates the
32-bit OpenSSL configuration rather than importing the native LP64 one. Native
assembly, sockets, engines, dynamic loading and async fibers are disabled for
this dependency build. This is currently used to satisfy the geometry header
closure and to verify big-integer arithmetic, not to provide browser TLS.
Use `-DSEEKDB_WASM_DEPS=/path/to/prefix` with `build-engine.sh` for a custom prefix.

The SQL test exercises the real grammar and AST for CREATE, INSERT, SELECT,
UPDATE, DELETE, BEGIN, COMMIT, and ROLLBACK syntax, with 64-bit integer values,
UTF-8 input, invalid SQL, repeated arena teardown, and PL callback validation.
The grammar cases run concurrently on the main runtime thread and two pthreads,
with a separate arena and parser result for each call.
These transaction tests validate syntax only, not transaction execution.
The same test checks datum slot offsets and a 128-bit decimal BIGNUM roundtrip.
Both parser C sources and the final test link use `-Oz`: the original `-O3`
artifact completed its assertions but then crashed in Node 24.19.0's background
Wasm optimizing compiler. The compact build passes through process exit without
Node flags that disable optimization. Browser verification is still pending.

System-table schema generation now shares a CMake function with the native
build. The generated schema header matches the existing native generated header.

The same entry point exposes full native module source inventories for SQL,
storage, share, observer, logservice, rootserver, PL, query, data-plane, OBLib
common/lib, RPC, compression, and restore. It reuses the production
inventory translator and preserves its Unity groups and standalone sources.
These optional compile targets are outside the default front-end build:

```sh
cmake --build build_wasm_engine --target seekdb_wasm_engine_objects --parallel 4
```

Each selected module has now compiled separately: SQL, storage, share,
observer, logservice, rootserver, PL, query, data-plane, OBLib common/lib,
RPC, compression, and restore. The complete rebuild with the selected
`-fexceptions` configuration also passes compilation for all these modules.
These are static archives, not a linked engine; third-party
implementations and browser transport/lifecycle still need closure before startup.
Engine declarations also use selected curl and Boost source headers from
`SEEKDB_HEADER_DEPS`; native HTTP model requests are explicitly unsupported
in the browser build, with vectors supplied by the host. VSAG's public
index factory now links, with query/mutation/stream-restore component tests. Compiled VSAG, libxml2, ICU, S2, Abseil and protobuf-c use headers installed with their
target libraries.
Vector/network execution is not yet usable. The current
native header bundle contains ICU 69.1, libxml2 2.10.4, and protobuf-c 1.4.1.
The SDK ICU port is 68.2; `build-icu.sh` instead builds matching 69.1 sources.
SQLite's newer standalone Wasm OPFS/WAL support cannot be assumed for this engine dependency.

The Boost 1.74 native header bundle needs the
[upstream MPL C++11 next/prior fix](https://github.com/boostorg/mpl/pull/77) and
[numeric-conversion enum fix](https://github.com/boostorg/numeric_conversion/commit/50a1eae942effb0)
for the SDK's Clang. `prepare-headers.py` applies those backports only to the build
copy, verifies input/output SHA-256 values from `dependencies.json`, and rejects
an unverified header revision. It does not suppress the enum constant-expression
error or change the native dependency installation. S2's copied `base/port.h`
selects the SDK's musl byteswap header to avoid redefinition and macro collisions;
that adaptation is also input/output hash pinned. Repeating header preparation
preserves existing header timestamps, including patched files, so a
CMake reconfigure does not force a full engine rebuild.

SQLite 3.38.1 now has a pinned target build. `build-deps.sh` invokes
`build-sqlite.sh`; the latter can also build only SQLite into the same prefix.
Its archive SHA-256 and amalgamation SHA3-256 are recorded in `dependencies.json`
and checked before compilation, with the latter matching the
[official release checksum](https://www.sqlite.org/releaselog/3_38_1.html).
The build preserves serialized pthread mutexes and WAL code, and omits dynamic
extension loading. Its Unix VFS is an intermediate MEMFS backend, not OPFS.
The `wasm_sqlite_metadata` test, verified on 2026-09-08 under Node 24.19.0,
links the production `ObSQLiteConnection` adapter
and runs three threads with separate in-memory connections, repeated open/close,
parameter binding, int64 limits, UTF-8 text, binary blobs, commit, rollback,
constraint failures, and invalid statements. It checks the linked SQLite source
ID against its header. This is metadata-adapter evidence, not execution by the
seekdb SQL engine or durable recovery.

The startup link diagnostic retains the native lifecycle: `ObServer::init`,
`start`, `wait`, `destroy`, and the stop request, with undefined symbols treated
as errors:

```sh
cmake --build build_wasm_engine --target seekdb_wasm_engine_link_probe --parallel 3
```

The full lifecycle link succeeds after linking the actual Rust memory NIO
archive: all 33 remaining unresolved symbols are resolved. The generated Wasm
is approximately 94 MiB before any deployment compression. The native
HTTP dependencies have been removed from this browser-local configuration.
This is the complete lifecycle probe, not just the VSAG test.
The public factory is now resolved, following the earlier initialization/options/
dataset integration that reduced the diagnostic from 54 to 49 symbols. A complete rebuild under the selected JavaScript
exception model also resolved the intermediate `emscripten_longjmp` mismatch.
Browser builds disable kernel telemetry and return `OB_NOT_SUPPORTED` before
accepting native HTTP model jobs, including both AI SQL client and embedding-task
initialization. No network success or fabricated embedding is returned. Host
supplied vectors, distance kernels and local index algorithms remain enabled.
A browser Fetch/model integration is a separate capability, still unimplemented.
The probe is intentionally
outside CTest and is not a JavaScript API or a bootstrap execution test. Generated system-package SQL uses
the same generation function as the native build and its generated C++ matches
the existing native output byte for byte. Build metadata is generated from the
actual source revision and SDK. Standby uses the production disabled-module
implementation, retaining its existing primary-role checks.

CRoaring 3.0.0 and libxml2 2.10.4 have pinned source builds invoked by
`build-deps.sh`. The former passes 64-bit boundary keys, ordered iteration,
set operations, portable serialization, copy/removal and teardown. The seekdb
bitmap allocation header now writes its size after the reserved 64-bit field
on wasm32. Actual bitmap allocator OOM/exception propagation is not yet verified.
The XML build retains pthreads and iconv, and disables HTTP/FTP, dynamic modules,
and compressed-file input. Its test runs the production seekdb XML parser and
tree construction, checking Unicode, entities, attributes, malformed documents,
error recovery between contexts, and concurrent threads.

ICU 69.1 is built from the pinned official source archive by `build-icu.sh`.
Its common and i18n sources follow the SDK port's grouping, with pthreads,
static linkage, and no dynamic loading or filesystem data lookup. The full
27 MiB little-endian Unicode data package is separately hash-checked and
embedded as aligned static data; this is not an empty stub-data library.
The test directly runs `ObExprRegexContext` and its real inplace allocator:
Unicode Han properties, positions, substrings, replacements, case folding,
supplementary-plane characters, invalid patterns, and concurrent context reuse.
The same test passes on macOS arm64 against native ICU 69.1 with the same official
data package, providing a result comparison. The current standalone Wasm regex
test is about 28 MiB uncompressed, mostly data; this is not a product bundle
size measurement or an agreed browser memory budget.

S2 0.10.0 and Abseil 20211102.0 are built by `build-s2.sh` from pinned official
archives. Their headers match the native dependency versions. S2's hardcoded
C++11 setting is adapted to C++20 with input/output checksums, matching Abseil
and the engine's standard-library type selection. Its byteswap adaptation is
also applied to the target library source. The test links the production
`ObSpatialMBR` implementation and verifies spatial filtering, antimeridian
bounds and region covering, exact 64-bit face/cell IDs, cell normalization,
point/center precision and spherical polygon predicates across three threads.
The same assertions pass against the native S2/Abseil libraries on macOS arm64.
These are geometry component checks, not a SQL spatial-query execution test.

`build-protobuf-c.sh` builds protobuf-c 1.4.1. Its test compiles seekdb's actual
vector-tile descriptors, compares known wire bytes for integer boundaries,
zigzag values, doubles and packed geometry fields, and round-trips a tile with
Unicode keys. Injecting failure at each decoder allocation returns an error
with all earlier allocations reclaimed; truncated input is also rejected.
The same wire-byte and allocation-failure tests pass natively. This verifies
vector-tile encoding, not vector similarity search or HNSW.

`build-vsag.sh` pins VSAG commit `129b82c`: all 26 public headers match the
installed native package, whose object version is `129b82c-dirty` despite its
package name `1.1.0`. It builds the full upstream distance-module source list
with generic kernels, plus the actual initialization, options, dataset, JSON
wrapper, constants and logger implementations. The dependencies cpuinfo
`ca678952a9a8eaa6de112d154e8e104b22f9ab3f`, fmt 10.2.1, spdlog 1.12.0,
nlohmann/json 3.11.3, robin-map 1.4.0 and ThreadPool
`3507796e172d36555b47d6191f170823d9f6b12c` are downloaded from official archives
and SHA-256 checked. cpuinfo already contains Emscripten code but its CMake
platform gate excludes it; a hash-checked patch enables that existing code.
Its estimated CPU topology is not a database memory or thread budget.
VSAG/fmt use their supported C++17 mode because fmt 10.2.1's C++20 consteval
parser fails with the pinned SDK compiler. Public VSAG headers remain unchanged.

The distance test checks 22 dimensions from 0 to 1536, float-aligned inputs
without requiring SIMD alignment, L2 and inner product, four distinct batch
lanes, normalization including zero vectors, int8 extrema, nonfinite inputs,
known nearest-neighbor ordering, and three concurrent callers against independent
double/integer references. The same test passes with the native arm64 distance
archive. This is a scalar numerical baseline, not HNSW or a performance result.
The runtime test checks real initialization, option rejection without mutation,
64-bit values across the wasm32 ABI, borrowed and copied dataset ownership, and
cleanup after each injected allocation failure in a dense dataset copy.

That runtime test exposed an upstream `DatasetImpl::DeepCopy` null dereference:
the optional `Paths` array was read unconditionally. The unmodified native
archive crashes in that loop; Wasm's readable address zero masked it until an
explicit absent-path assertion was added. A hash-checked source patch preserves
absent paths and copies present Unicode paths. The revised test passes on Wasm
and natively with the same patched dataset source linked against the remaining
native VSAG archives. The unmodified native runtime is not reported as passing.

Engine and VSAG compilation/linking now explicitly use Emscripten's JavaScript
exception model (`-fexceptions`). This preserves the parser's JS `longjmp` model
and enables C++ catch/cleanup paths used by the vector dependency. An experiment
with `-fwasm-exceptions` passed the SQL assertions but crashed Node 24.19.0's
background V8 optimizer with a Zone OOM, including at `-Oz`; it is not the
selected build mode. No JIT-disabling flags are used to obtain a passing test.
Third-party exception/OOM paths beyond these tests still need auditing.

The same dependency build now includes VSAG's actual `HierarchicalNSW` class,
block manager, iterator context, allocator and stream support. Its regression
test inserts 40 eight-dimensional vectors with positive and negative labels
outside the 32-bit range, compares top-five results to an independent exact
scan, runs three concurrent readers, updates a vector, marks a label deleted,
and checks the results again after serialization and restoration. Restoration
uses the same memory-initialization order as the public VSAG factory. The test
validates legitimate equal-distance ties and requires all allocator-owned blocks
to be released. The same test passes against the native arm64 VSAG archives.
This is a direct HNSW component test. The separate public factory test below
now exercises HNSW and HGraph; seekdb's SQL adapter and browser persistence
remain unverified.

The public HNSW `Index` implementation, filters, conjugate graph, ODescent,
quantizers (including RaBitQ/PQ), transforms, graph data cells and I/O sources
now compile for Wasm as well. seekdb's HNSW creation parameters do not request
VSAG's static PQ mode. A hash-checked optional-build guard removes that mode's
DiskANN dependency and throws `UNSUPPORTED_INDEX` for explicit requests; this
error path is now verified by both the link probe and the public factory test. Unused
DiskANN includes were removed from ODescent/k-means, and k-means' OpenMP thread
settings are guarded by `_OPENMP`; its actual pthread task execution remains.
No BLAS computations are replaced with stubs. The dependency build follows
upstream I/O, quantization and transform source inventories.

`cmake --build build_wasm_engine --target seekdb_wasm_vsag_index_link_probe`
retains the real HNSW vtable and now passes strict linking and execution. Its
BLAS/LAPACK and `InnerIndexInterface::FastCreateIndex` dependencies are resolved.
The dependency build includes the actual Factory/Engine, HGraph, SINDI, IVF,
Pyramid, sparse and brute-force source closures. DiskANN is an optional disabled
factory branch and returns `UNSUPPORTED_INDEX`; seekdb's HNSW/HGraph/SINDI
branches remain present. The actual ANTLR4 4.13.2 C++ runtime and VSAG's generated
FC parser are included, with the upstream archive SHA-256 pinned in the manifest.
Build-private forwarding headers preserve canonical header identity for VSAG's
two include conventions. This does not yet verify all factory branches or the
attribute-filter language.

`wasm_vsag_factory` calls the public `vsag::Factory::CreateIndex` API for HNSW,
HGraph FP32, HGraph SQ8 without refinement, and RaBitQ with FP32 refinement. It
builds/adds 32 eight-dimensional vectors with signed 64-bit labels, checks five
queries against an independent exact scan, updates vectors and labels, deletes
a label, runs three concurrent readers, and repeats checks after stream restore.
FP32/refined results use strict numerical/ranking checks; SQ8 requires the exact
nearest label and bounds its distance/ranking error by 0.05 on this fixture.
Deletion support is explicitly enabled. This configuration does not cover every
parameter emitted by seekdb's adapter. Allocator-owned memory must be released.
The same fixtures pass against the native arm64 VSAG archives.

HGraph restore exposed a wasm32 serialization error: `SparseGraphDataCell` wrote
a `size_t` count but read a `uint64_t`. The manifest's hash-checked patch writes
the declared 64-bit field, and restore now passes on both target and native.
This does not establish compatibility or bounds safety for every serialization
field. The component target uses six prestarted workers and 64 MiB initial
memory growing to 512 MiB; these are test settings, not measured engine budgets.
All 19 engine-side component CTests pass under the pinned Node runtime,
including the production log-block test described below. The full
engine's separate startup-link result above remains incomplete.

`wasm_vsag_index_support` exercises the actual VSAG bitset using the engine's
single CRoaring 3.0.0 library (VSAG upstream requests 3.0.1): concurrent setters,
bit removal, the maximum 32-bit bit position, and serialization/restoration.
It also covers conjugate-graph recovery with positive and negative 64-bit labels.
The new graph test failed on Wasm because `AddNeighbor` counted a neighbor-count
field using `sizeof(size_t)` while serialization writes `uint64_t`. The resulting
four-byte shortfall per adjacency list put the recovery footer at the wrong
offset. A hash-checked fix counts the actual serialized type. The regression now
passes on Wasm and against the unmodified native arm64 VSAG/CRoaring archives.
This does not establish general index-format or malformed-input safety.

The numerical dependency now uses the single-precision reference closure from
[Netlib CLAPACK 3.2.1](https://www.netlib.org/clapack/) and the CBLAS/LAPACKE C
interfaces shipped with OpenBLAS 0.3.23. Both source archives are SHA-256 pinned.
No native Fortran or OpenMP runtime is linked. The target explicitly checks the
wasm32/f2c integer ABI and patches the C interface return declarations to match
the actual f2c functions; the BLAS test treats linker warnings as errors.
CBLAS error state is thread-local. The four LAPACK entry points share a mutex
because the reference implementation caches machine constants; BLAS products
remain concurrent. SLAMC2's warning uses stdio instead of importing the Fortran
formatted-I/O runtime. This is a serial reference backend, not a SIMD or
performance result.

`wasm_vsag_blas` tests all eight operations through VSAG's real `BlasFunction`
interface: padded row/column-major matrices, transpose/conjugate-transpose,
negative vector strides, zero scalars and empty inputs, independent matrix
products, QR reconstruction/orthogonality, pivoted rectangular LU, symmetric
eigenvalue/eigenvector residuals, singular/invalid/NaN inputs, and three concurrent
callers beginning with uninitialized LAPACK caches. The same numerical tests pass
against the native VSAG/OpenBLAS archives. Wasm-only checks reject invalid or
unaddressable matrices before LAPACKE's NaN scan, and inject seven actual malloc
failures across workspace/transpose allocations in all four LAPACK entry points.
Failures return memory errors and preserve input matrices; a subsequent QR call
succeeds. Quantized index quality, full index allocation-failure behavior, and
application memory budgets still need end-to-end validation.

Wasm compilation also exposed unchecked narrowing of serialized 64-bit string
lengths to `size_t`. A hash-checked patch rejects string/vector counts exceeding
the target container/address limits before allocation, resizing or payload reads.
The stream test covers both standard and VSAG-allocated vectors, high-bit values,
empty values, embedded-NUL Unicode text, oversized lengths and truncated payloads.
It passes on Wasm and natively with the patched header. These checks do not
establish safety for every other index deserialization field or OPFS recovery.
The default VSAG allocator also rejects 64-bit sizes above `SIZE_MAX` before
calling `malloc` or `realloc`. The same stream test exercises the production
allocator through a fixture compiled with its release class layout, verifies
normal growth, and verifies that an oversized Wasm reallocation leaves the
original bytes intact. Linking the unpatched allocator source makes the
oversized-allocation assertion fail: a request for 4 GiB plus 32 bytes used to
allocate only 32 bytes. This does not replace the remaining engine memory-budget
or complete HNSW allocation-failure audit.


`wasm_log_block` links the production `ObServerLogBlockMgr` and PALF file helpers
as individual sources, without the broader service Unity initialization graph.
It injects ENOSPC after a 17-byte partial write and verifies failure returns,
the partial file is removed, and allocated-block usage stays zero. A retry
creates the actual 64 MiB PALF block; the test reads every byte, verifies zeros
and usage accounting, then checks that an O_EXCL collision neither deletes nor
changes the existing block. This target uses a 256 MiB fixed test memory and two
prestarted workers. It verifies allocation behavior under MEMFS, not database
startup, durable commit, or flush-failure recovery.

Rust interoperability has a separate reproducible gate:

```sh
rustup toolchain install nightly-2026-09-07 --profile minimal \
  --component rust-src --target wasm32-unknown-emscripten
bash tools/wasm/build-rust-runtime.sh
```

The script explicitly selects Rust 1.100.0-nightly (`5a2be9f5f`, 2026-09-06),
rebuilds `std` for the pinned Emscripten with atomics/bulk memory and
`panic=abort`, and links the resulting static library into a C++ pthread program.
The [Rust target documentation](https://doc.rust-lang.org/rustc/platform-support/wasm32-unknown-emscripten.html#emscripten-abi-compatibility)
requires matching standard-library and Emscripten ABIs; this build does not use
the precompiled standard library as proof of thread compatibility. Rust panics
abort; they do not unwind through seekdb's JS-exception C++ frames.

The unmodified rebuilt standard library failed the test: zero Rust TLS
destructors ran after both C++ and Rust thread exit. Its Wasm cleanup guard
explicitly has no thread-exit implementation. A source/input/output hash-checked
patch selects the existing pthread-key destructor guard for Emscripten. The
script stages a private sysroot and uses a compiler wrapper; rustup's installed
compiler and standard-library sources remain unmodified. With that patch, all
12 TLS destructors run across three rounds of C++-created and Rust-created
threads, including worker reuse. The test also checks u64 values above 32 bits,
120,000 shared atomic and mutex-protected updates, Rust Vec allocation, joins,
barriers and condition-variable notifications, and exits successfully on
Node 24.19.0. The same C++/Rust fixtures also pass natively with Rust 1.97.1
and macOS pthreads. Its four-worker, 64 MiB memory configuration is a runtime test
budget, not a database minimum. The pinned nightly still warns that the Wasm
atomics target feature is unstable. Rust OOM/panic recovery and browser
execution remain pending.

Rust NIO now has a `memory-transport` feature, built without the default native
network feature by `tools/wasm/build-rust-nio.sh`. It uses bounded in-process
byte streams and condition-variable readiness notifications with the existing
reactor, MySQL parser, response writer, and request generation/commit lifecycle.
It opens no socket or discovery file and rejects TLS configuration. The separate
`nio_memory.h` C ABI provides copying, nonblocking connect/read/write/close
operations; clients may drain delivered output after reactor destruction.
The host must supply and sequence actual MySQL packets; SQL authentication and
execution remain the responsibilities of the production C++ callbacks.

The native and Wasm protocol fixtures pass partial greetings with one- and
seven-byte queues, login, pipelined commands, 64-bit response values, a 64 KiB
response with blocking flush, concurrent progress on another connection,
client-close cancellation, stale-generation rejection, and exactly-once
disconnect/close. They also verify EOF while a bound SQL session still retains
the connection storage, then release it before reactor destruction. A Rust
readiness test checks that undrained input does not continually wake the reactor
and that new input and explicit rearming still deliver events. The greeting
sender now stages a blocked tail instead of spinning during admission.
These fixtures return protocol test data; they do not execute SQL.

The NIO build uses the same private patched standard library as the Rust runtime
gate. Its test starts two Rust IO threads and two C++ workers with a six-worker,
64 MiB Emscripten configuration. CMake's `SEEKDB_WASM_RUST_DIR` selects this build
directory. A manifest checks source/configuration hashes, the standard-library
patch, and the archive hash before linking, so changes require rebuilding NIO.

The RPC page pool rejects negative/overflowing sizes and retains the embedded
pool object's prefix during reuse. Its Wasm test explicitly exercises small
allocations in the retained page; restoring the old reset-to-zero behavior makes
the test fail. The same test passes natively with both jemalloc and obmalloc,
using compile definitions matching the native libraries. NIO pointer-bearing
struct layout assertions now cover 32-bit and 64-bit targets in both C++ and
Rust; native Rust checks and the native C++ ABI assertions pass. The memory
transport additionally exercises these layouts across the Wasm C++/Rust boundary.

## Implemented platform changes

- Wasm fences and spin hints; i64 atomic operations retain 64-bit counters.
- CAS128 and LOAD128 use Emscripten compiler-rt's common address-based lock
  implementation. They are not lock-free. Every shared access to a 128-bit
  object must use the same atomic protocol; the caller audit remains open.
- The wasm32 atomic-list head packs a 32-bit pointer and 32-bit ABA version in
  one i64 atomic word. Shared link reads/writes are atomic as well. Tests retain
  node storage throughout contention; this does not establish memory reclamation
  safety for arbitrary users of the list.
- Character-set pointer, long, and size_t sizes follow the target ABI.
- Emscripten thread identity and diagnostic thread-name support, POSIX
  strerror_r handling, and cache-line padding.
- Chunk allocation uses the Emscripten builtin allocator to avoid malloc-hook
  recursion, requests alignment directly, and preserves mmap's initial zero
  contents. Complete chunks are charged because their tail consumes linear
  memory. Free returns storage to the allocator; it cannot shrink Wasm memory.
- Page washing reports ENOTSUP rather than falsely decrementing memory charges.
  Both chunk-cache trimming and block washing use this behavior.
- Shared easy structure declarations can compile without pulling in native
  OpenSSL implementation headers. Native transport removal is still unfinished.

- Stack bounds and overflow checks use the actual Emscripten stack on each
  pthread. Native stack switching explicitly returns `OB_SIZE_OVERFLOW` without
  running the callback. Backtraces use the pinned SDK's stack snapshot/unwind
  interface; pointer values are widened individually for diagnostic formatting.
- Futex wait/wake use Emscripten's atomics-backed implementation, preserving
  timeout and wake-count semantics. Thread creation uses pthread-managed stacks,
  rejects wasm32 size truncation, and skips native pthread-memory inspection.
  Native TLS certificate inspection returns `OB_NOT_SUPPORTED` in the local
  browser build. CPU cache sizes use documented tuning defaults.
- Pointer-tagged queue operations use `uintptr_t`-sized atomics, including
  pointer slots that are only four-byte aligned. The tiny hazard-pointer
  allocator computes its block capacity from the actual target layout.
- Wasm32 `ObDatum` retains the 12-byte slot and descriptor offset 8; storage
  datum inline buffers remain at offset 16. Dynamic expression buffer headers
  keep their native 16-byte size through explicit alignment.
- SQL grammar symbol lookup now uses a callback supplied by PL preparation.
  A namespace without a resolver is rejected. The native PL resolver is retained;
  ordinary SQL grammar no longer requires the whole PL runtime just to link.
- Diagnostic string formatting is shared from a separate OBLib source file so
  memory/thread startup does not link the native RPC transport just for it.
- The Wasm local-device path performs real `pread`/`pwrite`, reserves capacity
  before submission, and delivers each completion once through a bounded queue.
  Errors and short reads retain their actual results. Cancellation returns
  `-EAGAIN` because the synchronous syscall cannot be cancelled; the completion
  still releases the channel's request reference. Context destruction requires
  the owning channel to quiesce submitters and join consumers.
- PALF block reuse writes zeros without Linux `FALLOC_FL_ZERO_RANGE`, retries
  interrupted/short writes, preserves bytes outside the range, and propagates
  failures. It does not replace the existing flush boundary. MEMFS does not
  supply persistent storage, regardless of these I/O checks.
- New data and log files, plus serialized data-file expansion, write their
  missing tail with a bounded zero buffer. They preserve existing bytes and
  propagate short-write, allocation and quota errors. Retrying an extension
  continues after the partial tail; a smaller request never shrinks the file.
  Failed new-file allocation closes/unlinks only the file it created with
  `O_EXCL`. Wasm log-block creation returns allocation failure instead of retrying
  forever. Arbitrary range reservation through the generic device `fallocate`
  API returns `OB_NOT_SUPPORTED`; the inspected production allocation sites
  use explicit growth. This is not Linux physical-space reservation or durable
  commit. The shared growth tests pass on Wasm/MEMFS and native macOS, with
  Wasm quota injection. Engine startup and OPFS recovery remain unverified.
- Storage sequence masks and long timeouts use explicitly 64-bit constants.
  The wasm32 B-tree iterator has its own 2048-byte layout assertion; its storage
  continues to use `sizeof` rather than the native pointer layout.
- Integer formatting uses `PRId64`/`PRIu64`, including volatile integers and
  unsigned enums. Wasm32 `size_t` retains vi64 serialization; decoding rejects
  negative or overlarge values without publishing a partial value or position.
- Ordered numeric encoding uses local byte-swap helpers, avoiding a macro
  collision with S2. The real encoder passes signed/unsigned boundary ordering,
  guard-byte, unaligned-output, and known floating-point byte tests under Node.
- SQL zlib calls use `uLongf` output-length pointers on every platform and
  bound compressed input after its four-byte header. Virtual expression-codegen
  lookup reads pointer-sized member-function fields on wasm32.
- OBLib's SSE4.1 integer codecs map through Emscripten to SIMD128. Tests compare
  packed bytes with an independent bit-level reference for every 16/32-bit width,
  including unaligned buffers and guards, and check delta/prefix-sum roundtrips
  across SIMD block boundaries. This makes SIMD128 a requirement for this codec
  target; it does not prove vector-index or distance-kernel performance.
  The same integer-codec test also passes against the existing macOS arm64
  `build_release` OBLib and jemalloc archives, exercising the native NEON codec
  and hardware CRC32C path as a numerical/byte-format reference.
- CRC32C uses the existing slicing-by-eight table implementation on Wasm;
  entry points preserve native raw CRC state and high-bit truncation on input.
  The implementation is selected at initialization, without a first-call race.
  Tests cover the standard `123456789` check value, independent bitwise results
  for eight alignments and 130 lengths, 64-bit seed edges, and chunked updates.
- Hashing supports wasm32 pointer-sized `long` types. The real seekdb hash set
  runs insertion, lookup, and removal for zero, an aligned address value, and
  `UINTPTR_MAX`, using the production bucket-size prime table.

## Evidence and outstanding gates

| Gate | Current evidence | Still required |
| --- | --- | --- |
| G0: platform primitives and dependency closure | Emscripten 4.0.23 wasm32: real allocator links and runs; atomic, stack/backtrace, futex and memory/thread Node tests pass | Browser runs; engine stack budgets; CAS128/list caller audit; target ABI/varargs audit; minimal service thread and I/O wait graph; complete dependency/source inventory |
| G1: browser memory database | Node real SQL lifecycle and production async Worker API pass: authentication, CRUD, commit/rollback, invalid-SQL recovery, disconnect rollback/write-lock release, result abandonment, cancellation, complete shutdown, fresh-Worker reopen and startup failure cleanup | Actual browser lifecycle; native SQL comparison; browser cancellation and measured thread/memory budgets |
| G2: persistence and recovery | Not implemented | Data-device, PALF and SQLite VFS integration; flush/rename/lock semantics; normal reopen, worker termination, failed flush, quota exhaustion and competing instances; clear commit durability contract |
| G3: AI functionality and product measurements | Real SQL HNSW, exact-distance, fulltext mutation/rollback and ANN-plus-text candidate join pass on small Node fixtures; earlier VSAG component tests have native comparisons | All emitted index configurations/parser languages; index refresh/rebuild and persistent restart consistency; ranking fusion; native SQL comparison; recall/performance; Chrome/Safari/Firefox results; size/startup/memory/latency/cancel metrics |

Current runtime tests cover four concurrent threads performing 100,000 total
CAS128 increments with cross-translation-unit snapshots; 100,000 list pop/push
reuse operations with exclusive ownership and exact final node counts; pointer
high bits and version rollover; list batch/popall/remove operations; aligned
allocation reuse, zero fill, OOM and page-wash refusal; and platform thread/lock
operations. Additional tests cover stack consumption on main and worker stacks,
bounded backtrace writes, three real futex waiters and one-at-a-time wakeups,
and actual seekdb allocator growth with preserved contents. Three rounds of
three seekdb worker threads each perform 256 allocation/reallocation/free cycles;
main-thread reclamation after worker exit and cooperative stop/join are checked.
A 4,096-object tiny allocator test checks object uniqueness and contents across
block boundaries before reclaiming all objects.
They do not justify marking any complete database gate passed.

Verified on 2026-09-07: the pinned SDK gate passed under its bundled Node
24.19.0, and the shared native reference passed on macOS arm64 with AppleClang
17.0.0. Native syntax checks also verify the unchanged datum and expression-buffer
layout assertions. The SQL front-end test passes separately in `build_wasm_engine`.
This is not a native database regression suite.

Verified on 2026-09-08: all five platform tests pass under the same SDK/Node;
the I/O checks also pass with native macOS POSIX files. They cover 512 concurrent
writes through an eight-slot completion queue, exactly-once completion identity,
backpressure, failed cancellation with retained completion, EOF/short reads,
bad descriptors, timeout validation, and zeroing across multiple buffer sizes.
The Wasm I/O executable also injects interrupted/short writes, a zero-byte write,
and ENOSPC after a partial write; zeroing returns the right failure and preserves
the bytes that were not written. This does not simulate OPFS quota enforcement.
Native syntax checks preserve the storage masks and B-tree layout; logservice,
observer, query, and data-plane Wasm archives compile, and the SQL parser test
passes separately.
Subsequent 2026-09-08 checks passed ordered numeric encoding, SIMD integer
bitpacking/deltas/CRC32C, and the SQLite metadata adapter. Native macOS syntax
checks passed the modified compression expression, virtual codegen lookup, and
XML error callbacks, and PL interface-signature dispatch. The full SQL, storage, rootserver, PL, OBLib common/lib, compression, and
restore archives now compile. All five platform
tests and all nineteen engine-side probes pass in their latest builds (including
RPC memory, CRoaring, the production XML parser, regular-expression context,
spatial filtering, vector-tile encoding, VSAG distance kernels, the VSAG runtime,
HNSW, stream-size guards, VSAG index-support recovery, BLAS/LAPACK, the public
VSAG factory, PALF block allocation, and memory NIO).
These are not an engine startup or recovery test.

The database browser cases now pass in the Codex in-app browser against MEMFS
(see the browser validation entry point above). Other browser engines and
persistent recovery remain unverified.

The development bootstrap target executes the actual `ObServer` against fresh
MEMFS. It remains a separately invoked lifecycle experiment, outside CTest:

```sh
cmake --build build_wasm_engine --target seekdb_wasm_bootstrap_probe --parallel 3
python3 tools/wasm/run-bootstrap.py build_wasm_engine --static-only
python3 tools/wasm/run-bootstrap.py build_wasm_engine --invalid-log-budget
python3 tools/wasm/run-bootstrap.py build_wasm_engine
python3 tools/wasm/run-bootstrap.py build_wasm_engine --client-sql
```

Activate the pinned SDK first (the runner requires `EMSDK_NODE`, or an explicit
`--node` path). Each run writes a raw log and a JSON result into the build
directory. A startup pass requires successful init, start, wait, and destruction
markers as well as exit code zero. The default timeout is 180 seconds. The probe
currently prewarms 64 pthread workers with 1 MiB stacks and allows up to 2 GiB
linear memory; these are diagnostic budgets, not measured minimum requirements.
For failure diagnosis, add `--capture-memfs` to save the complete observer log
instead of only its tail. This uses a temporary JS wrapper with the same WASM
hash, and marks the result as diagnostic because periodic snapshots can affect timing.

The first complete lifecycle pass is artifact
`25d8645bb6668880c999d315e6dc407d29ad012545e3c410f2dc4042283707bd`,
built with Emscripten 4.0.23 and run with the pinned Node 24.19.0 runtime.
`bootstrap-results.json` reports init/start/wait success, completed destruction,
exit code zero and no runtime exception. Both static timezone cases also pass,
including integer literals, timestamp sentinels, SLOG IDs, metadata/schema SQL,
real ring allocation and SSTable copies. This is one fresh MEMFS lifecycle,
not proof of user query APIs, repeat-open safety, browser operation or durability.

Actual startup exposed and drove fixes for microseconds incorrectly passed to
`localtime_r`, fractional timezone formatting, UUID generation depending on
network-interface ioctls, a pointer-sized KV cache slot accessed through a
64-bit atomic pointer, and PALF block paths formatted with LP64 varargs
assumptions. Subsequent startup runs reached MDS table creation and compaction,
exposing object alignment bugs in `ObLightSharedPtr` and SSTable deep copies.
The shared payload now preserves `alignof(T)`, and SSTable array, cache, and
bypass copies preserve the alignment needed by their atomic SCN fields.
The static probe checks the real timezone and server UUID, shared ownership
and final destruction with an atomic payload, and actual SSTable cache/array
copies with one, two, and three tables at offsets zero and four. It checks SCN
values through the production atomic readers and guards the destination bounds.
These cases pass under the pinned SDK's Node runtime. PALF
path tests cover block zero and IDs exceeding 32 bits. A deliberately undersized
log budget verifies startup failure followed by service shutdown and completed
destruction; it must exit with failure and does not count as a startup pass.
Browser lifecycle code stops services before joining the configuration manager,
and avoids the native client-file monitor and process-wide `_Exit` path.

The last full 180-second startup run passed init but did not return from start.
Subsequent entry/return tracing localized the stall to result-type calculation
for the real bootstrap query's `table_name = '__all_global_stat'` condition.
That path uses `FOREACH_CNT`, which stored an int64 loop index in a pointer
variable: on wasm32 its eight-byte accesses overrun a four-byte stack object.
A standalone AddressSanitizer run reproduces the stack-buffer-overflow with
the old macro. The macro now uses a separate int64 index. The `wasm_foreach`
CTest passes with AddressSanitizer, covering pointer identity and mutation,
const inputs, break/continue, conditional exit, empty arrays, changing counts,
nested loops, and unbraced if/else use. The same test passes natively with
AddressSanitizer and UBSan. Temporary engine tracing has been removed.
The rebuilt engine passes that former stall: start now returns an explicit
bootstrap error instead of timing out in expression deduction. The first run
exposed empty column names in the global-stat INSERT, caused by `%ld` consuming
only four bytes of an int64 argument and misreading the following string
arguments. Core-table and global-stat SQL now use PRId64/PRIu64. A subsequent
real startup passed global-stat initialization and reached inner-table schema
loading, where the schema-row constructor had the same format mismatch. Its
three row formats now use PRIu64, matching the unsigned row counter. The next
real startup passed schema loading and reached optimizer preference creation.

Failed-start cleanup also exposed a ring-buffer segment tail overwrite:
`ObPtrSpinLock` used uint64 storage while being overlaid on a four-byte PtrSlot.
It now uses uintptr_t, with a compile-time slot size/alignment check. The old
implementation fails an adjacent-canary test; the new `wasm_ptr_spin_lock` CTest
passes adjacent-storage, pointer-bit and concurrent-update checks. The native
version passes with AddressSanitizer and UBSan. The bootstrap static probe now
also exercises actual ring segment growth/free; both timezone runs pass on the
rebuilt engine. Failed startup at optimizer preference creation now completes
destruction without the former allocator assertion. This establishes cleanup
for that observed failure, not every possible startup failure.
One diagnostic run also reported a null-function/signature-mismatch trap in
the logging thread; a repeat run did not reproduce it, and it remains unresolved.
Result JSON includes the artifact SHA-256 and symbolized exception frames when
available. The last completed full startup (artifact SHA-256
`a7ae1ceec88208ead6dfcb12d9166574cb44aacfbac1cf8441cda26f4b5739ef`)
returns OB_ERR_PARSE_SQL while initializing `STATS_RETENTION`: another `%ld`
mismatch corrupts the following timestamp/NULL arguments. The optimizer
preference SQL formats now use the integer-width macros. Their static producer
checks pass in both timezone runs.

Additional static checks call the actual metadata-update, schema-row, and runtime
statistics SQL producers with values above 32 bits. Metadata and schema cases
pass. The runtime-stat case exposed a further truncation in `ObObj` integer
literal formatting, before the runtime DDL SQL builder even receives the value.
Integer, unsigned integer, enum/set, bit and placeholder formatting are now
corrected. New checks cover signed boundaries, UINT64_MAX, quoted/plain/SQL
representations, bit hex output, and optimizer preference initialization versus
reset. All of these checks pass on artifact
`9fd8c099288c220bd954fd813aa1094954adeee9828de936967603e26b3b4d56`.
SQL capture tests inspect real producer output but do not execute SQL or
substitute for a successful bootstrap.

The following startup run traps in background freeze-task logging:
`KTIME(INT64_MAX)` reaches `localtime_r` with a value outside JavaScript Date's
range. The timestamp formatter now emits the original integer for negative or
out-of-calendar-range values, bounds its output, and pads microseconds to six
digits for fixed-position range extraction. It uses the standard calendar
conversion for dates beyond the native fast helper's 32-bit intermediate range.
The range formatter preserves raw sentinel output. Sentinel, cached timestamp,
microsecond and post-2038 checks pass in both timezone runs on artifact
`aef18f44249e179025413d3a3da07b055068d06660c155bf60b1548cbfa65d05`.
Full startup then times out at 90 seconds without that trap. A run of the same
WASM with periodic MEMFS log snapshots shows the runtime initialization
transaction reaches commit, followed by schema refresh retrying malformed SQL.
`check_sys_schema_change` still used LP64 formats for its version bounds and
table-ID list; these and the incremental DDL-operation version range now use
the correct signed/unsigned width macros. A probe after `ObServer::init` calls
the real initialized schema service with a capturing SQL client to check both
queries and the unchanged-version shortcut. This check passes on artifact
`8946fd983f0dddfe1b279c8902ca26eb2c9d1dfae6183e7a46da11da6d13aa24`.
Startup advances past that query but continues retrying: a truncated version
bound in the system-variable maximum-version query produces a NULL result.

A compiler-guided audit of `ob_schema_service_sql_impl.cpp` then identified and
corrected 92 direct integer format sites and the associated SQL macros. Each
change follows the actual argument type; shared index-query formats normalize
their two unsigned-long constant arguments to uint64_t, while unrelated genuine
unsigned-long arguments retain their formats. A fresh syntax compilation reports
no format-type mismatches attributed to this source in the current WASM build
configuration. This does not cover every conditional configuration or the rest
of the engine. Post-init tests now also inspect system-variable version, user
schema and core/non-core table-version queries. These checks and invalid-budget
cleanup pass on artifact
`a702353b4e821abf33449e16d312e6dcb0c331bc1c86a651fb24337a7777b09f`.
Actual startup passes schema refresh and reaches the bootstrap local checkpoint,
then returns OB_INVALID_ARGUMENT while deleting an invalid SLOG file ID. Cleanup
completes without a runtime exception.

The SLOG file-path helper still used `%ld` for an int64 ID. It now uses PRId64.
Directory scanning also now parses before narrowing: valid IDs above INT32_MAX
must not saturate through wasm32 `strtol`, and out-of-range numeric names must
not wrap into another log ID. Invalid numeric names return an error without
changing the collected range; nonnumeric directory entries remain ignored.
Path creation, exact/truncated buffers, the valid ID upper bound, directory
range selection and overflow rejection pass in both static timezone runs on
artifact `25d8645bb6668880c999d315e6dc407d29ad012545e3c410f2dc4042283707bd`.
Full startup and shutdown pass on this artifact. The observed vector history
cleanup query's int64 timestamp and batch-limit formats are also fixed.

The production C++ SQL callbacks are now reached through
`ObSqlNioServer::connect_memory`, which protects reactor admission against
concurrent destruction. The Node SQL/lifecycle regression now passes. Next work
must provide the asynchronous Worker API, verify the browser lifecycle and
measure the service/thread configuration.
The runtime's wasm32 ABI still needs a broader
audit. The current allocator compilation also exposes LP64 printf
assumptions (`%ld`/`%lu` for int64_t/uint64_t) that require a systematic wasm32
audit before SQL results and diagnostics can be trusted. Do not suppress those
warnings to claim ABI compatibility.

The browser-side protocol component is `src/wasm/mysql-wire.mjs`. It frames and
decodes uncompressed MySQL 4.1 packets, checks sequence numbers and packet limits,
handles continuation frames, and emits column/row/completion events for text
results. It preserves column names and cell values as bytes (SQL NULL separately),
and represents OK counters as BigInt. Metadata has a cumulative size budget.
`mysql-auth.mjs` constructs the corresponding login payload and native-password
challenge response using Web Crypto. Greeting/auth-switch decoding preserves all
20 challenge bytes, including embedded zeros. Login capability negotiation leaves
compression, local-file requests, session tracking, TLS, and deprecated EOF disabled.
`mysql-transport.mjs` owns the copying, nonblocking WASM memory connection;
`mysql-client.mjs` authenticates and streams query results through the production
reactor and SQL workers. Closing is idempotent; aborting or abandoning a result
closes its connection. Browser cancellation behavior and the user-facing Worker
lifecycle remain unverified.

The `--client-sql` runner uses `unittest/wasm/test_engine_sql.mjs` against the
actual bootstrap artifact, with a 257-byte connection capacity and 31-byte JS
copies to force fragmentation and backpressure. On artifact
`5bf23b1499f873139476d3f6dc4410f01a5ed3810dfa7dd829ac2485190f1f0e`,
authentication, SELECT, Chinese/NULL/uint64 results, create database/table,
insert/update/delete, rollback/commit and subsequent result assertions passed.
Invalid SQL also preserved a usable connection. The complete case failed:
DROP DATABASE generated a malformed column-history INSERT, and shutdown timed
out. Linear memory remained 512 MiB during the SQL sequence; this is one Node
MEMFS observation, not a minimum budget, browser benchmark or durability result.

This path exposed additional wasm32 varargs defects in max-ID updates,
optimizer-statistics queries and DDL history rows. Their integer formats now
match the actual signed/unsigned 64-bit arguments. The optimizer-statistics
source also passes a standalone syntax check without format-type warnings in
that source. The probe's memstore budget is now 192 MiB: the original 64 MiB was
below the production freezer's 100 MiB replay reserve and rejected user writes.
WASM shutdown now waits for runtime/module workers and the log writer, and
`ob_pthread_tryjoin_np` propagates a pending join instead of reporting success.
On the subsequent artifact
`2dcad3f1e99b50cd10adf30d54cb2ab0daf79c16d2cf174a4c4c06e7a71be16f`,
both static timezone cases pass, including a real pending-thread join check;
CRUD and transaction assertions pass again. DROP DATABASE now returns an
internal error (4016), and shutdown still times out. The complete SQL/lifecycle
case remains failing; earlier lifecycle-only results do not establish this newer state.
The full-log capture traces 4016 to `ObDatabaseSqlService::delete_database`:
its DELETE and history INSERT also used LP64 formats for 64-bit arguments.
Those formats are corrected, and DROP DATABASE passes in subsequent runs.

The first complete real client SQL lifecycle passes on artifact
`6fe6c2eee9edbd0aeab95b3390afbc99429cb2835459f44580ec3c6e359af78b`
(Emscripten 4.0.23, Node 24.19.0). The normal runner, without diagnostic JS
instrumentation, reports every SQL assertion, wait and destruction complete,
exit zero and no runtime exception. The expanded test also verifies rejected
password authentication, rollback and write-lock release after disconnect,
closing an abandoned result iterator, and AbortSignal cancellation closing its
connection while a peer remains usable. Cancellation uses a SLEEP query; this
checks the client's close/error contract, not a browser cancellation-latency SLA.
The invalid-log-budget case on the same artifact reports the expected startup
failure and completed cleanup. Linear memory stayed at its configured initial
512 MiB during the query sequences; minimum memory and native comparison remain
unmeasured.

Two shutdown ordering defects were exposed by actually joining the workers.
First, the global timer service was stopped before storage metadata GC could
finish, leaving the runtime wait stuck. WASM now joins runtime/module workers
before stopping process-wide IO, cache and timer services. Second,
`ObLSService::wait` freed the LS while deferred MDS table destructors still used
its MDS manager, producing a linked-list assertion. It now keeps the LS alive
until the metadata manager confirms the deferred objects are released. Temporary
stderr instrumentation used to locate both faults has been removed. This is a
fresh Node MEMFS database lifecycle, not proof of browser operation, repeated
database reopening, OPFS durability, or SQL vector/fulltext/hybrid completeness.

Run its tests with the pinned Rust toolchain available through rustup:

```sh
bash tools/wasm/test-mysql-wire.sh "$EMSDK_NODE"
```

The test script compiles the production Rust packet/response encoders into a
small fixture generator. Fourteen Node tests consume these encoded packets and cover
fragmented transport reads, sequence wrap, the 0xffffff continuation boundary,
truncated/oversized input, exact uint64 values, binary cells/NULL, SQLSTATE,
multiple results, and cumulative metadata limits. Login payloads are checked by
the production Rust login parser, and password responses are checked against
the server-side SHA-1 recovery algorithm. These are protocol compatibility tests,
not evidence of successful SQL execution or browser integration.

The header inventory generator previously treated SDK `emscripten/threading.h`
as zstd's same-basename header; SDK includes are now excluded from repository
resolution. The checked-in header inventory already differs from the generator
at HEAD (verified using the original tracked sources). This change adds the new
Wasm headers but does not regenerate unrelated baseline ownership differences;
the full inventory check therefore remains failing. Emscripten MEMFS filesystem
capacity reports are not OPFS quota or durability evidence.

No-op fsync, fake SQL results, a SQLite database presented as seekdb, or an HTTP
proxy to a native server do not satisfy this implementation's target.
