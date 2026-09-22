# seekdb WebAssembly shell

A browser SQL shell backed by the seekdb engine compiled to WebAssembly. The
terminal layout, command history, and example entry point take inspiration from
the [Lite4MariaDB shell](https://lite4mariadb.shyim.de/repl?run=example). Its
[JavaScript SDK and Worker API](https://github.com/shyim/lite4mariadb/tree/main/wasm/src)
also informed the loading, result, error, and close behavior. This shell uses
seekdb's existing JavaScript API and WebAssembly engine.

## Run an existing build

From this checkout, pass an engine build made from this branch (see
[Build from source](#build-from-source)):

```sh
python3 tools/wasm/serve-shell.py \
  --build-dir build_wasm_engine \
  --port 8767
```

Open [the shell](http://127.0.0.1:8767/shell.html).
The first load downloads the engine and initializes a database in the browser.
The version badge shows the build version before the engine starts.
Examples run only when selected from the **Examples** menu.
Keep the server running; press Ctrl+C in its terminal to stop it.

The launcher copies `seekdb_wasm_database.mjs`, `seekdb_wasm_database.wasm`, and
`engine-version.mjs` into this checkout's ignored `build_wasm_shell/`
directory. It serves the shell and JavaScript API from this checkout, so reloading
the page picks up source edits. The server binds to `127.0.0.1` and serves an
explicit list of assets. It does not expose the repository or execute SQL.
Reusing another build is appropriate when its engine source and toolchain match;
rebuild after changing C++, Rust, or the runtime ABI. Builds from
`feature/webassembly` predate the storage argument and the WasmFS link, so they
do not start with this shell.

## Use the shell

The database starts automatically. Once it is ready, choose **Hybrid Search** or
**Fork Table** from **Examples** to run its SQL immediately, or enter SQL in
the prompt. Hybrid Search combines full-text matching, a category filter, and
vector distance. Fork Table creates a copy and shows that changes to either table
do not affect the other. Each example recreates its own demo tables in `playground`
so it can be run repeatedly.

| Input | Action |
| --- | --- |
| Enter | Accept the current line; execute complete statements and keep unfinished SQL in the input buffer |
| Ctrl / Command + Enter, Shift + Enter | Same as Enter; do not force execution of unfinished SQL |
| Tab / Shift + Tab | Return focus to SQL input without navigating between controls |
| `;` or `\g` | End and execute the current statement |
| `\G` | End and execute the current statement with vertical output |
| `\c` | Discard the input buffer without clearing earlier output |
| `\p` | Print the input buffer without executing or discarding it |
| `DELIMITER $$` or `\d $$` | Change the statement delimiter; use `DELIMITER ;` to restore it |
| `USE database_name` | Change the database without a delimiter when starting with an empty input buffer |
| Up / Down | Browse history at the first or last input line |
| Ctrl + C | Clear unfinished input, or interrupt the active query, when no text is selected |
| Ctrl + L | Clear earlier output while keeping the pending or active statement, tables, and history |
| `\clear` | Clear output |
| `\tables` | Show tables |
| `\databases` | Show databases |

Clicking unused terminal space also returns focus to SQL input. Selecting text
and focusing result tables for scrolling do not move focus to the input.

Enter echoes each accepted line in the transcript and clears the editable line.
Unfinished SQL stays in the input buffer. The continuation prompt is `->` for
ordinary SQL, `'>` or `">` for an open quoted string, `` `> `` for an open
backtick identifier, and `/*>` for an open block comment. A blank line does not
execute the buffer. Delimiters and short commands inside strings or comments
do not end a statement; close the quote or comment first, or use Ctrl+C to
discard the input.

Set a custom delimiter on its own line with an empty input buffer. It must be
nonempty and contain no whitespace or backslashes. The delimiter is removed
before SQL reaches the engine. New Instance restores the default `;` delimiter.

Interactive statements run in order and continue after SQL errors. Examples
stop at the first error. Results display up to 500 rows and 100 columns as ASCII
tables, or vertically when ended with `\G`. Values and column names longer than
120 characters are shortened for display. Each formatted table is limited to
100,000 characters, including column padding and borders. The transcript keeps
up to 40 entries and 1,000,000 characters, removing older output as needed.
The Worker sends a bounded preview and consumes the rest of the result so the
reported row count and session state remain correct. These display limits do
not add a SQL `LIMIT` or stop query execution. Use `LIMIT` for large
queries. Statement splitting supports default MySQL quoting and
`NO_BACKSLASH_ESCAPES`. Use backticks for quoted identifiers; the shell does not
track `ANSI_QUOTES` mode. Custom delimiters change input parsing, not the SQL
features supported by the engine.

The shell implements the client commands listed above, not the full `mysql`
client or its line editor. Commands for local files, external editors, and
pagers are not supported. Short commands inside executable comments such as
`/*! ... */` and optimizer hints such as `/*+ ... */` are not interpreted.

Ctrl+C sends `KILL QUERY` through a separate connection and stops the remaining
submitted statements. The current session and its settings remain available.
Connection failures still trigger a reconnect, which rolls back uncommitted
changes and resets session settings. Closing the database stops the engine;
whether its data survives depends on the storage mode below.

### Storage

The shell opens the `test` database by default, creating it if it is missing.

The shell remembers the last successfully opened storage mode for this origin.
Reloading or reopening the page restores that mode. OPFS reopens its existing
database; Memory starts empty. If no mode has been saved, the shell checks for
an existing OPFS database and reopens it, or starts in Memory when none exists.
OPFS can be selected when the browser supports both OPFS and Web Locks.
Choose **Memory** or **OPFS** from **New Instance** to clear terminal output,
terminate the current engine's Worker, discard its data, and start an empty
database in that mode. This action does not wait for native graceful shutdown.
SQL input stays disabled until the new instance is ready.
Selecting the current mode also creates a new instance. Creating an OPFS instance
clears any previously stored database; leaving a running OPFS instance for Memory
also clears its stored data. If clearing fails, startup stops and shows the error.

| Item | Effect |
| --- | --- |
| Memory | The data directory lives in Wasm memory; it is gone when the database is closed or the page is reloaded |
| OPFS | The data directory lives in the origin private file system (OPFS); committed data survives reloads and browser restarts until New Instance clears it |

The badge next to the database name shows `memory://` or `opfs://`.
Reloading never clears OPFS files. **New Instance** explicitly starts empty.
Storage preferences and OPFS files belong to the page's origin, including its
protocol, hostname, and port. Another origin has separate data.
One tab at a time may open the stored database: a second tab is told so and can
switch to memory, and a page outside this shell that still holds the files is
reported as locking them, to be closed before reloading the page. Browsers
without OPFS or Web Locks can use Memory only.

## Build from source

Use Emscripten 4.0.23 and the pinned Rust nightly. Host parser generation also
needs bison 2.4.1, flex, and the native dependency headers described in
[engine dependency setup](webassembly.md#configure-engine-dependencies).
After activating Emscripten and installing those dependencies:

```sh
rustup toolchain install nightly-2026-09-07 --profile minimal \
  --component rust-src --target wasm32-unknown-emscripten
bash tools/wasm/build-deps.sh
bash tools/wasm/build-rust-nio.sh
bash tools/wasm/build-engine.sh build_wasm_engine
cmake --build build_wasm_engine --target seekdb_wasm_database --parallel 3
python3 tools/wasm/serve-shell.py --build-dir build_wasm_engine
```

When the host tools or headers are outside this checkout's `deps/3rd`, pass
`-DSEEKDB_HOST_DEVTOOLS=/path/to/devtools` and
`-DSEEKDB_HEADER_DEPS=/path/to/devel/include` to `build-engine.sh`.

CMake copies the shell, API, and Worker assets beside the generated engine module.
That build directory can also be served with the existing server:

```sh
python3 tools/wasm/serve.py build_wasm_engine --port 8767
```

Open `/shell.html` when using this server. A static host must send
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` to enable `SharedArrayBuffer` for
Emscripten pthreads. Use HTTPS or loopback HTTP, and serve `.wasm` as
`application/wasm` and `.mjs` as JavaScript. Opening the HTML as a file will not
work.

## Package a release

Refresh the complete build directory before packaging. The packager reads the
generated engine and all shell/API assets from that directory, without rebuilding
the engine or reading live source files:

```sh
cmake --build build_wasm_engine --target seekdb_wasm_database --parallel 3
python3 tools/wasm/release-shell.py package build_wasm_release \
  --build-dir build_wasm_engine
python3 tools/wasm/release-shell.py serve build_wasm_release --port 0
```

Open the printed loopback URL. This preview serves the packaged snapshot and uses
a free port by default. The development servers above retain `no-store` so source
edits remain visible on reload.

The output directory must be new. Every release includes `index.html`,
`shell.html`, `manifest.json`, and `releases/<build-hash>/`. The hash covers every
input asset, including the HTML and JavaScript, so an engine or UI change produces
a new directory. The root HTML points its relative assets at that directory.
The manifest records SHA256 and byte length for each served representation and
the engine hash; it identifies the packaged files, not a verified source commit.
The preview validates these hashes before listening and serves only listed assets.

All assets have gzip files precompressed at level 6. Add `--brotli` to also
generate Brotli quality 5 files using an existing `brotli` executable on `PATH`.
No additional dependency is required for gzip. The preview negotiates the stored
representations using `Accept-Encoding`, and supports `HEAD` and conditional
requests with an encoding-specific `ETag`.

For an HTTPS static host or CDN, upload the package and configure these headers:

| Response | Required configuration |
| --- | --- |
| All shell assets | `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp`, `Vary: Accept-Encoding` |
| Root `index.html` and `shell.html` | `Cache-Control: no-cache` |
| `releases/<build-hash>/*` | `Cache-Control: public, max-age=31536000, immutable` |
| Negotiated `.gz` or `.br` representation | `Content-Encoding: gzip` or `br`, keeping the original MIME type |
| Wasm / JavaScript / CSS / HTML | `application/wasm` / `text/javascript` / `text/css` / `text/html; charset=utf-8` |

Serve compressed representations at the original asset URL; do not point imports
at `.gz` or `.br` URLs. Copy a new version directory before replacing root HTML,
and retain old version directories while existing pages may still reference them.
Keep published version directories unchanged. Merely uploading the compressed
files to a static host does not enable compression negotiation or these headers.

Validate packaging and HTTP behavior without starting an engine:

```sh
python3 unittest/wasm/test_release_shell.py
```

## Run tests

Use the Node runtime from the activated Emscripten SDK:

```sh
node --test unittest/wasm/*.test.mjs
python3 unittest/wasm/test_release_shell.py
node unittest/wasm/test_shell_examples.mjs build_wasm_engine/seekdb_wasm_database.mjs
```

The first command checks SQL statement splitting, command history, output limits,
Worker batching, preview, cancellation, and module loading. The second checks
release packaging, compression, and HTTP headers. The third runs the examples
and session checks against the real Wasm engine. Pass another
build's `seekdb_wasm_database.mjs` path as the final argument when reusing a build.
Node has no OPFS, so these run in memory mode; the persistent mode is covered by
the browser cases in `database-browser.html`, which add a stored reopen, the
one-instance rule and clearing when the browser offers OPFS. Those cases delete
the database stored in OPFS for their origin, so do not run them on the origin
of a shell whose stored data you want to keep.

## How SQL runs

```text
shell.html + shell.mjs
  → Database / Session API (database.mjs)
  → postMessage to database-worker.mjs
  → MySQL packets over the in-memory transport
  → seekdb C++ / Rust engine in WebAssembly
  → result events back to the shell
```

The main browser thread handles input and renders results. A dedicated Web Worker
owns the database runtime. Emscripten pthreads run engine work, while the existing
JavaScript API streams column, row, and completion events back to the shell.
The shell uses the real engine; startup failures are displayed as errors.

During Wasm startup, internal SQL on the main thread and the bootstrap partition
thread uses a five-minute deadline for each startup scope instead of the default
30-second internal SQL timeout. An existing shorter deadline is preserved.
These contexts are released before the shell accepts SQL; normal session and
internal SQL timeout settings are unchanged. This accommodates slower browser
scheduling, but is not a five-minute limit on the entire startup or a guarantee
against failures after a long browser suspension.

Worker responses batch up to 64 events and 256 KiB of field bytes, with a 4 ms
flush deadline after the first available event. An indivisible event can exceed
the byte limit, subject to the existing protocol packet limits. The public
`session.query()` iterator still yields individual events with exact byte values
and BigInt counters. Its default returns the complete result. The optional
`preview` argument bounds rows, columns, cells, and bytes sent to the caller;
completion events then include `preview.rowCount` and display-limit metadata.
The shell uses this option while the Worker drains undisplayed rows.

The shell supplies both `moduleURL` and `wasmURL` to `Database.open()`. This keeps
one compiled `WebAssembly.Module` in the page, keyed by both URLs, for later
instances. New Instance still creates fresh memory, Workers, and an engine after
discarding the previous one and clearing its data. Reloading the page clears this
in-page reference; release HTTP caching applies independently. API callers that
omit `wasmURL` retain the generated loader's default behavior.

The engine runs on Emscripten WasmFS. `Database.open({storage: 'memory'})`, the
JavaScript API default, keeps the data directory in the WasmFS memory backend, so it is gone
when the database is closed or the page is reloaded. `Database.open({storage:
'opfs'})` mounts the origin private file system at the data directory instead:
the files live at the OPFS root of the page's origin, survive reloads, and are
opened by one instance per origin, guarded by a Web Lock. Committed data was
recovered after a clean close and after a reload without one in headless
Chrome; a durable commit contract on OPFS is not established yet.
`Database.clearPersistentStorage()` removes the stored files. No database
server, TCP connection, or SQL API service is required. The adapter that lets
the engine run on OPFS, and what it does not cover, is described in
[webassembly.md](webassembly.md#storage-modes-and-the-wasmfs-adapter).

`Database.close()` waits for native shutdown and keeps OPFS data.
`Database.discard()` rejects pending requests and asks the browser to terminate
the runtime Worker and its nested pthread Workers. For OPFS, it holds the Web Lock while a separate
cleanup Worker waits for file handles to be released and removes the stored
files. New Instance uses this destructive operation because its data is being
discarded. Cleanup errors prevent a replacement instance from starting and can
be retried through New Instance. A successful discard is not a native graceful
shutdown or an `onExit(0)` acknowledgement, and the browser does not provide a
Worker termination completion event. The cleanup Worker's 15-second limit
applies to retries for locked files, not to browser suspension or the total
duration of storage operations.

The Wasm build splits positive and `INFINITY` Emscripten futex waits into waits
of at most one second, calling an empty JavaScript import after each timeout. Safari 27.0
(22625.1.29.11.27) can otherwise leave a terminated Worker blocked in Wasm
`atomic.wait`, including loops that retry finite waits. The JavaScript call
allows Safari to process the termination request. Finite waits retain their
original total timeout; normal wake and value-mismatch results are returned
unchanged. This covers the SDK futex path, not the linker's shared-memory
initialization wait, and is not an event-loop yield. File-handle probes accept
both `NoModificationAllowedError` and Safari's `InvalidStateError` while waiting
for exclusive OPFS access, with bounded retries that preserve the final error.

The current engine uses a 512 MiB initial Wasm memory, can grow to 2 GiB, and
prewarms 64 pthread workers. These build settings make the shell most suitable
for a desktop browser. The engine binary is large, and startup can take several
seconds on the first load.
