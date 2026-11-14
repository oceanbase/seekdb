// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

mod named;
use named::{new_named_file, new_named_owner, NamedFile, NamedOwnerHandle};
mod blocks;
mod statistics;
use statistics::{
    metric_integer, metric_mbps, new_statistics_handle, new_statistics_snapshot, StatisticsHandle,
    StatisticsSnapshot,
};
mod requests;
use crate::Request;
use blocks::{new_block_owner, read_block, write_block, BlockOwner};
pub use ffi::RequestOptions;
use file_io::toggle_worker_fault_delay;
pub use requests::{new_service_handle, ServiceHandle};

#[cxx::bridge(namespace = "oceanbase::storage_io")]
pub mod ffi {
    #[derive(Default)]
    struct NamedStats {
        phase: u8,
        committed: u64,
        charged: u64,
        capacity: u64,
        leases: usize,
        pending: usize,
        active: usize,
        error: i32,
    }
    struct CompletionRatesDto {
        error: i32,
        average_bytes: f64,
        iops: f64,
        bytes_per_second: i64,
        total_us: i64,
    }

    struct Metric {
        error: i32,
        value: i64,
    }

    enum HeaderStatus {
        Ok,
        InvalidArgument,
        InvalidData,
        ChecksumMismatch,
    }

    struct RuntimeConfig {
        server_memory_bytes: u64,
        io_buffer_bytes: u64,
        request_limit: usize,
        callback_threads: usize,
        statistics_lanes: usize,
    }

    enum BlockCode {
        Ok,
        InvalidArgument,
        InvalidHeader,
        ChecksumMismatch,
        NotAllocated,
        MarkingAborted,
        Exhausted,
        IoError,
        Memory,
        NotOpen,
        AlreadyOpen,
    }

    struct BlockStatus {
        code: BlockCode,
        error: i32,
        os_error: i32,
    }

    struct BlockOptions {
        block_size: u64,
        initial_size: u64,
        reserved_bytes: u64,
        punch_holes: bool,
    }

    struct BlockOpenResult {
        status: BlockStatus,
        primary_error: FsError,
        sync: DirectorySyncResult,
        sync_path: Vec<u8>,
        cleanup_close: FsError,
        cleanup_remove: FsError,
        cleanup_sync: DirectorySyncResult,
        directory_summary: DirectorySyncSummary,
    }

    struct BlockRelease {
        status: BlockStatus,
        pending: bool,
        reclamation_error: i32,
        reclamation_os_error: i32,
    }

    struct BlockStats {
        block_size: u64,
        capacity: u64,
        total_blocks: u64,
        free_blocks: u64,
        pending_release_blocks: u64,
        reclamation_failures: u64,
    }

    enum RequestMode {
        Read,
        Write,
    }

    struct ExecutionOptions {
        worker_threads: usize,
        max_depth: usize,
    }

    struct RequestOptions {
        worker_required: bool,
        mode: RequestMode,
        offset: u64,
        length: usize,
        timeout_us: i64,
        statistics_lane: usize,
        worker_fault: i32,
    }

    enum CompletionCode {
        Empty,
        Complete,
        ShortIo,
        IncompleteEnvelope,
        IoError,
        Cancelled,
        TimedOut,
        Shutdown,
    }

    struct CompletionInfo {
        code: CompletionCode,
        id: u64,
        tag: u64,
        transferred: usize,
        error: i32,
        os_error: i32,
        callback_error: i32,
        has_buffer: bool,
        physical_attempts: u64,
        raw_depth: usize,
        effective_depth: usize,
    }

    unsafe extern "C++" {
        include!("rust/file_io.h");
        #[namespace = "oceanbase::file_io"]
        type FsError = file_io::bridge::ffi::FsError;
        #[namespace = "oceanbase::file_io"]
        type DirectorySyncResult = file_io::bridge::ffi::DirectorySyncResult;
        #[namespace = "oceanbase::file_io"]
        type DirectorySyncSummary = file_io::bridge::ffi::DirectorySyncSummary;
        #[namespace = "oceanbase::file_io"]
        type DirectoryHandle = file_io::bridge::DirectoryHandle;
        #[namespace = "oceanbase::file_io"]
        #[namespace = "oceanbase::file_io"]
        type IoResult = file_io::bridge::ffi::IoResult;
    }

    unsafe extern "C++" {
        include!("rust/storage-io/include/callback_adapter.h");
        type CallbackAdapter;
        fn process(self: Pin<&mut CallbackAdapter>, bytes: &[u8]) -> Result<i32>;
    }

    extern "Rust" {
        type StatisticsHandle;
        type StatisticsSnapshot;
        fn new_statistics_handle() -> Box<StatisticsHandle>;
        fn new_statistics_snapshot() -> Box<StatisticsSnapshot>;
        fn statistics_snapshot(
            self: &StatisticsHandle,
            now_us: i64,
            output: &mut StatisticsSnapshot,
        ) -> i32;
        fn statistics_at(self: &StatisticsSnapshot, lane: usize) -> CompletionRatesDto;

        fn toggle_worker_fault_delay();
        fn encode_block_header(block_size: u64, page: &mut [u8]) -> HeaderStatus;
        fn validate_block_header(block_size: u64, page: &[u8]) -> HeaderStatus;

        type NamedOwnerHandle;
        fn new_named_owner() -> Box<NamedOwnerHandle>;
        fn begin_named_inventory(
            self: &mut NamedOwnerHandle,
            service: &ServiceHandle,
            directory: &[u8],
            capacity: u64,
            execution: &ExecutionOptions,
        ) -> i32;
        fn named_owner_replaceable(self: &NamedOwnerHandle) -> bool;
        fn close_named_owner(self: &NamedOwnerHandle) -> i32;
        fn validate_named_workers(self: &NamedOwnerHandle, configured: usize) -> i32;
        fn select_named_candidate(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            name: &[u8],
            extent: u64,
            id: &mut u64,
            length: &mut u64,
        ) -> i32;
        fn repair_named_candidate(self: &NamedOwnerHandle, id: u64) -> i32;
        fn admit_named_candidate(self: &NamedOwnerHandle, id: u64) -> i32;
        fn finish_named_inventory(self: &NamedOwnerHandle) -> i32;
        fn create_and_publish_named_file(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            name: &[u8],
            destination: &[u8],
            extent: u64,
            accept_moved_identity: bool,
        ) -> i32;
        fn remove_named_file(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            name: &[u8],
            required: bool,
        ) -> i32;
        fn rename_named_file(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            old: &[u8],
            new_name: &[u8],
            accept_moved_identity: bool,
        ) -> i32;
        fn create_named_directory(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            name: &[u8],
            mode: u32,
        ) -> i32;
        fn rename_named_directory(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            old: &[u8],
            new_name: &[u8],
            accept_moved_identity: bool,
        ) -> i32;
        fn remove_named_directory(
            self: &NamedOwnerHandle,
            parent: &DirectoryHandle,
            name: &[u8],
        ) -> i32;
        fn set_named_capacity(self: &NamedOwnerHandle, capacity: u64) -> i32;
        fn named_stats(self: &NamedOwnerHandle) -> NamedStats;
        type NamedFile;
        fn new_named_file() -> Box<NamedFile>;
        fn open_named_file(
            self: &NamedOwnerHandle,
            path: &[u8],
            read_only: bool,
            file: &mut NamedFile,
        ) -> i32;
        fn submit_named(
            self: &NamedFile,
            options: &RequestOptions,
            bytes: &[u8],
            callback: UniquePtr<CallbackAdapter>,
        ) -> Box<Request>;
        fn read_named(self: &NamedFile, bytes: &mut [u8], offset: u64) -> IoResult;
        fn resize_named(self: &NamedFile, length: u64) -> i32;
        type BlockOwner;
        fn new_block_owner() -> Box<BlockOwner>;
        fn open_blocks(self: &BlockOwner, path: &[u8], options: &BlockOptions) -> BlockOpenResult;
        fn close_blocks(self: &BlockOwner) -> i32;
        fn prepare_blocks(
            self: &BlockOwner,
            service: &ServiceHandle,
            directory: &[u8],
            execution: &ExecutionOptions,
        ) -> i32;
        fn validate_block_workers(self: &BlockOwner, configured: usize) -> i32;
        fn submit_block(
            self: &BlockOwner,
            index: u64,
            options: &RequestOptions,
            bytes: &[u8],
            callback: UniquePtr<CallbackAdapter>,
        ) -> Box<Request>;
        fn mark_blocks(self: &BlockOwner, indices: &[u64]) -> BlockStatus;
        fn restart_marking(self: &BlockOwner) -> BlockStatus;
        fn finish_marking(self: &BlockOwner) -> BlockStatus;
        fn allocate_blocks(self: &BlockOwner, count: usize, out: &mut Vec<u64>) -> BlockStatus;
        fn release_block(self: &BlockOwner, index: u64) -> BlockRelease;
        fn sync_blocks(self: &BlockOwner) -> BlockStatus;
        fn grow_blocks(self: &BlockOwner, bytes: u64, reserved_bytes: u64) -> BlockStatus;
        fn block_stats(self: &BlockOwner, stats: &mut BlockStats) -> BlockStatus;
        fn read_block(owner: &BlockOwner, index: u64, offset: u64, bytes: &mut [u8]) -> IoResult;
        fn write_block(owner: &BlockOwner, index: u64, offset: u64, bytes: &[u8]) -> IoResult;
        fn metric_integer(value: f64) -> Metric;
        fn metric_mbps(iops: i64, size: i64) -> Metric;
        type ServiceHandle;
        type Request;
        fn new_service_handle() -> Box<ServiceHandle>;
        fn prepare_service(self: &ServiceHandle, config: &RuntimeConfig) -> i32;
        fn activate_service(self: &ServiceHandle) -> i32;
        fn configure_resources(
            self: &ServiceHandle,
            server_memory_bytes: u64,
            io_buffer_bytes: u64,
            callback_threads: usize,
        ) -> i32;
        fn stop_service(self: &ServiceHandle);
        fn shutdown_service(self: &ServiceHandle) -> i32;
        fn clone_handle(self: &Request) -> Box<Request>;
        fn submission_error(self: &Request) -> i32;
        #[rust_name = "cancel_for_ffi"]
        fn request_cancel(self: &Request) -> bool;
        fn is_finished(self: &Request) -> bool;
        #[rust_name = "wait_micros"]
        fn wait(self: &Request, timeout_us: u64) -> i32;
        fn result(self: &Request) -> CompletionInfo;
        fn data(self: &Request) -> &[u8];
        fn data_size(self: &Request) -> usize;
        fn has_callback_output(self: &Request) -> bool;
        // The C++ borrower must retain its Request for the lifetime of this reference.
        unsafe fn callback_output<'a>(self: &'a Request) -> Result<&'a CallbackAdapter>;
        fn elapsed_us(self: &Request) -> i64;
        fn statistics_handle(self: &ServiceHandle) -> Box<StatisticsHandle>;
    }
}

fn status(result: Result<(), crate::BlockError>) -> ffi::HeaderStatus {
    match result {
        Ok(()) => ffi::HeaderStatus::Ok,
        Err(crate::BlockError::ChecksumMismatch) => ffi::HeaderStatus::ChecksumMismatch,
        Err(crate::BlockError::InvalidInput(_)) => ffi::HeaderStatus::InvalidArgument,
        Err(_) => ffi::HeaderStatus::InvalidData,
    }
}

fn encode_block_header(block_size: u64, page: &mut [u8]) -> ffi::HeaderStatus {
    status(crate::encode_block_header(block_size, page))
}

fn validate_block_header(block_size: u64, page: &[u8]) -> ffi::HeaderStatus {
    status(crate::validate_block_header(block_size, page))
}

#[cfg(all(test, target_os = "linux"))]
mod typed_tests;
