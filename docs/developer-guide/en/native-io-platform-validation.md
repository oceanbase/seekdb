# Native I/O platform validation

Validation started on 2026-11-14 from commit `8ad181756`, with the platform fixes
listed below. This record covers native Windows x64 and macOS arm64. It does not
claim validation of Wasm, Android or iOS.

## Fixes

- Windows path conversion now uses `MAX_PATH` explicitly. The removed `dirent.h`
  had defined `PATH_MAX` as that value; the existing length boundary is preserved.
- Unix `openat` promotes the mode argument before passing it through C varargs.
  Darwin's `mode_t` is narrower than the required argument type.
- Apple directory validation uses the open descriptor's `F_GETPATH` path and
  compares file identity. APFS retains a nonzero link count and allows `.` lookup
  after directory removal, so neither check proves the directory is still linked.
  Operations remain descriptor-relative. Missing or replaced paths fail; a racing
  rename, inaccessible ancestor or overlong path can conservatively fail.
- Existing filename tests use valid non-ASCII UTF-8 on Apple filesystems, which
  reject the invalid UTF-8 used by the other Unix test cases. Tests still execute;
  byte-name coverage remains on other Unix platforms.
- `storage-io` declares its test dependency on libc on all platforms that use it.
- Windows tests explicitly cover rejection of renaming a directory containing an
  open child, release and retry, and preserved identity. A separate existing-file
  test releases the exclusive owner before reading through an independent handle.
- The named-file timeout test controls physical completion retirement explicitly,
  rather than assuming directory operations finish within a 30 ms worker delay.

The Apple identity change received independent source review. No C++ unit tests,
new test files under `unittest/`, or production shutdown mechanisms were added.

## Windows

The user's main checkout contained unrelated edits and was left intact. Source,
output, dependency override and SQL instances are isolated under task-owned
`C:\io-rust-1114`, `C:\iob1114` and `C:\iot1114` paths. The installed SDK and
Rust 1.98.1 toolchain were reused. Missing Cargo cache entries were supplied from
the Linux build's locked vendor archive, without downloading external packages.

The full optimized product build passed, followed by an incremental build after
synchronizing all platform patches. Native Rust tests passed: 79 file-io and
146 storage-io tests; one existing external block-file fixture test remains
ignored. Standalone Cargo tests with the `cxx` feature could not link the C++
CallbackAdapter implementation on Windows. No stub was added to hide that gap;
the product build and SQL checks exercise the actual linked CXX integration.

The built executable's SHA-256 is
`a2cad15ccc407cc01dfdf21555760eac5ea9981bfc1ffb4671184430d83e090f`.

The isolated instance passed transaction commit and rollback, creation and
readback of 65,536 rows with 64 MiB of payload, sorting and hash aggregation with
actual spill, and restart readback. The workarea view reported ONE PASS and
71,277,056 bytes of temporary storage for sort; hash aggregation also spilled.
Every sorted row and aggregate was compared with independently generated values.
The sorted output hash matches the Linux verification:
`848391089b1ebec0f225293d7929e29cd4b50faf5167f347b81d37a490497f6d`.

Windows used the existing `--embedded` client-file-lock lifecycle: releasing the
client lock lets the server exit with code zero. This verifies supported process
exit and restart, not execution of every service's shutdown/drain method. The
ordinary Windows daemon has no equivalent implemented POSIX signal path; service
stop uses process termination. Rust tests separately cover I/O shutdown and
resource lifetime.

A separate 2,048-row table produced an observed MINI_MERGE for its exact tablet.
After restart, its row count, ID sum and payload size matched. A committed update
survived intentional process termination and another restart; the original
65,536-row table remained intact. The final embedded exit returned zero. This is
a process-crash check, not a simulated power failure.

A larger sorting query was cancelled after a temporary file was observed. It
returned error 1317, temporary files returned to zero, and the original table
retained all 65,536 rows. The final embedded exit returned zero and the test port
was closed. Existing user instances were not restarted.

## macOS

The remembered Apple Silicon host and cached SDK/toolchain are used in an isolated
workspace. The shared host's existing services and source trees remain untouched.
The final optimized product build passed after all twelve patch files were
synchronized and their hashes checked. Rust tests with CXX enabled passed: 103
file-io and 163 storage-io tests, with one existing external fixture test ignored.

The executable SHA-256 is
`dab48a6172956047390432f754b5a0d39c6311e0b3359a49a595a9894212e8fc`.

The real instance passed transaction commit/rollback, the same 64 MiB sort and
hash-aggregation spill checks, cancellation with error 1317 and zero remaining
temporary files, and a MINI_MERGE for the exact 2,048-row test tablet. The sorted
output hash matches Windows and Linux. Readback after SIGUSR1 exit and restart
matched the flushed rows. A committed update survived intentional SIGKILL and
another restart. Final SIGUSR1 exit returned zero. The final audit found no owned database
processes and confirmed the test port was closed; the executable was preserved
under the task artifact directory.

As on Windows, this verifies the supported native process-exit path, not a call
to every service destructor: native `ObServer::wait()` uses `_Exit(0)`. Rust
lifecycle tests provide the separate executor-drain evidence.

The first build took place on a busy shared host. An earlier runner-script edit
truncated its log; that log is not treated as final build evidence. The stable
final build retained its full successful log, and the actual final executable
was used for all runtime checks.

## Linux regression and evidence

After the platform changes, the focused CXX-enabled Linux suite passed: 136
file-io and 171 storage-io tests, with one existing external fixture ignored.

Local artifacts are under `.audit/io-platform/`, `.audit/io-platform-windows/`
and `.audit/io-platform-macos/`. They retain failing and passing build/test logs,
platform probes, SQL checks and source/artifact provenance. Build durations from
these shared hosts are not performance measurements.
