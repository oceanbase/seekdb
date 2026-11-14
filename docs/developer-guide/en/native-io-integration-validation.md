# Three database I/O integration points

The migration removes the production `src/share/io/` directory. Database startup and configuration own the native service handle, the local file adapter supplies native request targets and database errors, and business callers consume native results through the common result adapter. CXX generates the language boundary. Micro-block decoding remains with its existing business callbacks.

The common C++ result adapter is 425 lines across `src/share/io_request.h` and `.cpp`. PALF, query, and storage share it without depending on the storage implementation. It contains no C++ manager, service registry, scheduler, or configuration snapshot.

## Verification

- The full database shared library and executable built successfully with offline Cargo dependencies.
- 164 Rust unit tests and one integration test passed. The existing C++ block-file fixture compatibility test remains ignored without `BLOCK_FILE_FIXTURE`.
- Public native-read, native-write, worker-read, and worker-write paths each completed 128 requests with zero submission, completion, short-I/O, or data errors. Each used a fresh 16 MiB file with whole-file validation. Syscall tracing confirmed native `io_submit` use.
- A SQL instance wrote 32,768 rows containing 64 MiB of payload. Compaction history confirmed the flush. Aggregate content fingerprints matched after restart, and updated content matched after a second restart. All three processes exited normally. A second minor-freeze command was needed before the matching flush record appeared.
- No C++ unit tests ran. The public I/O driver ran only in validation mode. These checks establish correctness for the exercised paths, not a new performance comparison.

Independent shutdown reviews found and drove regressions for retained backend ownership, reaped backend ownership, and result publication preceding quiescence. Final stderr inspection also found that closing an endpoint after service shutdown reported `BrokenPipe` despite successful draining. The final regressions verify repeated close after both stop and shutdown, and concurrent close and stop. The concurrent test does not force every internal interleaving; both stopped rejection points use the same error normalization.

The local evidence directory is `.audit/io-three-boundaries/`. It contains build logs, Rust test logs, public-path JSON and syscall traces, and SQL statements and recovery fingerprints. The stopped SQL instance is retained for inspection.

## Final artifact check

After the close fix, the database rebuilt successfully. All four public I/O paths passed again with no endpoint-shutdown error in their logs. The rebuilt database recovered the prior updated SQL aggregate fingerprint and exited normally. This final run is recorded in `final-validation/report.json`; the earlier full SQL sequence is in `sql/report.json`.

| Artifact | SHA-256 |
| --- | --- |
| Source diff against `87cd8c90b`, restricted to `src`, `rust`, `bazel`, and `unittest/share/io` | `0c1f0127134f603c8d1b4e19e425730047715c61ebb9c033587fc48a267f80b0` |
| Database executable | `53298ebcf3bbd2b59e6b09cf43824d596ac8fe7417e790eb27a324ff03589ebd` |
| Database shared library | `f4211124892c44bc34fac03ec0bbc13c1c56a0e31987fa8e602af417913d4cf6` |

The test workspace uses local copies of `storage-io` and `file-io` to avoid resolving unrelated workspace dependencies. Their source files were compared with the production tree. One pre-existing external fixture test remains unverified; no new performance result is claimed.
