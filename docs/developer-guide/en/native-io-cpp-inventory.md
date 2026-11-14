# Remaining C++ file-I/O responsibilities

This read-only inventory describes commit `604e22787`. It separates removable
legacy code from live database responsibilities. No production implementation,
configuration, or build list was changed during the investigation.

The generic C++ I/O framework is gone: `src/share/io/`, `ObDeviceManager`,
`ObIODevice`, `ObLocalDevice`, `ObIOManager`, and `ObIOService` are absent from
production `src` and `rust`. The remaining common request integration is
`src/share/io_request.{h,cpp}`. Rust owns request execution and physical file
ownership. C++ still owns database algorithms and adapts their types to CXX.

## Counting scope

At this revision, searches over `src/**/*.h` and `src/**/*.cpp` found:

| Search | Matching files | Meaning |
| --- | ---: | --- |
| `#include "share/io_request.h"` or `storage_io::` | 41 | Direct storage bridge consumers or adapters, including headers. |
| `file_io::` or `#include "rust/file_io.h"` | 13 | Direct filesystem bridge consumers or adapters. |
| Calls named `io_submit`, `io_getevents`, `io_setup`, or `io_destroy` | 0 | No native-AIO syscall call sites matching these names in this C++ scope. |
| Calls named `pread`, `pwrite`, their `64` forms, `fsync`, or `fdatasync` | 27 | Candidates, mostly domain methods or Rust-backed adapters, not 27 OS implementations. |

The sets overlap. They do not count transitive callers, generated build outputs,
network I/O, or every platform file operation. They must not be reported as the
number of remaining C++ I/O implementation files. The temporary-file directory
alone contains 46 files, implementing caches, page allocation, flushing and
metadata, rather than 46 independent physical file backends.

Exact manifests and search expressions are saved in
`.audit/io-cpp-inventory/inventory.json` and its sibling text files. Reproduce the
main consumer list with:

```sh
rg -l '#include "share/io_request.h"|storage_io::' src -g '*.cpp' -g '*.h' | sort
rg -l 'file_io::|#include "rust/file_io.h"' src -g '*.cpp' -g '*.h' | sort
```

## First cleanup: remove code with no consumers

These candidates were checked by repository-wide symbol and include searches,
then against build entries. Deletion has not been performed or build-verified.

| File or API | Evidence | Recommendation |
| --- | --- | --- |
| `src/oblib/lib/file/ob_fileinfo_manager.h` | `IFileInfo` and `IFileInfoMgr` occur only in their definitions. No include, implementation or consumer was found. `src/oblib/BUILD.bazel` exports the header. | Delete the 66-line header and its build export. Do not migrate the old fd registry interface. |
| `src/oblib/lib/file/wasm_file.h` | `zero_file_range` and `extend_file_with_zeros` occur only inside this header. No consumer was found; the build exports it. | Delete the 78-line unused compatibility implementation and export. This does not delete the active WASM filesystem adapter. |
| `src/oblib/lib/file/config.h` and `config.cpp` | `ObSysConfig` and `OB_TBSYS_CONFIG` have no consumers outside these files. Only the header is exported; the cpp is not in the source inventory. | Delete both legacy parser files and the header export, 419 lines total. They are separate from the current Rust configuration system. |
| `ObIOHandle::check_is_finished` and `ObStorageObjectHandle::check_is_finished` | The only references are declarations, definitions, and the forwarding call between them. No caller reaches the outer method. | Delete this unused method chain from `share/io_request.*` and `storage/blocksstable/ob_storage_object_handle.*`. Retain the live `is_finished()` API. |
| `src/sql/engine/ob_io_event_observer.h` | Contains only an include of `query/engine/ob_io_event_observer.h`. `ob_dh_msg.h` and `ob_pdml_op_batch_row_cache.h` still use the old include path. | Migrate those include paths and remove the forwarding header/build entry. Retain the actual observer class. |

The first four physical files total 563 lines, including comments and headers.
This is source cleanup, not a measured binary-size or runtime improvement.
`ob_string_util.{h,cpp}` must stay: vector-index validation uses `is_int`.

## Live C++ boundaries

A file family includes its existing header and implementation where both exist.

| Location or file family | Actual responsibility and consumers | Decision |
| --- | --- | --- |
| `share/io_request` | Converts database flags/errors, owns a generated CXX Request through ObIOHandle, exposes results and callback ownership. BlockManager and PALF construct requests; storage/cache callers consume handles. | Keep the thin adapter; remove only proven unused methods. No queue or independent request state machine remains here. |
| `rust/storage-io/include/callback_adapter.h`, implementation in `share/io_request.cpp` | Owns allocator-backed C++ callbacks and exposes their processing and typed micro-block results to Rust. | Keep while decode/cache objects remain C++. CXX generates ABI glue, not these business semantics. |
| `rust/file-io/src/bridge.rs`, `rust/storage-io/src/bridge.rs`, `rust/BUILD.bazel` | Generate `rust/file_io.h`, `rust/storage_io.h`, and cross-language glue. | Keep existing CXX integration. These generated headers are not handwritten legacy wrappers. |
| `storage/blocksstable/ob_block_manager` | Database macro-block identity, business references, recovery/root traversal and Rust BlockOwner access. Physical allocation, growth, sizing and admission are native Rust operations. StorageObjectHandle submits through it. | Keep the cohesive business-reference/recovery/collection protocol and domain submission. C++ sizing algorithms, allocation retry, ObjectManager resize lock, dead disk-check/write-limit methods and unused raw block CXX read/write endpoints have been removed. |
| `ob_storage_object_handle`, `ob_storage_object_rw_info`, `ob_macro_block_handle` | Database object identity, macro-block references, and borrowed completed results. | Keep live ownership semantics; trim only the unused query method above. |
| `ob_micro_block_cache`, `storage/access/ob_micro_block_handle_mgr`, `storage/meta_mem/ob_storage_meta_cache` | Decode/decompress read bytes and populate allocator/cache-owned database objects. Micro-block callbacks call `ObMacroBlockReader::do_decompress_data`. | Keep for this cleanup. A Rust codec/cache migration is a separate cohesive project. |
| `logservice/palf/log_io_adapter` | Converts PALF synchronous read/write contracts to NamedFile submission and wait; copies read results into caller buffers. Direct reads and truncation also call Rust. | Keep the thin PALF adapter. `pread_direct` has a real tail-block repair caller in `log_block_handler.cpp`. |
| `logservice/ob_server_log_block_mgr` | PALF inventory capacity, numeric block-name recovery, directory layout and tail repair, using Rust NamedOwnerHandle and file-io operations. | Keep PALF policy; consider consolidating repeated directory scans separately. |
| `palf/log_block_handler`, `log_block_mgr`, `log_storage`, `log_reader`, `log_iterator_storage` | LSN ranges, log-block buffers, read cache, layout and recovery. They call LogIOAdapter/Rust owners. | Keep domain algorithms. Their methods named pread/pwrite are not raw OS implementations. |
| `palf/log_io_worker`, `log_io_worker_wrapper`, `log_io_task`, callback task pool/utilities, `log_io_context` | Log-task ordering, batching, flush/throttling, retries and business callbacks. | Keep for now. They are not interchangeable with Tokio's file-operation pool; removing a scheduling hop requires a separate ordering/durability design. |
| `storage/tmp_file/*`, `data_plane/api/data_plane/tmp_file/ob_tmp_file.h` | SQL spill page caches, write buffers, flush/eviction, metadata and file lifecycle. Temp-page reads go through StorageObjectHandle and the same data-block owner. | Keep functionality. Simplify the single-implementation layering described below rather than porting all 46 files mechanically. |
| `oblib/lib/file/ob_file`, `path_operations` | Convert database errors and strings to Rust ReaderFacade/AppenderFacade and path operations. Used by LOAD DATA, SELECT INTO, optimizer trace, startup and PALF. | Keep thin compatibility at live callers. The physical operations are already Rust-backed. |
| `query/engine/ob_io_event_observer.h` | Adds SQL temporary-file call time to block_time; feedback reaches plan io_cost and `IO TIME(us)`/`IO_COST`. | Keep SQL diagnostics unless the feature is deliberately removed. Network waits also contribute to block_time; it is not pure disk latency. |
| SQL LOAD DATA and external-file writer/select-into families | Input protocol, compression, formats, buffers and SQL policies around Rust-backed stream I/O. | Keep SQL responsibilities. |
| `observer/ob_server`, runtime controller | Creates/configures/stops Service and passes owners and resource limits to database modules. | Keep application lifecycle integration. CXX does not generate startup policy. |
| `observer/virtual_table/ob_all_virtual_io_status` | Converts a Rust observed-statistics snapshot to SQL rows; registered in the iterator factory. | Keep passive presentation. It does not reintroduce health probes. |
| `ob_all_disk_stat`, `ob_all_virtual_tmp_file` | Display block capacity and temporary-file metadata. | Keep independent diagnostic consumers. |
| Embedded client lock, thread/proc diagnostics, WASM filesystem adapter | Client/process coordination and platform services outside data/PALF request execution. | Separate platform scope if eliminating all C++ filesystem operations is later required. Do not silently mix them into storage-io. |

## Simplification requiring a separate change

`ObTmpFileManager` forwards to `ObSNTmpFileManager`, which inherits
`ObITmpFileManager`. The source has one implementation of that base. The same
pattern exists for `ObITmpFile`/`ObSharedNothingTmpFile` and
`ObITmpFileHandle`/`ObSNTmpFileHandle`.

This is the strongest next architectural simplification candidate: keep the
public data-plane contract and consolidate the unique implementation and its
state. First trace destructor, ownership and override behavior. Do not claim
that unique inheritance alone proves every base can be deleted unchanged.

`ILogBlockPool` also merits a later interface review, but it still connects PALF
block management to capacity policy through real create/delete/rename calls.
It is not currently a proven dead interface.

## Configuration and identifiers

- `disk_io_thread_count` currently feeds the PALF owner worker limit. YAML declares
  default 1 and range `[1,32]`, while its description says default 8 and even
  `[2,32]`. The legacy EvenInt checker also needs validation-path review before
  changing accepted values. This is not the native-AIO completion-thread count.
- `sync_io_thread_count` feeds the data owner worker ceiling. Zero currently uses
  Rust available_parallelism, not database cpu_count. Document the actual behavior;
  changing default sizing is a separate policy decision.
- `_io_callback_thread_count` zero resolves to 16, and the ceiling is restart-effective.
  Its description still claims dynamic memory-based selection. Correct that text.
- Reload-time `validate_io_threads()` has callers. Do not delete it without tracing
  static-configuration staging and rejection semantics.
- `SLOG_IO`, `DETECT_IO`, `BACKUP_READER_IO`, and `TEST_CALLBACK` appear definition-only.
  Removing enum entries can renumber statistics lanes and visible MODULE_ID values.
  Preserve IDs or explicitly decide the output change before removing them.
- `CALIBRATION_IO` remains used by the optimizer workload in
  `storage/tx_storage/ob_access_service.cpp`; it is not the removed calibration
  virtual-table feature. `get_io_interval`, `get_io_time_us`, and `get_fs_errno`
  likewise retain real consumers.

## Recommended order and final boundary

1. Remove the four unused legacy files and unused completion-query chain. Update
   build entries and verify the affected product build.
2. Correct configuration descriptions. Migrate old observer include paths before
   deleting their forwarding header. Keep identifier changes separate.
3. Simplify temporary-file single-implementation layers as a focused follow-up,
   preserving page/cache/lifetime behavior.
4. Leave PALF scheduling and micro-block codec/cache migration outside this cleanup.

The final boundary remains: C++ owns database policy, formats and caller-facing
objects; generated CXX plus small handwritten business adapters connect those
objects to Rust-owned files, requests and execution. A zero-C++-file target would
require migrating those database domains too, not merely finishing the I/O layer.

Evidence is source-reference and build-list inspection, not deletion validation.
No functional tests or performance runs were needed for this read-only inventory.

## Implemented cleanup

The first cleanup items and temporary-file consolidation have now been implemented.
The tables above preserve the pre-change inventory at `604e22787` and its rationale.
See [cleanup validation](native-io-cpp-cleanup-validation.md) for the resulting
structure, retained boundaries and before/after runtime evidence.
