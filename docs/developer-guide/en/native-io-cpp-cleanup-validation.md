# C++ I/O cleanup and temporary-file consolidation

The cleanup removes unused interfaces and collapses the temporary-file module's
single-implementation class layers. Physical I/O remains in Rust. The SQL data-plane
API, page/cache algorithms and allocator ownership stay unchanged.

## Structure

The first commit, `2c1b553a6`, deletes four unused legacy file helpers and their
build entries, removes the unused completion-query forwarding chain, migrates the
two old SQL observer includes, and deletes the forwarding header. Three I/O worker
configuration descriptions now describe the current pools. Accepted configuration
values and checker policy did not change. In particular, disk_io_thread_count
still defaults to 1 while explicit values pass the legacy even-number checker;
the new description states this distinction.

The temporary-file change then consolidates:

| Before | After |
| --- | --- |
| ObTmpFileManager forwarding to ObSNTmpFileManager, inheriting ObITmpFileManager | One concrete ObTmpFileManager owning the map, allocators and real components |
| ObSharedNothingTmpFile inheriting ObITmpFile | One concrete ObSharedNothingTmpFile |
| ObSNTmpFileHandle inheriting ObITmpFileHandle | One handle directly referencing the concrete file |

The directory goes from 46 files to 42. Common read/write algorithms remain in
`ob_tmp_file_io.cpp`; disk/metadata/flush algorithms remain in
`ob_shared_nothing_tmp_file.cpp`. `ob_tmp_file_types.h` holds diagnostic data, the
file key and handle. This splits implementation by responsibility without adding
another class or forwarding layer. Old base types, SN manager types and
`get_sn_file_manager()` references are removed, with no compatibility aliases.

Cache, buffer-pool, block-manager, flush and eviction components remain because
they perform distinct work. The diagnostic Info hierarchy and ObTmpFileIOHandle
also remain. No new C++ tests or files under unittest were added.

Two independent sketches agreed on concrete ownership. The chosen design keeps
separate implementation files to avoid a larger monolithic source. Independent
review found no blocking correctness issues. It checked constructor/reset ordering,
truncate locking, lookup/removal after stop, intrusive node references, and the
sequence `release_resource` → final-handle destructor → manager allocator free.
The data-plane implementation tail is unchanged; moved common algorithms retain
their bodies apart from type and private-helper names.

## Before/after SQL behavior

Separate instances used frozen old and new executables with identical settings:
4 GiB memory, four configured CPUs, MANUAL work areas, 2 MiB sort area, 4 MiB hash
area, and deterministic data containing 65,536 rows with 1,024-byte payloads.
The data contains 64 MiB of payload and an ID sum of 2,147,450,880.

Both executables passed:

- ORDER BY returned all 65,536 rows in the expected order and with the expected
  payload. Output SHA-256 was
  `848391089b1ebec0f225293d7929e29cd4b50faf5167f347b81d37a490497f6d`.
- Hash aggregation returned every expected payload/count/ID sum. Its output also
  matched between versions; correctness was checked as a set independently of
  result order.
- Workarea diagnostics reported ONE PASS for both operators. Sort used a
  71,277,056-byte temporary segment and aggregation used 69,639,256 bytes on both
  versions. These prove that the test exercised spill rather than only memory.
- A larger sort was cancelled after its temporary file appeared. Both returned
  ERROR 1317, cleaned up to zero temporary files, and retained the expected table
  row count, ID sum and payload size.
- Normal shutdown, restart, repeated sorting and final shutdown succeeded. The
  restarted sorted output had the same hash, no temporary files remained, and
  both final child processes exited with code 0.

The first shutdown script incorrectly used SIGTERM, which this project's signal
handler turns into SIGKILL. That attempt is retained as a failed verification
artifact. The script was corrected to SIGUSR1 and the complete normal
shutdown/restart check was rerun on both versions. The failed script attempt is
not counted as a normal shutdown pass.

The stage-one and final product builds succeeded. The source-reference audit
found no old production types/includes or get_sn_file_manager calls. Validation
instances were stopped; existing user instances were not restarted.

## Evidence and limits

Artifacts are in `.audit/io-cleanup/`: design candidates and accepted design,
`stage1-final-build.log`, `tmp-build.log`, `structure.json`, `spill_check.py`,
`cancel_check.py`, `restart_check.py`, and before/after result JSON and logs.
The SQL checks exercise actual spill, readback, cancellation cleanup and restart.
They do not exhaust every truncate boundary or failed-initialization interleaving;
those unchanged paths were additionally checked in source review.

This is a behavior-preserving simplification. No performance improvement is
claimed. Reader load decreases from three manager layers and two file/handle
layers to one concrete type for each, while real cache/flush responsibilities
remain explicit.
