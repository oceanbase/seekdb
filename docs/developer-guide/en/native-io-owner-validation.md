# Typed I/O owner validation

This record covers removal of the generic device framework after baseline `0a0314c83`.
The earlier [integration record](native-io-integration-validation.md) predates this change.
The current ownership model is described in [Native I/O ownership](native-io.md).

## Implementation

`ObBlockManager` submits data operations through `BlockOwner`.
PALF opens `NamedFile` objects from the inventory owned by `ObServerLogBlockMgr`.
Both use the server's Rust `ServiceHandle`.
`share/io_request` contains common error, result, and callback adaptation.

The production tree no longer contains `ObDeviceManager`, `ObIODevice`, `ObLocalDevice`,
`ObIOFd`, `ObIODOpts`, `FileRegistry`, or `NamedBindings`.
The historical benchmark under `unittest/share/io` retains its frozen-version interface.
Its build command rejects a current tree without that interface.

The change also removes unused CXX configuration and statistics functions,
unmeasured latency stages, the empty calibration and benchmark virtual tables,
configuration generations, and the idle aligned-buffer cache.
Active request records and buffer bytes remain bounded.
Closed execution endpoints retain distinct identities until their accepted work retires.
Their routing and admission entries are then removed.

## Rust verification

The two crates were copied into an isolated workspace with a separate Cargo target directory.
All 66 source and manifest files were compared byte for byte with the production working tree.
Dependencies came from the existing offline cache.

```sh
CARGO_NET_OFFLINE=true cargo test \
  --manifest-path .audit/io-convergence/final-rust/Cargo.toml \
  --workspace --features storage-io/cxx,storage-io/ownership-test-support \
  -- --test-threads=1
```

The run passed 133 file-io tests, 171 storage-io tests, and one integration test.
The existing real-file fixture test was ignored by that run, then explicitly executed
against the stopped database's block file. It passed and verified unchanged header bytes
and file length.

The last-request-drop test previously assumed that a 30 ms delay would outlast its assertion.
It now retains the backend completion, drops the real request, and processes cancellation.
It checks that the record and buffer charges survive until explicit retirement delivery.
The test uses existing engine controls and adds no production hooks.

Actual database startup exposed a callback-window regression. The aligned physical buffer
had been delivered to callbacks that parse the logical request range. The fix restores
that logical range while retaining full physical-read validation. The strengthened test
uses distinct prefix, requested, and suffix bytes. It fails against the incorrect implementation
and passes against the fix.

Writable PALF opens preserve `O_SYNC` on Unix before direct I/O is enabled.
Tests inspect descriptor flags on buffered and direct descriptors and through the typed PALF owner.
Windows uses `FILE_FLAG_WRITE_THROUGH`; that branch was inspected but not executed on this host.

Local evidence is in `.audit/io-convergence/final-rust-tests.log`,
`block-fixture-test.log`, and `final-rust-source-manifest.json`.

## Product verification

The offline Bazel build and the final incremental builds passed for both
`//src/observer:_liboceanbase_link` and `//src/observer:seekdb`.
The changed production and build files were checked against the recorded source manifest
and remained unchanged during the build.

```sh
source ~/.bashrc
CARGO_NET_OFFLINE=true bazel --output_base="$PWD/build" build \
  --workspace_status_command='python3 bazel/build_metadata.py status' \
  --jobs=10 --action_env=CARGO_BUILD_JOBS=1 \
  //src/observer:_liboceanbase_link //src/observer:seekdb
```

The final executable has SHA-256 `c7b243ddabae060b84a4e49e244e2b1a70ef0df75fec87ff9a12a08194ef9b61`.
The database run used the existing instance at
`/data/1/nijia.nj/test/io-three-boundaries-20261113`, with a 4 GiB memory budget and four CPUs.

The run verified these outcomes.

- The original 32,768 rows remained readable with the same aggregate checksum.
- Both retired virtual tables were absent on the initial and subsequent starts.
- A new 2,048-row table was written and its exact tablet produced a `MINI_MERGE` record.
- An orderly restart read the same new-table contents from the flushed state.
- After a committed update, `SIGKILL` terminated the database process. Restart recovered the updated contents.
- One further restart verified both the old data and new data, checking for accidental overwrite during block allocation.
- Every orderly shutdown exited with status zero. The intentional crash exited with signal 9.

| Data checked | Rows | Sum of IDs | Payload bytes | Sum of CRC32 values |
| --- | ---: | ---: | ---: | ---: |
| Historical data before and after new writes | 32768 | 536854528 | 67108864 | 70368836532672 |
| New data after flush and orderly restart | 2048 | 2096128 | 4194304 | 4394020122656 |
| Updated data after process-crash recovery | 2048 | 2096128 | 4194304 | 4370530642755 |

The flush check uses `ALTER SYSTEM MINOR FREEZE TABLET_ID=<id>` and waits for a matching
compaction-history row. An initial global freeze did not produce that table's flush record
within the observation period, so it was not counted as successful flush validation.
The crash check is a process-crash test, not a simulated power failure.

Local evidence is in `.audit/io-convergence/runtime/report.json` and its SQL outputs,
`post-write-preservation.log`, `include-cleanup-build.log`, and
`final-product-source-manifest.json`.

## Review and limits

Independent reviews covered typed owner shutdown, cancellation and callback ownership,
configuration and stop races, endpoint retirement, data capacity policy, PALF durability,
and catalog retirement before the listener starts. The final source review reported no
concrete Major or Blocker findings.

No C++ unit tests were added or run. No new tests were added under `unittest/`.
The existing SQL expected-table lists were adjusted for the two removed virtual tables.

This change has no new performance result. Removing the idle buffer cache can increase
allocator calls. Historical benchmark numbers do not describe this implementation.
