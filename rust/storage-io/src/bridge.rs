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

use crate::RequestLifetime;
mod named;
use named::{
    bind_named_target, new_named_bindings, new_named_owner, NamedBindings, NamedOwnerHandle,
};
mod blocks;
mod calibration;
use calibration::{calibration_integer, calibration_mbps, empty_calibration_view, CalibrationView};
mod statistics;
use statistics::{
    new_statistics_handle, new_statistics_snapshot, StatisticsHandle, StatisticsSnapshot,
};
mod requests;
use blocks::{bind_block_target, new_block_owner, read_block, write_block, BlockOwner};
use file_io::toggle_worker_fault_delay;
use requests::{
    bind_registered_target, new_cancel_handle, new_completion_slot, new_device_handle,
    new_request_lifetime, new_request_slot, new_service_handle, CancelHandle, CompletionSlot,
    DeviceHandle, RequestSlot, ServiceHandle,
};

#[cxx::bridge(namespace = "oceanbase::storage_io")]
mod ffi {
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
    enum StatisticsSampleMode {
        Cached,
        Advance,
        Preview,
    }
    struct CompletionEventDto {
        lane: usize,
        success: bool,
        bytes: u64,
        prepare_us: u64,
        schedule_us: u64,
        submit_us: u64,
        device_us: u64,
        total_us: u64,
    }
    struct CompletionRatesDto {
        error: i32,
        average_bytes: f64,
        iops: f64,
        bytes_per_second: i64,
        prepare_us: i64,
        schedule_us: i64,
        submit_us: i64,
        device_us: i64,
        total_us: i64,
    }

    struct CalibrationSample {
        mode: i32,
        size: i64,
        iops: f64,
        latency_us: f64,
    }
    struct CalibrationScale {
        error: i32,
        value: f64,
        calibrated: bool,
    }
    struct CalibrationMetric {
        error: i32,
        value: i64,
    }

    enum HeaderStatus {
        Ok,
        InvalidArgument,
        InvalidData,
        ChecksumMismatch,
    }

    struct GroupPolicy {
        id: u64,
        minimum_iops: u64,
        maximum_iops: u64,
        unlimited: bool,
        excess_weight: u32,
        burst: u64,
    }

    struct DevicePolicy {
        id: u64,
        depth: usize,
        probe_depth: usize,
        max_probe_bytes: usize,
    }

    struct HealthStatus {
        warning: bool,
        warning_wall_us: i64,
        queued: usize,
        cleaning: usize,
        active: usize,
        waiting_capacity: usize,
        active_capacity: usize,
        /// Reserved typed payload, not heap usage. Excludes opaque allocation headers,
        /// allocator overhead and separately accounted I/O buffers; shared allowances overlap.
        owned_payload_bytes: usize,
        accepted_error: u64,
        accepted_timeout: u64,
        full: u64,
        busy: u64,
        stopped: u64,
        already_observed: u64,
    }
    struct FinalFacts {
        io_error: bool,
        os_error: i32,
        deadline_exceeded: bool,
        finished_wall_us: i64,
    }
    enum ObservationOutcome {
        NotOffered,
        Accepted,
        Full,
        Busy,
        Stopped,
        AlreadyObserved,
    }
    struct ObservationReceipt {
        error: ObservationOutcome,
        timeout: ObservationOutcome,
    }
    struct ResourceConfig {
        health_warning_tolerance_us: u64,
        health_blacklist_interval_us: u64,
        health_wait_cap_us: u64,
        max_records: usize,
        max_bytes: usize,
        probe_records: usize,
        probe_bytes: usize,
        default_group: u64,
        capacity_iops: u64,
        unlimited_capacity: bool,
        groups: Vec<GroupPolicy>,
        devices: Vec<DevicePolicy>,
    }

    struct ConfigReceipt {
        error: i32,
        generation: u64,
        normal_record_debt: usize,
        normal_byte_debt: usize,
        probe_record_debt: usize,
        probe_byte_debt: usize,
        physical_debt: bool,
    }

    struct DeviceRemovalReceipt {
        error: i32,
        generation: u64,
    }

    struct DeviceOptions {
        id: u64,
        native_aio: bool,
        worker_threads: usize,
        backend_capacity: usize,
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
        direct_io: bool,
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

    struct RequestOptions {
        group: u64,
        probe: bool,
        worker_required: bool,
        rate_exempt: bool,
        mode: RequestMode,
        offset: u64,
        length: usize,
        alignment: usize,
        window_start: usize,
        window_length: usize,
        timeout_us: i64,
        retry_alignment: usize,
        max_error_retries: usize,
        full_buffer: bool,
        tag: u64,
        worker_fault: i32,
        observe_health: bool,
    }

    struct SubmitResult {
        error: i32,
        id: u64,
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
        has_buffer: bool,
        group: u64,
        physical_attempts: u64,
        raw_depth: usize,
        effective_depth: usize,
    }

    struct GroupStats {
        id: u64,
        physical_attempts: u64,
        rate_exempt_attempts: u64,
        rate_throttled: bool,
        device_blocked: bool,
    }

    struct ServiceStats {
        generation: u64,
        probe_attempts: u64,
        admission_debt: bool,
        physical_debt: bool,
        groups: Vec<GroupStats>,
        admitted_requests: usize,
        reserved_bytes: usize,
        queued_requests: usize,
        awaiting_retirement: usize,
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
        type FileRegistry = file_io::bridge::FileRegistry;
        #[namespace = "oceanbase::file_io"]
        type DirectoryHandle = file_io::bridge::DirectoryHandle;
        #[namespace = "oceanbase::file_io"]
        type FileStat = file_io::bridge::ffi::FileStat;
        #[namespace = "oceanbase::file_io"]
        type IoResult = file_io::bridge::ffi::IoResult;
    }

    extern "Rust" {
        type StatisticsHandle;
        type StatisticsSnapshot;
        fn new_statistics_handle() -> Box<StatisticsHandle>;
        fn new_statistics_snapshot() -> Box<StatisticsSnapshot>;
        fn initialize_statistics(self: &mut StatisticsHandle, lanes: usize, now_us: i64) -> i32;
        fn record_completion(self: &StatisticsHandle, event: CompletionEventDto) -> i32;
        fn statistics_snapshot(
            self: &StatisticsHandle,
            now_us: i64,
            sample: StatisticsSampleMode,
            output: &mut StatisticsSnapshot,
        ) -> i32;
        fn sample_failed_statistics(
            self: &StatisticsHandle,
            lane: usize,
            now_us: i64,
        ) -> CompletionRatesDto;
        fn statistics_at(self: &StatisticsSnapshot, lane: usize) -> CompletionRatesDto;

        fn toggle_worker_fault_delay();
        fn encode_block_header(block_size: u64, page: &mut [u8]) -> HeaderStatus;
        fn validate_block_header(block_size: u64, page: &[u8]) -> HeaderStatus;

        type NamedOwnerHandle;
        type NamedBindings;
        fn new_named_owner() -> Box<NamedOwnerHandle>;
        fn new_named_bindings() -> Box<NamedBindings>;
        fn begin_named_inventory(self: &mut NamedOwnerHandle, capacity: u64) -> i32;
        fn named_owner_replaceable(self: &NamedOwnerHandle) -> bool;
        fn close_named_owner(self: &NamedOwnerHandle);
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
        fn named_enabled(self: &NamedBindings) -> bool;
        fn attach_named_owner(self: &mut NamedBindings, owner: &NamedOwnerHandle) -> i32;
        fn clear_named_bindings(self: &mut NamedBindings);
        fn close_named_binding(self: &mut NamedBindings, token: u64);
        fn open_named(
            self: &mut NamedBindings,
            registry: &FileRegistry,
            path: &[u8],
            flags: i32,
            mode: u32,
            token: &mut u64,
        ) -> i32;
        fn read_named(self: &NamedBindings, token: u64, bytes: &mut [u8], offset: u64) -> IoResult;
        fn write_named(self: &NamedBindings, token: u64, bytes: &[u8], offset: u64) -> IoResult;
        fn sync_named(self: &NamedBindings, token: u64, data_only: bool) -> i32;
        fn resize_named(self: &NamedBindings, token: u64, length: u64) -> i32;
        fn allocate_named(
            self: &NamedBindings,
            token: u64,
            mode: i32,
            offset: u64,
            length: u64,
        ) -> i32;
        fn stat_named(self: &NamedBindings, token: u64, out: &mut FileStat) -> i32;
        fn bind_named_target(
            bindings: &NamedBindings,
            token: u64,
            slot: &mut RequestSlot,
            lifetime: &mut RequestLifetime,
        ) -> i32;
        type BlockOwner;
        fn new_block_owner() -> Box<BlockOwner>;
        fn open_blocks(self: &BlockOwner, path: &[u8], options: &BlockOptions) -> BlockOpenResult;
        fn close_blocks(self: &BlockOwner);
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
        fn bind_block_target(
            owner: &BlockOwner,
            index: u64,
            slot: &mut RequestSlot,
            lifetime: &mut RequestLifetime,
        ) -> i32;

        type CalibrationView;
        fn empty_calibration_view() -> Box<CalibrationView>;
        fn calibration_snapshot(self: &ServiceHandle) -> Box<CalibrationView>;
        fn replace_calibration(self: &ServiceHandle, samples: &[CalibrationSample]) -> i32;
        fn reset_calibration(self: &ServiceHandle) -> i32;
        fn calibrated(self: &CalibrationView) -> bool;
        fn calibration_generation(self: &CalibrationView) -> u64;
        fn calibration_sample(
            self: &CalibrationView,
            mode: i32,
            index: usize,
            output: &mut CalibrationSample,
        ) -> bool;
        fn calibration_scale(
            self: &CalibrationView,
            mode: i32,
            size: i64,
            initialized: bool,
        ) -> CalibrationScale;
        fn normalized_iops(
            self: &CalibrationView,
            mode: i32,
            size: i64,
            iops: f64,
            initialized: bool,
        ) -> CalibrationMetric;
        fn calibration_integer(value: f64) -> CalibrationMetric;
        fn calibration_mbps(iops: i64, size: i64) -> CalibrationMetric;
        type ServiceHandle;
        type DeviceHandle;
        type RequestSlot;
        type RequestLifetime;
        fn new_request_lifetime() -> Box<RequestLifetime>;
        fn observe_final(self: &RequestLifetime, facts: &FinalFacts) -> ObservationReceipt;
        fn health_status(self: &ServiceHandle, output: &mut HealthStatus) -> i32;
        type CancelHandle;
        type CompletionSlot;
        fn new_service_handle() -> Box<ServiceHandle>;
        fn start_service(self: &mut ServiceHandle, config: &ResourceConfig) -> i32;
        fn stop_service(self: &ServiceHandle);
        fn shutdown_service(self: &mut ServiceHandle) -> i32;
        fn service_stats(self: &ServiceHandle, stats: &mut ServiceStats) -> i32;
        fn apply_config(
            self: &ServiceHandle,
            expected: u64,
            config: &ResourceConfig,
        ) -> ConfigReceipt;
        fn new_device_handle() -> Box<DeviceHandle>;
        fn register_device(
            self: &ServiceHandle,
            expected: u64,
            config: &ResourceConfig,
            options: &DeviceOptions,
            device: &mut DeviceHandle,
        ) -> ConfigReceipt;
        fn remove_device(self: &ServiceHandle, expected: u64, device: u64) -> DeviceRemovalReceipt;
        fn close_device(self: &DeviceHandle) -> i32;
        fn new_request_slot() -> Box<RequestSlot>;
        fn new_cancel_handle() -> Box<CancelHandle>;
        fn clone_cancel_handle(self: &CancelHandle) -> Box<CancelHandle>;
        fn request_cancel(self: &CancelHandle) -> bool;
        fn begin_request(
            self: &DeviceHandle,
            options: &RequestOptions,
            request: &mut RequestSlot,
            control: &mut CancelHandle,
            lifetime: &mut RequestLifetime,
        ) -> i32;
        fn wait_capacity(self: &mut RequestSlot) -> i32;
        fn prepare_reserved(self: &mut RequestSlot, write_bytes: &[u8]) -> i32;
        fn has_request(self: &RequestSlot) -> bool;
        fn clear_request(self: &mut RequestSlot);
        fn bind_registered_target(
            files: &FileRegistry,
            id: u64,
            request: &mut RequestSlot,
            lifetime: &mut RequestLifetime,
        ) -> i32;
        fn publish(self: &mut RequestSlot) -> SubmitResult;
        fn new_completion_slot() -> Box<CompletionSlot>;
        /// A populated destination is rejected rather than replacing its borrowed data.
        fn poll_completion(
            self: &DeviceHandle,
            wait_us: u64,
            completion: &mut CompletionSlot,
        ) -> i32;
        fn completion_info(self: &CompletionSlot) -> CompletionInfo;
        /// The view expires when the completion slot is cleared or destroyed.
        fn completion_data(self: &CompletionSlot) -> &[u8];
        fn clear_completion(self: &mut CompletionSlot);
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
