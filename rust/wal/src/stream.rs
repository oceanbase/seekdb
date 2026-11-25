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

use crate::format::{
    block_id, block_offset, copied_bytes, crc64, crc64_with_seed, decode_group_header,
    encode_entry, encode_group, encode_segment_header, group_entries, physical_offset,
    segment_path, validate_group, validate_segment_header, zeroed_bytes, Error, GroupHeader,
    Result, SegmentHeader, BLOCK_SIZE, ENTRY_HEADER_SIZE, GROUP_HEADER_SIZE, MAX_LOG_BODY_SIZE,
    PHYSICAL_BLOCK_SIZE, SEGMENT_HEADER_SIZE,
};
use file_io::AlignedBuffer;
use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard, OnceLock};

const MANIFEST_FILE: &str = "manifest";
const STAGED_SEGMENT_FILE: &str = "segment.log.tmp";
const MANIFEST_SIZE: usize = 128;
const MANIFEST_FILE_SIZE: usize = MANIFEST_SIZE * 2;
const MANIFEST_MAGIC: &[u8; 8] = b"WALMAN01";
const DIO_ALIGNMENT: usize = 4096;
const MAX_APPEND_BATCH_REQUESTS: usize = 128;
const MAX_APPEND_BATCH_BYTES: usize = 2 * 1024 * 1024;

pub struct ImportedGroup<'a> {
    pub source_lsn: u64,
    pub source_scn: u64,
    pub zero_padding_before: u32,
    pub bytes: &'a [u8],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BaseInfo {
    pub curr_lsn: u64,
    pub prev_lsn: u64,
    pub prev_scn: u64,
    pub prev_log_id: i64,
    pub prev_accumulated_checksum: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AppendResult {
    pub lsn: u64,
    pub scn: u64,
    pub end_lsn: u64,
    pub end_scn: u64,
}

#[derive(Clone, Copy, Debug)]
pub struct Progress {
    pub begin_scn: u64,
    pub base_lsn: u64,
    pub end_lsn: u64,
    pub end_scn: u64,
    pub ref_scn: u64,
    pub used_disk: u64,
    pub unrecyclable_disk: u64,
    pub integrity: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Next {
    Entry,
    End,
    BlockedByReplayablePoint,
}

#[derive(Clone, Debug)]
struct RecordMeta {
    lsn: u64,
    len: u64,
    header: GroupHeader,
}

#[derive(Clone, Debug)]
struct SegmentMeta {
    length: u64,
    header: SegmentHeader,
}

impl SegmentMeta {
    fn base_info(&self) -> BaseInfo {
        BaseInfo {
            curr_lsn: self.header.start_lsn,
            prev_lsn: self.header.prev_lsn,
            prev_scn: self.header.prev_scn,
            prev_log_id: self.header.prev_log_id,
            prev_accumulated_checksum: self.header.prev_accumulated_checksum,
        }
    }
}

impl RecordMeta {
    fn end_lsn(&self) -> u64 {
        self.lsn + self.len
    }
}

struct PendingAppend {
    payload: Vec<u8>,
    ref_scn: u64,
    completion: Option<Arc<AppendCompletion>>,
    result: AppendResult,
    bytes_len: usize,
}

impl PendingAppend {
    fn complete_durable(self, result: Result<AppendResult>) {
        if let Some(completion) = self.completion {
            completion.complete_durable(result);
        }
    }
}

#[derive(Default)]
struct AppendCompletion {
    durable: Mutex<Option<Result<AppendResult>>>,
    durable_changed: Condvar,
}

impl AppendCompletion {
    fn complete_durable(&self, result: Result<AppendResult>) {
        if let Ok(mut slot) = self.durable.lock() {
            *slot = Some(result);
            self.durable_changed.notify_one();
        }
    }

    fn wait_durable(&self) -> Result<AppendResult> {
        let mut result = self.durable.lock().map_err(|_| Error::Io)?;
        loop {
            if let Some(result) = result.take() {
                return result;
            }
            result = self.durable_changed.wait(result).map_err(|_| Error::Io)?;
        }
    }
}

struct PlannedRecord {
    lsn: u64,
    bytes: Vec<u8>,
    new_segment: Option<SegmentHeader>,
}

struct AppendFile {
    id: u64,
    path: PathBuf,
    file: File,
    tail: Vec<u8>,
}

#[derive(Debug)]
pub(crate) struct WriteFailure {
    pub code: Error,
    pub operation: &'static str,
    pub path: Option<PathBuf>,
    pub raw_os_error: Option<i32>,
}

impl WriteFailure {
    fn new(code: Error, operation: &'static str) -> Self {
        Self {
            code,
            operation,
            path: None,
            raw_os_error: None,
        }
    }

    fn from_io(error: std::io::Error, path: &Path, operation: &'static str) -> Self {
        Self {
            raw_os_error: error.raw_os_error(),
            code: error.into(),
            operation,
            path: Some(path.to_path_buf()),
        }
    }
}

struct AppendBatch {
    records: Vec<PlannedRecord>,
    segments: BTreeMap<u64, SegmentMeta>,
    last_record: Option<RecordMeta>,
    end_lsn: u64,
    end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
}

#[derive(Clone, Debug)]
struct Manifest {
    base_lsn: u64,
    previous: BaseInfo,
    ref_scn: u64,
}

struct ManifestWriter {
    file: File,
    generation: u64,
    #[cfg(test)]
    fail_writes: bool,
}

struct State {
    directory: PathBuf,
    manifest_writer: Option<ManifestWriter>,
    append_file: Option<AppendFile>,
    manifest: Manifest,
    segments: BTreeMap<u64, SegmentMeta>,
    last_record: Option<RecordMeta>,
    end_lsn: u64,
    end_scn: u64,
    reserved_end_lsn: u64,
    reserved_end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
    integrity: bool,
    closed: bool,
    io_in_progress: bool,
    unflushed_appends: usize,
    failed: Option<Error>,
    #[cfg(test)]
    fail_append_plan: bool,
}

#[derive(Default)]
struct AppendQueue {
    pending_appends: Vec<PendingAppend>,
    pending_append_bytes: usize,
    worker_started: bool,
    closed: bool,
}

type FlushCallback = Arc<dyn Fn(u64, u64) + Send + Sync + 'static>;
type FailureCallback = Arc<dyn Fn(&WriteFailure) + Send + Sync + 'static>;

struct StreamInner {
    state: Mutex<State>,
    appends: Mutex<AppendQueue>,
    worker_closed: Mutex<bool>,
    flush_callback: OnceLock<FlushCallback>,
    failure_callback: OnceLock<FailureCallback>,
    failure_reported: AtomicBool,
    changed: Condvar,
    append_changed: Condvar,
    worker_changed: Condvar,
    handle_count: AtomicUsize,
}

pub struct Stream {
    inner: Arc<StreamInner>,
}

impl Clone for Stream {
    fn clone(&self) -> Self {
        self.inner.handle_count.fetch_add(1, Ordering::Relaxed);
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl Drop for Stream {
    fn drop(&mut self) {
        if self.inner.handle_count.fetch_sub(1, Ordering::AcqRel) == 1 {
            let _ = self.close();
        }
    }
}

pub struct Cursor {
    inner: Arc<StreamInner>,
    next_lsn: u64,
    group_mode: bool,
    bytes: Vec<u8>,
    payload_offset: usize,
    entry_offset: usize,
    group_lsn: u64,
    current_lsn: u64,
    current_end_lsn: u64,
    current_scn: u64,
    current_log_id: i64,
    current_committed_end_lsn: u64,
    current_accumulated_checksum: u64,
    current_padding: bool,
    zero_padding_before: u32,
    next_min_scn: u64,
    seek_required: bool,
}

impl Stream {
    pub(crate) fn failed(error: Error) -> Self {
        Self::from_result(Err(error))
    }

    fn from_result(result: Result<State>) -> Self {
        let state = result.unwrap_or_else(|error| State {
            directory: PathBuf::new(),
            manifest_writer: None,
            append_file: None,
            manifest: Manifest {
                base_lsn: 0,
                previous: BaseInfo {
                    curr_lsn: 0,
                    prev_lsn: 0,
                    prev_scn: 0,
                    prev_log_id: 0,
                    prev_accumulated_checksum: 0,
                },
                ref_scn: 0,
            },
            segments: BTreeMap::new(),
            last_record: None,
            end_lsn: 0,
            end_scn: 0,
            reserved_end_lsn: 0,
            reserved_end_scn: 0,
            next_log_id: 1,
            accumulated_checksum: 0,
            integrity: false,
            closed: false,
            io_in_progress: false,
            unflushed_appends: 0,
            failed: Some(error),
            #[cfg(test)]
            fail_append_plan: false,
        });
        Self {
            inner: Arc::new(StreamInner {
                state: Mutex::new(state),
                appends: Mutex::new(AppendQueue::default()),
                worker_closed: Mutex::new(false),
                flush_callback: OnceLock::new(),
                failure_callback: OnceLock::new(),
                failure_reported: AtomicBool::new(false),
                changed: Condvar::new(),
                append_changed: Condvar::new(),
                worker_changed: Condvar::new(),
                handle_count: AtomicUsize::new(1),
            }),
        }
    }

    pub fn create(path: &str, base: BaseInfo, ref_scn: u64) -> Self {
        Self::from_result(create_state(Path::new(path), base, ref_scn))
    }

    pub fn open(path: &str) -> Self {
        Self::from_result(open_state(Path::new(path), file_io::read_exact_at))
    }

    fn lock(&self) -> Result<MutexGuard<'_, State>> {
        self.inner.state.lock().map_err(|_| Error::Io)
    }

    fn lock_appends(&self) -> Result<MutexGuard<'_, AppendQueue>> {
        self.inner.appends.lock().map_err(|_| Error::Io)
    }

    fn lock_for_write(&self) -> Result<MutexGuard<'_, State>> {
        let mut state = self.lock()?;
        while state.io_in_progress || state.unflushed_appends != 0 {
            state = self.inner.changed.wait(state).map_err(|_| Error::Io)?;
        }
        state.check_writable()?;
        Ok(state)
    }

    pub fn status(&self) -> Result<bool> {
        let state = self.lock()?;
        state.check_open()?;
        Ok(state.integrity)
    }

    pub fn set_flush_callback<F>(&self, callback: F) -> Result<()>
    where
        F: Fn(u64, u64) + Send + Sync + 'static,
    {
        self.inner
            .flush_callback
            .set(Arc::new(callback))
            .map_err(|_| Error::Invalid)
    }

    pub(crate) fn set_failure_callback<F>(&self, callback: F) -> Result<()>
    where
        F: Fn(&WriteFailure) + Send + Sync + 'static,
    {
        self.inner
            .failure_callback
            .set(Arc::new(callback))
            .map_err(|_| Error::Invalid)
    }

    pub fn append(&self, payload: &[u8], ref_scn: u64) -> Result<AppendResult> {
        let (_, completion) = self.enqueue_append(payload, ref_scn, true)?;
        let completion = completion.ok_or(Error::Io)?;
        completion.wait_durable()
    }

    pub fn submit(&self, payload: &[u8], ref_scn: u64) -> Result<AppendResult> {
        self.enqueue_append(payload, ref_scn, false)
            .map(|(result, _)| result)
    }

    fn enqueue_append(
        &self,
        payload: &[u8],
        ref_scn: u64,
        wait_for_durable: bool,
    ) -> Result<(AppendResult, Option<Arc<AppendCompletion>>)> {
        if payload.is_empty() || payload.len() > MAX_LOG_BODY_SIZE || ref_scn == u64::MAX {
            return Err(Error::Invalid);
        }
        let payload = copied_bytes(payload)?;
        let bytes_len = payload.len();
        let completion = wait_for_durable.then(|| Arc::new(AppendCompletion::default()));
        let mut queue = self.lock_appends()?;
        while !queue.closed && !queue.can_queue_append(payload.len()) {
            queue = self
                .inner
                .append_changed
                .wait(queue)
                .map_err(|_| Error::Io)?;
        }
        if queue.closed {
            return Err(Error::Closed);
        }
        let mut state = self.lock()?;
        state.check_writable()?;
        self.ensure_worker(&mut queue)?;
        let result = state.reserve_append(payload.len(), ref_scn)?;
        state.unflushed_appends += 1;
        drop(state);
        queue.enqueue_append(PendingAppend {
            payload,
            ref_scn,
            completion: completion.clone(),
            result,
            bytes_len,
        });
        drop(queue);
        self.inner.append_changed.notify_one();
        Ok((result, completion))
    }

    fn ensure_worker(&self, queue: &mut AppendQueue) -> Result<()> {
        if !queue.worker_started {
            let inner = Arc::clone(&self.inner);
            std::thread::Builder::new()
                .name("wal-io".into())
                .spawn(move || Self::run_append_worker(inner))
                .map_err(|_| Error::Io)?;
            queue.worker_started = true;
        }
        Ok(())
    }

    fn run_append_worker(inner: Arc<StreamInner>) {
        loop {
            match Self::take_next_append_batch(&inner) {
                Ok(Some(requests)) => Self::execute_append_batch(&inner, requests),
                Ok(None) => break,
                Err(failure) => {
                    let state = inner.state.lock().ok();
                    State::fail_write(&inner, state, Vec::new(), failure);
                    break;
                }
            }
        }
        Self::finish_append_worker(&inner);
    }

    fn take_next_append_batch(
        inner: &StreamInner,
    ) -> std::result::Result<Option<Vec<PendingAppend>>, WriteFailure> {
        let mut queue = inner
            .appends
            .lock()
            .map_err(|_| WriteFailure::new(Error::Io, "lock append queue"))?;
        while queue.pending_appends.is_empty() && !queue.closed {
            queue = inner
                .append_changed
                .wait(queue)
                .map_err(|_| WriteFailure::new(Error::Io, "wait for append queue"))?;
        }
        if queue.pending_appends.is_empty() {
            return Ok(None);
        }
        let requests = queue.take_append_batch();
        inner.append_changed.notify_all();
        Ok(Some(requests))
    }

    fn finish_append_worker(inner: &StreamInner) {
        let drained = {
            let mut queue = inner
                .appends
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            queue.closed = true;
            queue.pending_append_bytes = 0;
            std::mem::take(&mut queue.pending_appends)
        };
        if let Ok(mut state) = inner.state.lock() {
            state.unflushed_appends = state.unflushed_appends.saturating_sub(drained.len());
            state.closed = true;
        }
        inner.changed.notify_all();
        for request in drained {
            request.complete_durable(Err(Error::Io));
        }
        if let Ok(mut closed) = inner.worker_closed.lock() {
            *closed = true;
            inner.worker_changed.notify_all();
        }
        inner.append_changed.notify_all();
    }

    fn execute_append_batch(
        inner: &Arc<StreamInner>,
        requests: Vec<PendingAppend>,
    ) {
        let mut state = match inner.state.lock() {
            Ok(state) => state,
            Err(poisoned) => {
                drop(poisoned);
                State::fail_write(
                    inner,
                    None,
                    requests,
                    WriteFailure::new(Error::Io, "lock WAL state for append"),
                );
                return;
            }
        };
        if let Err(error) = state.check_writable() {
            state.unflushed_appends = state.unflushed_appends.saturating_sub(requests.len());
            inner.changed.notify_all();
            drop(state);
            for request in requests {
                request.complete_durable(Err(error));
            }
            return;
        }
        State::execute_append_batch(state, inner, requests);
    }

    pub fn append_imported(&self, groups: &[ImportedGroup]) -> Result<AppendResult> {
        if groups.is_empty() {
            return Err(Error::Invalid);
        }
        let mut state = self.lock_for_write()?;
        let mut plan = AppendBatch::new(&state);
        for group in groups {
            plan.push_imported(&state, group)?;
        }
        let last = groups.last().ok_or(Error::Invalid)?;
        if let Err(failure) = state.persist_append_batch(&plan) {
            let code = failure.code;
            State::fail_write(&self.inner, Some(state), Vec::new(), failure);
            return Err(code);
        }
        state.reserved_end_lsn = state.end_lsn;
        state.reserved_end_scn = state.end_scn;
        Ok(AppendResult {
            lsn: last.source_lsn,
            scn: last.source_scn,
            end_lsn: state.end_lsn,
            end_scn: state.end_scn,
        })
    }

    pub fn progress(&self) -> Result<Progress> {
        let state = self.lock()?;
        state.check_open()?;
        let begin_scn = state
            .segments
            .range(block_id(state.manifest.base_lsn)..)
            .next()
            .map(|(_, segment)| segment.header.first_scn)
            .unwrap_or(state.manifest.previous.prev_scn);
        let unrecyclable_disk = state.end_lsn.saturating_sub(state.manifest.base_lsn);
        Ok(Progress {
            begin_scn,
            base_lsn: state.manifest.base_lsn,
            end_lsn: state.end_lsn,
            end_scn: state.end_scn,
            ref_scn: state.manifest.ref_scn,
            used_disk: state.segments.values().map(|segment| segment.length).sum(),
            unrecyclable_disk,
            integrity: state.integrity,
        })
    }

    pub fn base_info(&self, lsn: u64) -> Result<BaseInfo> {
        let state = self.lock_for_write()?;
        state.check_open()?;
        if lsn > state.end_lsn || lsn < state.manifest.base_lsn {
            return Err(Error::Invalid);
        }
        let previous = state.previous_info(lsn)?;
        Ok(BaseInfo {
            curr_lsn: lsn,
            ..previous
        })
    }

    pub fn set_base_lsn(&self, lsn: u64) -> Result<Progress> {
        let mut state = self.lock_for_write()?;
        if lsn > state.end_lsn || lsn < state.manifest.base_lsn || block_offset(lsn) != 0 {
            return Err(Error::Invalid);
        }
        let previous = state.previous_info(lsn)?;
        let mut manifest = state.manifest.clone();
        manifest.base_lsn = lsn;
        manifest.previous = BaseInfo {
            curr_lsn: lsn,
            ..previous
        };
        state.publish_manifest(&manifest)?;
        state.manifest = manifest;
        let minimum_block = block_id(lsn);
        if state
            .append_file
            .as_ref()
            .is_some_and(|file| file.id < minimum_block)
        {
            state.append_file = None;
        }
        let old_blocks: Vec<u64> = state
            .segments
            .range(..minimum_block)
            .map(|(id, _)| *id)
            .collect();
        for id in old_blocks {
            match fs::remove_file(segment_path(&state.directory, id)) {
                Ok(()) => {
                    state.segments.remove(&id);
                }
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                    state.segments.remove(&id);
                }
                Err(error) => return Err(error.into()),
            }
        }
        sync_directory(&state.directory)?;
        if lsn == state.end_lsn {
            state.last_record = None;
        }
        drop(state);
        self.inner.changed.notify_all();
        self.progress()
    }

    pub fn locate_by_scn(&self, scn: u64) -> Result<u64> {
        let state = self.lock()?;
        state.check_open()?;
        let (id, _) = state
            .segments
            .iter()
            .rfind(|(_, segment)| segment.header.first_scn <= scn)
            .ok_or(Error::OutLower)?;
        Ok((id * BLOCK_SIZE).max(state.manifest.base_lsn))
    }

    pub fn locate_by_lsn(&self, lsn: u64) -> Result<u64> {
        let state = self.lock()?;
        state.check_open()?;
        if state.last_record.is_none() {
            return Err(Error::OutLower);
        }
        let (record, _) = state.find_record(lsn)?;
        Ok(record.header.max_scn)
    }

    pub fn close(&self) -> Result<()> {
        let worker_started = {
            let mut queue = self.lock_appends()?;
            queue.closed = true;
            queue.worker_started
        };
        self.inner.append_changed.notify_all();
        if !worker_started {
            let mut state = self.lock()?;
            state.closed = true;
            self.inner.changed.notify_all();
            let mut closed = self.inner.worker_closed.lock().map_err(|_| Error::Io)?;
            *closed = true;
            self.inner.worker_changed.notify_all();
            return Ok(());
        }
        let mut closed = self.inner.worker_closed.lock().map_err(|_| Error::Io)?;
        while !*closed {
            closed = self.inner.worker_changed.wait(closed).map_err(|_| Error::Io)?;
        }
        Ok(())
    }

    pub fn cursor(&self, lsn: u64, group_mode: bool) -> Cursor {
        Cursor::new(Arc::clone(&self.inner), lsn, group_mode)
    }
}

impl Cursor {
    fn new(inner: Arc<StreamInner>, lsn: u64, group_mode: bool) -> Self {
        Self {
            inner,
            next_lsn: lsn,
            group_mode,
            bytes: Vec::new(),
            payload_offset: 0,
            entry_offset: 0,
            group_lsn: 0,
            current_lsn: 0,
            current_end_lsn: 0,
            current_scn: 0,
            current_log_id: 0,
            current_committed_end_lsn: 0,
            current_accumulated_checksum: 0,
            current_padding: false,
            zero_padding_before: 0,
            next_min_scn: 0,
            seek_required: true,
        }
    }

    fn lock(&self) -> Result<MutexGuard<'_, State>> {
        self.inner.state.lock().map_err(|_| Error::Io)
    }

    pub fn reuse(&mut self, lsn: u64) -> Result<()> {
        self.next_lsn = lsn;
        self.bytes.clear();
        self.entry_offset = 0;
        self.zero_padding_before = 0;
        self.seek_required = true;
        Ok(())
    }

    pub fn next(&mut self, replayable_point: u64) -> Result<Next> {
        self.zero_padding_before = 0;
        loop {
            if !self.group_mode && !self.bytes.is_empty() {
                let entries = group_entries(&self.bytes[GROUP_HEADER_SIZE..], self.group_lsn)?;
                let mut index = self.entry_offset + 1;
                while index < entries.len() {
                    let entry = &entries[index];
                    if entry.header.scn > replayable_point {
                        self.next_min_scn = entry.header.scn;
                        return Ok(Next::BlockedByReplayablePoint);
                    }
                    if !entry.header.padding {
                        self.entry_offset = index;
                        return self.set_entry(entry).map(|_| Next::Entry);
                    }
                    index += 1;
                }
            }
            let state = self.lock()?;
            state.check_open()?;
            if self.next_lsn < state.manifest.base_lsn {
                return Err(Error::OutLower);
            }
            if self.next_lsn >= state.end_lsn {
                return Ok(Next::End);
            }
            let remaining = BLOCK_SIZE - block_offset(self.next_lsn);
            let (read_lsn, zero_padding_before) = if remaining
                < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64
                && self.next_lsn + remaining <= state.end_lsn
            {
                let next_lsn = state.skip_zero_padding(self.next_lsn)?;
                if !self.group_mode {
                    drop(state);
                    self.next_lsn = next_lsn;
                    self.bytes.clear();
                    continue;
                }
                if next_lsn == state.end_lsn {
                    return Ok(Next::End);
                }
                (next_lsn, remaining as u32)
            } else {
                (self.next_lsn, 0)
            };
            let (meta, bytes) = if self.seek_required {
                state.find_record(read_lsn)?
            } else {
                match state.read_record(read_lsn) {
                    Ok(record) => record,
                    Err(Error::OutUpper) => return Err(Error::Partial),
                    Err(error) => return Err(error),
                }
            };
            let header = validate_group(&bytes, meta.lsn)?;
            if self.group_mode {
                if read_lsn != meta.lsn {
                    return Err(Error::Partial);
                }
                drop(state);
                self.bytes = bytes;
                self.payload_offset = GROUP_HEADER_SIZE;
                self.group_lsn = meta.lsn;
                self.current_lsn = meta.lsn;
                self.current_end_lsn = meta.end_lsn();
                self.current_scn = header.max_scn;
                self.current_log_id = header.log_id;
                self.current_committed_end_lsn = header.committed_end_lsn;
                self.current_accumulated_checksum = header.accumulated_checksum;
                self.current_padding = header.padding;
                self.zero_padding_before = zero_padding_before;
                self.next_lsn = meta.end_lsn();
                self.next_min_scn = 0;
                self.seek_required = false;
                return Ok(Next::Entry);
            }
            let entries = group_entries(&bytes[GROUP_HEADER_SIZE..], meta.lsn)?;
            let mut index = entries
                .iter()
                .position(|entry| entry.lsn >= self.next_lsn)
                .ok_or(Error::Partial)?;
            if entries[index].lsn != self.next_lsn && self.next_lsn != meta.lsn {
                return Err(Error::Partial);
            }
            while index < entries.len() && entries[index].header.padding {
                index += 1;
            }
            if index == entries.len() {
                drop(state);
                self.bytes.clear();
                self.next_lsn = meta.end_lsn();
                continue;
            }
            if entries[index].header.scn > replayable_point {
                drop(state);
                self.next_min_scn = entries[index].header.scn;
                return Ok(Next::BlockedByReplayablePoint);
            }
            drop(state);
            self.bytes = bytes;
            self.entry_offset = index;
            self.group_lsn = meta.lsn;
            self.current_lsn = meta.lsn;
            self.current_end_lsn = meta.end_lsn();
            self.next_lsn = meta.end_lsn();
            self.seek_required = false;
            return self.set_entry(&entries[index]).map(|_| Next::Entry);
        }
    }

    fn set_entry(&mut self, entry: &crate::format::EntryMeta) -> Result<()> {
        self.payload_offset = GROUP_HEADER_SIZE + entry.offset + ENTRY_HEADER_SIZE;
        self.current_lsn = entry.lsn;
        self.current_end_lsn = entry.lsn + ENTRY_HEADER_SIZE as u64 + entry.header.data_len as u64;
        self.current_scn = entry.header.scn;
        self.current_log_id = 0;
        self.current_committed_end_lsn = 0;
        self.current_accumulated_checksum = entry.header.data_checksum;
        self.current_padding = entry.header.padding;
        self.next_min_scn = 0;
        Ok(())
    }

    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    pub fn payload(&self) -> &[u8] {
        let end = self.payload_offset.saturating_add(self.payload_len());
        if end <= self.bytes.len() {
            &self.bytes[self.payload_offset..end]
        } else {
            &[]
        }
    }

    pub fn payload_len(&self) -> usize {
        if self.group_mode {
            self.bytes.len().saturating_sub(GROUP_HEADER_SIZE)
        } else {
            self.current_end_lsn.saturating_sub(self.current_lsn) as usize - ENTRY_HEADER_SIZE
        }
    }

    pub fn lsn(&self) -> u64 {
        self.current_lsn
    }
    pub fn end_lsn(&self) -> u64 {
        self.current_end_lsn
    }
    pub fn next_lsn(&self) -> u64 {
        self.next_lsn
    }
    pub fn scn(&self) -> u64 {
        self.current_scn
    }
    pub fn log_id(&self) -> i64 {
        self.current_log_id
    }
    pub fn committed_end_lsn(&self) -> u64 {
        self.current_committed_end_lsn
    }
    pub fn accumulated_checksum(&self) -> u64 {
        self.current_accumulated_checksum
    }
    pub fn padding(&self) -> bool {
        self.current_padding
    }
    pub fn zero_padding_before(&self) -> u32 {
        self.zero_padding_before
    }
    pub fn next_min_scn(&self) -> u64 {
        self.next_min_scn
    }
}

impl AppendBatch {
    fn new(state: &State) -> Self {
        Self {
            records: Vec::new(),
            segments: state.segments.clone(),
            last_record: state.last_record.clone(),
            end_lsn: state.end_lsn,
            end_scn: state.end_scn,
            next_log_id: state.next_log_id,
            accumulated_checksum: state.accumulated_checksum,
        }
    }

    fn note_write(&mut self, lsn: u64, length: usize) -> Result<()> {
        let segment = self
            .segments
            .get_mut(&block_id(lsn))
            .ok_or(Error::Invalid)?;
        segment.length = segment.length.max(physical_offset(lsn) + length as u64);
        Ok(())
    }

    fn push_record(
        &mut self,
        state: &State,
        lsn: u64,
        bytes: Vec<u8>,
        meta: Option<RecordMeta>,
    ) -> Result<()> {
        let new_segment = if self.segments.contains_key(&block_id(lsn)) {
            None
        } else {
            let meta = meta.as_ref().ok_or(Error::Invalid)?;
            let mut previous =
                self.last_record
                    .as_ref()
                    .map_or(state.manifest.previous, |record| BaseInfo {
                        curr_lsn: lsn,
                        prev_lsn: record.lsn,
                        prev_scn: record.header.max_scn,
                        prev_log_id: record.header.log_id,
                        prev_accumulated_checksum: record.header.accumulated_checksum,
                    });
            previous.curr_lsn = lsn;
            Some(SegmentHeader {
                start_lsn: lsn,
                prev_lsn: previous.prev_lsn,
                prev_scn: previous.prev_scn,
                prev_log_id: previous.prev_log_id,
                prev_accumulated_checksum: previous.prev_accumulated_checksum,
                first_scn: meta.header.max_scn,
            })
        };
        if let Some(header) = &new_segment {
            self.segments.insert(
                block_id(lsn),
                SegmentMeta {
                    length: SEGMENT_HEADER_SIZE,
                    header: header.clone(),
                },
            );
        }
        self.note_write(lsn, bytes.len())?;
        if let Some(meta) = &meta {
            self.last_record = Some(meta.clone());
        }
        self.records.push(PlannedRecord {
            lsn,
            bytes,
            new_segment,
        });
        Ok(())
    }

    fn push(&mut self, state: &State, payload: &[u8], ref_scn: u64) -> Result<AppendResult> {
        let scn = ref_scn.max(self.end_scn.saturating_add(1));
        let entry = encode_entry(payload, scn, false)?;
        let group_len = GROUP_HEADER_SIZE + entry.len();
        let mut lsn = self.end_lsn;
        let remaining = BLOCK_SIZE - block_offset(lsn);
        if remaining < group_len as u64 {
            if remaining < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64 {
                let bytes = zeroed_bytes(remaining as usize)?;
                self.push_record(state, lsn, bytes, None)?;
            } else {
                let payload_len = remaining as usize - GROUP_HEADER_SIZE - ENTRY_HEADER_SIZE;
                let entry = encode_entry(&zeroed_bytes(payload_len)?, scn, true)?;
                let group = encode_group(
                    &entry,
                    lsn,
                    scn,
                    self.next_log_id,
                    self.accumulated_checksum,
                    true,
                )?;
                let header = validate_group(&group, lsn)?;
                self.push_record(
                    state,
                    lsn,
                    group,
                    Some(RecordMeta {
                        lsn,
                        len: remaining,
                        header: header.clone(),
                    }),
                )?;
                self.accumulated_checksum = header.accumulated_checksum;
                self.next_log_id = self.next_log_id.checked_add(1).ok_or(Error::Invalid)?;
            }
            self.end_lsn = (block_id(lsn) + 1) * BLOCK_SIZE;
            lsn = self.end_lsn;
        }
        let group = encode_group(
            &entry,
            lsn,
            scn,
            self.next_log_id,
            self.accumulated_checksum,
            false,
        )?;
        let header = validate_group(&group, lsn)?;
        self.push_record(
            state,
            lsn,
            group,
            Some(RecordMeta {
                lsn,
                len: group_len as u64,
                header: header.clone(),
            }),
        )?;
        self.end_lsn = lsn + group_len as u64;
        self.end_scn = scn;
        self.accumulated_checksum = header.accumulated_checksum;
        self.next_log_id = self.next_log_id.checked_add(1).ok_or(Error::Invalid)?;
        Ok(AppendResult {
            lsn: lsn + GROUP_HEADER_SIZE as u64,
            scn,
            end_lsn: self.end_lsn,
            end_scn: scn,
        })
    }

    fn push_imported(&mut self, state: &State, group: &ImportedGroup) -> Result<()> {
        if group.source_lsn < self.end_lsn {
            return Err(Error::Duplicate);
        }
        let padding = u64::from(group.zero_padding_before);
        let gap = group.source_lsn - self.end_lsn;
        if gap != padding || group.source_scn == u64::MAX {
            return Err(Error::Invalid);
        }
        if padding != 0
            && (padding >= (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64
                || padding != BLOCK_SIZE - block_offset(self.end_lsn))
        {
            return Err(Error::Invalid);
        }
        let header = validate_group(group.bytes, group.source_lsn)?;
        if header.max_scn != group.source_scn
            || header.committed_end_lsn != group.source_lsn
            || header.log_id != self.next_log_id
            || header.accumulated_checksum
                != crc64_with_seed(
                    self.accumulated_checksum,
                    &header.data_checksum.to_le_bytes(),
                )
        {
            return Err(Error::Invalid);
        }
        if padding != 0 {
            self.push_record(state, self.end_lsn, zeroed_bytes(padding as usize)?, None)?;
        }
        self.push_record(
            state,
            group.source_lsn,
            copied_bytes(group.bytes)?,
            Some(RecordMeta {
                lsn: group.source_lsn,
                len: group.bytes.len() as u64,
                header: header.clone(),
            }),
        )?;
        self.end_lsn = group.source_lsn + group.bytes.len() as u64;
        self.end_scn = group.source_scn;
        self.next_log_id = self.next_log_id.checked_add(1).ok_or(Error::Invalid)?;
        self.accumulated_checksum = header.accumulated_checksum;
        Ok(())
    }
}

impl AppendQueue {
    fn can_queue_append(&self, payload_len: usize) -> bool {
        self.pending_appends.len() < MAX_APPEND_BATCH_REQUESTS
            && (self.pending_appends.is_empty()
                || self.pending_append_bytes.saturating_add(payload_len) <= MAX_APPEND_BATCH_BYTES)
    }

    fn enqueue_append(&mut self, request: PendingAppend) {
        self.pending_append_bytes = self
            .pending_append_bytes
            .saturating_add(request.bytes_len);
        self.pending_appends.push(request);
    }

    fn take_append_batch(&mut self) -> Vec<PendingAppend> {
        let mut count = 0;
        let mut bytes = 0usize;
        for request in &self.pending_appends {
            if count == MAX_APPEND_BATCH_REQUESTS
                || (count > 0
                    && bytes.saturating_add(request.bytes_len) > MAX_APPEND_BATCH_BYTES)
            {
                break;
            }
            count += 1;
            bytes = bytes.saturating_add(request.bytes_len);
        }
        self.pending_append_bytes = self.pending_append_bytes.saturating_sub(bytes);
        self.pending_appends.drain(..count).collect()
    }

}

impl State {
    fn check_open(&self) -> Result<()> {
        if self.closed {
            Err(Error::Closed)
        } else if let Some(error) = self.failed {
            Err(error)
        } else {
            Ok(())
        }
    }

    fn publish_manifest(&mut self, manifest: &Manifest) -> Result<()> {
        self.manifest_writer
            .as_mut()
            .ok_or(Error::Io)?
            .write(manifest)
    }

    fn check_writable(&self) -> Result<()> {
        self.check_open()
    }

    fn reserve_append(&mut self, payload_len: usize, ref_scn: u64) -> Result<AppendResult> {
        let scn = ref_scn.max(self.reserved_end_scn.saturating_add(1));
        let group_len = GROUP_HEADER_SIZE
            .checked_add(ENTRY_HEADER_SIZE)
            .and_then(|len| len.checked_add(payload_len))
            .ok_or(Error::Invalid)? as u64;
        let mut lsn = self.reserved_end_lsn;
        if BLOCK_SIZE - block_offset(lsn) < group_len {
            lsn = (block_id(lsn) + 1) * BLOCK_SIZE;
        }
        self.reserved_end_lsn = lsn.checked_add(group_len).ok_or(Error::Invalid)?;
        self.reserved_end_scn = scn;
        Ok(AppendResult {
            lsn: lsn + GROUP_HEADER_SIZE as u64,
            scn,
            end_lsn: self.reserved_end_lsn,
            end_scn: scn,
        })
    }

    fn plan_appends(
        &self,
        requests: &[PendingAppend],
    ) -> std::result::Result<AppendBatch, WriteFailure> {
        #[cfg(test)]
        if self.fail_append_plan {
            return Err(WriteFailure::new(Error::NoMemory, "plan append batch"));
        }
        let mut plan = AppendBatch::new(self);
        for request in requests {
            let result = plan
                .push(self, &request.payload, request.ref_scn)
                .map_err(|error| WriteFailure::new(error, "plan append batch"))?;
            if result != request.result {
                return Err(WriteFailure::new(Error::Corrupt, "verify append reservation"));
            }
        }
        Ok(plan)
    }

    fn execute_append_batch(
        mut state: MutexGuard<'_, State>,
        inner: &Arc<StreamInner>,
        requests: Vec<PendingAppend>,
    ) {
        let plan = match state.plan_appends(&requests) {
            Ok(plan) => plan,
            Err(failure) => {
                Self::fail_write(inner, Some(state), requests, failure);
                return;
            }
        };
        state.io_in_progress = true;
        let directory = state.directory.clone();
        let mut append_file = state.append_file.take();
        drop(state);
        let write_result = Self::write_planned_records(&directory, &mut append_file, &plan.records);
        let mut state = match inner.state.lock() {
            Ok(state) => state,
            Err(poisoned) => {
                drop(poisoned);
                let failure = write_result
                    .err()
                    .unwrap_or_else(|| WriteFailure::new(Error::Io, "lock WAL state after write"));
                Self::fail_write(inner, None, requests, failure);
                return;
            }
        };
        state.append_file = append_file;
        if let Err(failure) = write_result {
            Self::fail_write(inner, Some(state), requests, failure);
            return;
        }
        state.io_in_progress = false;
        state.unflushed_appends = state.unflushed_appends.saturating_sub(requests.len());
        inner.changed.notify_all();
        state.publish_append_batch(&plan);
        let flush_result = requests.last().map(|request| request.result);
        drop(state);
        for request in requests {
            let result = request.result;
            request.complete_durable(Ok(result));
        }
        if let Some(result) = flush_result {
            Self::publish_flush(inner, result.end_lsn, result.end_scn);
        }
    }

    fn publish_flush(inner: &Arc<StreamInner>, end_lsn: u64, end_scn: u64) {
        if let Some(callback) = inner.flush_callback.get() {
            callback(end_lsn, end_scn);
        }
    }

    fn fail_write(
        inner: &StreamInner,
        mut state: Option<MutexGuard<'_, State>>,
        requests: Vec<PendingAppend>,
        failure: WriteFailure,
    ) {
        let mut code = failure.code;
        if let Some(state) = &mut state {
            code = *state.failed.get_or_insert(code);
            state.io_in_progress = false;
            state.unflushed_appends = state.unflushed_appends.saturating_sub(requests.len());
        }
        let callback = inner
            .failure_callback
            .get()
            .filter(|_| !inner.failure_reported.swap(true, Ordering::Relaxed));
        drop(state);
        inner.changed.notify_all();
        if let Some(callback) = callback {
            callback(&failure);
        }
        for request in requests {
            request.complete_durable(Err(code));
        }
    }

    fn persist_append_batch(&mut self, plan: &AppendBatch) -> std::result::Result<(), WriteFailure> {
        Self::write_planned_records(&self.directory, &mut self.append_file, &plan.records)?;
        self.publish_append_batch(plan);
        Ok(())
    }

    fn publish_append_batch(&mut self, plan: &AppendBatch) {
        self.segments = plan.segments.clone();
        self.last_record = plan.last_record.clone();
        self.end_lsn = plan.end_lsn;
        self.end_scn = plan.end_scn;
        self.next_log_id = plan.next_log_id;
        self.accumulated_checksum = plan.accumulated_checksum;
    }

    fn write_planned_records(
        directory: &Path,
        append_file: &mut Option<AppendFile>,
        records: &[PlannedRecord],
    ) -> std::result::Result<(), WriteFailure> {
        let mut index = 0;
        while index < records.len() {
            let id = block_id(records[index].lsn);
            let create = records[index].new_segment.is_some();
            let start = if create {
                0
            } else {
                physical_offset(records[index].lsn)
            };
            let mut end = index;
            let mut write_len = if create {
                SEGMENT_HEADER_SIZE as usize
            } else {
                0
            };
            while end < records.len() && block_id(records[end].lsn) == id {
                write_len = write_len
                    .checked_add(records[end].bytes.len())
                    .ok_or_else(|| WriteFailure::new(Error::Invalid, "assemble segment write"))?;
                end += 1;
            }
            let mut bytes = Vec::new();
            bytes
                .try_reserve_exact(write_len)
                .map_err(|_| WriteFailure::new(Error::NoMemory, "assemble segment write"))?;
            if let Some(header) = &records[index].new_segment {
                let encoded = encode_segment_header(id, header)
                    .map_err(|error| WriteFailure::new(error, "encode segment header"))?;
                bytes.extend_from_slice(&encoded);
            }
            let mut expected = if create {
                SEGMENT_HEADER_SIZE
            } else {
                start
            };
            while index < end {
                let record = &records[index];
                if physical_offset(record.lsn) != expected {
                    return Err(WriteFailure::new(Error::Invalid, "assemble segment write"));
                }
                bytes.extend_from_slice(&record.bytes);
                expected = expected
                    .checked_add(record.bytes.len() as u64)
                    .ok_or_else(|| WriteFailure::new(Error::Invalid, "assemble segment write"))?;
                index += 1;
            }
            if create {
                *append_file = Some(open_append_file_with_failure(
                    id,
                    directory.join(STAGED_SEGMENT_FILE),
                    true,
                    Vec::new(),
                )?);
            }
            let file = append_file
                .as_mut()
                .filter(|file| file.id == id)
                .ok_or_else(|| WriteFailure::new(Error::Io, "get append file"))?;
            file.write(start, &bytes)?;
            if create {
                file.publish(directory)?;
            }
        }
        Ok(())
    }

    fn read_record(&self, lsn: u64) -> Result<(RecordMeta, Vec<u8>)> {
        let id = block_id(lsn);
        let segment = self.segments.get(&id).ok_or(Error::OutLower)?;
        let file = File::open(segment_path(&self.directory, id))?;
        read_record_from(
            &file,
            segment.length,
            lsn,
            self.end_lsn.min((id + 1) * BLOCK_SIZE),
        )
    }

    fn skip_zero_padding(&self, lsn: u64) -> Result<u64> {
        let id = block_id(lsn);
        let segment = self.segments.get(&id).ok_or(Error::Corrupt)?;
        let remaining = BLOCK_SIZE - block_offset(lsn);
        if physical_offset(lsn) + remaining > segment.length {
            return Err(Error::Corrupt);
        }
        let file = File::open(segment_path(&self.directory, id))?;
        let mut padding = zeroed_bytes(remaining as usize)?;
        file_io::read_exact_at(&file, &mut padding, physical_offset(lsn))?;
        if padding.iter().any(|byte| *byte != 0) {
            return Err(Error::Corrupt);
        }
        Ok(lsn + remaining)
    }

    fn find_record(&self, lsn: u64) -> Result<(RecordMeta, Vec<u8>)> {
        if lsn < self.manifest.base_lsn {
            return Err(Error::OutLower);
        }
        let id = block_id(lsn);
        let Some(segment) = self.segments.get(&id) else {
            return Err(if self.last_record.is_none() {
                Error::OutLower
            } else {
                Error::OutUpper
            });
        };
        let mut scanner = SegmentScanner::new(
            &self.directory,
            id,
            segment,
            self.end_lsn.min((id + 1) * BLOCK_SIZE),
        )?;
        while let Some((record, bytes)) = scanner.next()? {
            if lsn < record.end_lsn() {
                return Ok((record, bytes));
            }
        }
        Err(Error::OutUpper)
    }

    fn previous_info(&self, lsn: u64) -> Result<BaseInfo> {
        if lsn == self.manifest.base_lsn {
            return Ok(self.manifest.previous);
        }
        if lsn == self.end_lsn {
            if let Some(record) = &self.last_record {
                return Ok(base_after_record(lsn, record));
            }
        }
        let id = block_id(lsn);
        let segment = self.segments.get(&id).ok_or(Error::Corrupt)?;
        let base = segment.base_info();
        if block_offset(lsn) == 0 {
            return Ok(base);
        }
        let mut previous = base;
        let mut scanner = SegmentScanner::new(
            &self.directory,
            id,
            segment,
            self.end_lsn.min((id + 1) * BLOCK_SIZE),
        )?;
        while let Some((record, _)) = scanner.next()? {
            if record.lsn >= lsn {
                break;
            }
            previous = base_after_record(lsn, &record);
        }
        Ok(previous)
    }
}

fn base_after_record(curr_lsn: u64, record: &RecordMeta) -> BaseInfo {
    BaseInfo {
        curr_lsn,
        prev_lsn: record.lsn,
        prev_scn: record.header.max_scn,
        prev_log_id: record.header.log_id,
        prev_accumulated_checksum: record.header.accumulated_checksum,
    }
}

fn read_record_from(
    file: &File,
    segment_length: u64,
    lsn: u64,
    limit: u64,
) -> Result<(RecordMeta, Vec<u8>)> {
    if physical_offset(lsn) + GROUP_HEADER_SIZE as u64 > segment_length {
        return Err(Error::OutUpper);
    }
    let mut header_bytes = zeroed_bytes(GROUP_HEADER_SIZE)?;
    file_io::read_exact_at(file, &mut header_bytes, physical_offset(lsn))?;
    if header_bytes.iter().all(|byte| *byte == 0) {
        return Err(Error::OutUpper);
    }
    let header = decode_group_header(&header_bytes)?;
    let record_len = GROUP_HEADER_SIZE as u64 + header.data_len as u64;
    let record_end = lsn.checked_add(record_len).ok_or(Error::Corrupt)?;
    if block_offset(lsn) + record_len > BLOCK_SIZE
        || record_end > limit
        || physical_offset(lsn) + record_len > segment_length
    {
        return Err(Error::Corrupt);
    }
    let mut bytes = zeroed_bytes(record_len as usize)?;
    bytes[..GROUP_HEADER_SIZE].copy_from_slice(&header_bytes);
    file_io::read_exact_at(
        file,
        &mut bytes[GROUP_HEADER_SIZE..],
        physical_offset(lsn) + GROUP_HEADER_SIZE as u64,
    )?;
    let header = validate_group(&bytes, lsn)?;
    Ok((
        RecordMeta {
            lsn,
            len: record_len,
            header,
        },
        bytes,
    ))
}

struct SegmentScanner {
    file: File,
    segment_length: u64,
    limit: u64,
    lsn: u64,
    end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
    last_record: Option<RecordMeta>,
}

impl SegmentScanner {
    fn new(directory: &Path, id: u64, segment: &SegmentMeta, limit: u64) -> Result<Self> {
        let start = segment.header.start_lsn;
        if block_id(start) != id || limit < start || limit > (id + 1) * BLOCK_SIZE {
            return Err(Error::Corrupt);
        }
        Ok(Self {
            file: File::open(segment_path(directory, id))?,
            segment_length: segment.length,
            limit,
            lsn: start,
            end_scn: segment.header.prev_scn,
            next_log_id: segment
                .header
                .prev_log_id
                .checked_add(1)
                .ok_or(Error::Corrupt)?,
            accumulated_checksum: segment.header.prev_accumulated_checksum,
            last_record: None,
        })
    }

    fn next(&mut self) -> Result<Option<(RecordMeta, Vec<u8>)>> {
        loop {
            if self.lsn == self.limit {
                return Ok(None);
            }
            if self.lsn > self.limit {
                return Err(Error::Corrupt);
            }
            let remaining = BLOCK_SIZE - block_offset(self.lsn);
            if remaining < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64 {
                if self.lsn + remaining != self.limit
                    || physical_offset(self.lsn) + remaining > self.segment_length
                {
                    return Err(Error::Corrupt);
                }
                let mut padding = zeroed_bytes(remaining as usize)?;
                file_io::read_exact_at(&self.file, &mut padding, physical_offset(self.lsn))?;
                if padding.iter().any(|byte| *byte != 0) {
                    return Err(Error::Corrupt);
                }
                self.lsn += remaining;
                continue;
            }
            let (record, bytes) = read_record_from(&self.file, self.segment_length, self.lsn, self.limit)?;
            let header = &record.header;
            if header.committed_end_lsn != self.lsn
                || header.log_id != self.next_log_id
                || header.accumulated_checksum
                    != crc64_with_seed(
                        self.accumulated_checksum,
                        &header.data_checksum.to_le_bytes(),
                    )
            {
                return Err(Error::Corrupt);
            }
            self.lsn = record.end_lsn();
            self.end_scn = header.max_scn;
            self.next_log_id = header.log_id.checked_add(1).ok_or(Error::Corrupt)?;
            self.accumulated_checksum = header.accumulated_checksum;
            self.last_record = Some(record.clone());
            return Ok(Some((record, bytes)));
        }
    }
}

fn tail_can_be_truncated(scanner: &SegmentScanner) -> Result<bool> {
    let file = scanner.file.try_clone()?;
    let mut offset = physical_offset(scanner.lsn);
    let mut saw_zero = false;
    let mut bytes = [0; 16384];
    while offset < scanner.segment_length {
        let count = bytes.len().min((scanner.segment_length - offset) as usize);
        file_io::read_exact_at(&file, &mut bytes[..count], offset)?;
        for byte in &bytes[..count] {
            if saw_zero && *byte != 0 {
                return Ok(false);
            }
            saw_zero |= *byte == 0;
        }
        offset += count as u64;
    }
    Ok(saw_zero)
}

fn create_state(directory: &Path, base: BaseInfo, ref_scn: u64) -> Result<State> {
    if base.curr_lsn < base.prev_lsn || ref_scn == u64::MAX {
        return Err(Error::Invalid);
    }
    if !directory.try_exists()? {
        return Err(Error::NotFound);
    }
    if !directory_is_empty(directory)? {
        return Err(Error::Duplicate);
    }
    let manifest = Manifest {
        base_lsn: base.curr_lsn,
        previous: base,
        ref_scn,
    };
    let manifest_writer = ManifestWriter::create(directory, &manifest)?;
    sync_directory(directory)?;
    Ok(State {
        directory: directory.to_path_buf(),
        manifest_writer: Some(manifest_writer),
        append_file: None,
        end_lsn: base.curr_lsn,
        end_scn: base.prev_scn,
        reserved_end_lsn: base.curr_lsn,
        reserved_end_scn: base.prev_scn,
        next_log_id: base.prev_log_id.checked_add(1).ok_or(Error::Invalid)?,
        accumulated_checksum: base.prev_accumulated_checksum,
        manifest,
        segments: BTreeMap::new(),
        last_record: None,
        integrity: true,
        closed: false,
        io_in_progress: false,
        unflushed_appends: 0,
        failed: None,
        #[cfg(test)]
        fail_append_plan: false,
    })
}

fn open_state(
    directory: &Path,
    read_segment_header: impl Fn(&File, &mut [u8], u64) -> std::io::Result<()>,
) -> Result<State> {
    let (manifest, manifest_writer) = ManifestWriter::open(directory)?;
    let mut ids = Vec::new();
    for item in fs::read_dir(directory)? {
        let item = item?;
        let name = item.file_name();
        let name = name.to_string_lossy();
        if let Some(hex) = name.strip_suffix(".log") {
            if hex.len() == 16 {
                if let Ok(id) = u64::from_str_radix(hex, 16) {
                    ids.push(id);
                }
            }
        }
    }
    ids.sort_unstable();
    let first_required = block_id(manifest.base_lsn);
    let (obsolete, retained) = ids.split_at(ids.partition_point(|id| *id < first_required));
    reclaim_obsolete_segments(directory, &manifest_writer.file, obsolete);
    let mut segments = BTreeMap::new();
    for id in retained.iter().copied() {
        if id != first_required + segments.len() as u64 {
            return Err(Error::Corrupt);
        }
        let path = segment_path(directory, id);
        let file = File::open(&path)?;
        let length = file.metadata()?.len();
        if !(SEGMENT_HEADER_SIZE..=PHYSICAL_BLOCK_SIZE).contains(&length) {
            return Err(Error::Corrupt);
        }
        let mut segment_header = zeroed_bytes(SEGMENT_HEADER_SIZE as usize)?;
        read_segment_header(&file, &mut segment_header, 0)?;
        let header = validate_segment_header(&segment_header, id)?;
        if block_id(header.start_lsn) != id
            || header.start_lsn < manifest.base_lsn
            || header.first_scn < header.prev_scn
            || (id != first_required && block_offset(header.start_lsn) != 0)
        {
            return Err(Error::Corrupt);
        }
        segments.insert(id, SegmentMeta { length, header });
    }
    if let Some(segment) = segments.get(&first_required) {
        if segment.base_info() != manifest.previous {
            return Err(Error::Corrupt);
        }
    }
    let recovered = if let Some((&id, segment)) = segments.last_key_value() {
        let mut scanner = SegmentScanner::new(directory, id, segment, (id + 1) * BLOCK_SIZE)?;
        let mut first = true;
        loop {
            match scanner.next() {
                Ok(Some((record, _))) => {
                    if first && record.header.max_scn != segment.header.first_scn {
                        return Err(Error::Corrupt);
                    }
                    first = false;
                }
                Ok(None) | Err(Error::OutUpper) => break,
                Err(Error::Corrupt | Error::UnknownVersion) if tail_can_be_truncated(&scanner)? => {
                    break;
                }
                Err(error) => return Err(error),
            }
        }
        Some(scanner)
    } else {
        None
    };
    match fs::remove_file(directory.join(STAGED_SEGMENT_FILE)) {
        Ok(()) => sync_directory(directory)?,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => (),
        Err(error) => return Err(error.into()),
    }
    let mut append_file = None;
    let (end_lsn, end_scn, next_log_id, accumulated_checksum, last_record) =
        if let Some(scanner) = recovered {
            let id = block_id(scanner.last_record.as_ref().ok_or(Error::Corrupt)?.lsn);
            let end_lsn = scanner.lsn;
            let valid_length = if block_id(end_lsn) == id {
                physical_offset(end_lsn)
            } else {
                PHYSICAL_BLOCK_SIZE
            };
            let path = segment_path(directory, id);
            let tail = read_append_tail(&path, valid_length)?;
            let file = open_append_file(id, &path, false, tail)?;
            file.file.set_len(valid_length)?;
            file_io::extend_file(&file.file, PHYSICAL_BLOCK_SIZE)?;
            append_file = Some(file);
            if let Some(segment) = segments.get_mut(&id) {
                segment.length = valid_length;
            }
            (
                end_lsn,
                scanner.end_scn,
                scanner.next_log_id,
                scanner.accumulated_checksum,
                scanner.last_record,
            )
        } else {
            (
                manifest.base_lsn,
                manifest.previous.prev_scn,
                manifest.previous.prev_log_id.checked_add(1).ok_or(Error::Corrupt)?,
                manifest.previous.prev_accumulated_checksum,
                None,
            )
        };
    Ok(State {
        directory: directory.to_path_buf(),
        manifest_writer: Some(manifest_writer),
        append_file,
        manifest,
        segments,
        last_record,
        end_lsn,
        end_scn,
        reserved_end_lsn: end_lsn,
        reserved_end_scn: end_scn,
        next_log_id,
        accumulated_checksum,
        integrity: true,
        closed: false,
        io_in_progress: false,
        unflushed_appends: 0,
        failed: None,
        #[cfg(test)]
        fail_append_plan: false,
    })
}

fn reclaim_obsolete_segments(directory: &Path, manifest: &File, obsolete: &[u64]) {
    if obsolete.is_empty() {
        return;
    }
    // A valid slot from the previous process may still be dirty in the page cache.
    if let Err(error) = manifest.sync_data() {
        log::warn!(
            target: "WAL",
            "failed to sync the manifest before removing obsolete segments in {}: {error}",
            directory.display()
        );
        return;
    }
    let mut removed = false;
    for &id in obsolete {
        let path = segment_path(directory, id);
        match fs::remove_file(&path) {
            Ok(()) => removed = true,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => (),
            Err(error) => log::warn!(
                target: "WAL",
                "failed to remove obsolete segment {}: {error}",
                path.display()
            ),
        }
    }
    if removed {
        if let Err(error) = sync_directory_io(directory) {
            log::warn!(
                target: "WAL",
                "failed to sync obsolete segment removal in {}: {error}",
                directory.display()
            );
        }
    }
}

impl ManifestWriter {
    fn create(directory: &Path, manifest: &Manifest) -> Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(directory.join(MANIFEST_FILE))?;
        file.set_len(MANIFEST_FILE_SIZE as u64)?;
        let mut writer = Self {
            file,
            generation: 0,
            #[cfg(test)]
            fail_writes: false,
        };
        writer.write(manifest)?;
        Ok(writer)
    }

    fn open(directory: &Path) -> Result<(Manifest, Self)> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(directory.join(MANIFEST_FILE))?;
        if file.metadata()?.len() != MANIFEST_FILE_SIZE as u64 {
            return Err(Error::Corrupt);
        }
        let mut bytes = [0; MANIFEST_FILE_SIZE];
        file_io::read_exact_at(&file, &mut bytes, 0)?;
        let first = Self::decode_slot(&bytes[..MANIFEST_SIZE]);
        let second = Self::decode_slot(&bytes[MANIFEST_SIZE..]);
        let (generation, manifest) = match (first, second) {
            (Some(first), Some(second)) => {
                if first.0 >= second.0 {
                    first
                } else {
                    second
                }
            }
            (Some(slot), None) | (None, Some(slot)) => slot,
            (None, None) => return Err(Error::Corrupt),
        };
        Ok((
            manifest,
            Self {
                file,
                generation,
                #[cfg(test)]
                fail_writes: false,
            },
        ))
    }

    fn write(&mut self, manifest: &Manifest) -> Result<()> {
        #[cfg(test)]
        if self.fail_writes {
            return Err(Error::Io);
        }
        let generation = self.generation.checked_add(1).ok_or(Error::Invalid)?;
        let bytes = Self::encode_slot(manifest, generation);
        let slot = self.generation % 2;
        file_io::write_all_at(&self.file, &bytes, slot * MANIFEST_SIZE as u64)?;
        self.file.sync_data()?;
        self.generation = generation;
        Ok(())
    }

    fn encode_slot(manifest: &Manifest, generation: u64) -> [u8; MANIFEST_SIZE] {
        let mut bytes = [0; MANIFEST_SIZE];
        bytes[..8].copy_from_slice(MANIFEST_MAGIC);
        bytes[8..16].copy_from_slice(&manifest.base_lsn.to_le_bytes());
        bytes[16..24].copy_from_slice(&manifest.previous.prev_lsn.to_le_bytes());
        bytes[24..32].copy_from_slice(&manifest.previous.prev_scn.to_le_bytes());
        bytes[32..40].copy_from_slice(&manifest.previous.prev_log_id.to_le_bytes());
        bytes[40..48].copy_from_slice(&manifest.previous.prev_accumulated_checksum.to_le_bytes());
        bytes[48..56].copy_from_slice(&manifest.ref_scn.to_le_bytes());
        bytes[88..96].copy_from_slice(&generation.to_le_bytes());
        let checksum = crc64(&bytes[..96]);
        bytes[96..104].copy_from_slice(&checksum.to_le_bytes());
        bytes
    }

    fn decode_slot(bytes: &[u8]) -> Option<(u64, Manifest)> {
        if bytes.len() != MANIFEST_SIZE || bytes.get(..8) != Some(MANIFEST_MAGIC) {
            return None;
        }
        let generation = u64::from_le_bytes(bytes[88..96].try_into().unwrap());
        let checksum = u64::from_le_bytes(bytes[96..104].try_into().unwrap());
        if crc64(&bytes[..96]) != checksum {
            return None;
        }
        let number =
            |offset: usize| u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap());
        let signed =
            |offset: usize| i64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap());
        let base_lsn = number(8);
        let previous = BaseInfo {
            curr_lsn: base_lsn,
            prev_lsn: number(16),
            prev_scn: number(24),
            prev_log_id: signed(32),
            prev_accumulated_checksum: number(40),
        };
        Some((
            generation,
            Manifest {
                base_lsn,
                previous,
                ref_scn: number(48),
            },
        ))
    }
}

fn read_append_tail(path: &Path, valid_length: u64) -> Result<Vec<u8>> {
    let tail_len = valid_length as usize % DIO_ALIGNMENT;
    if tail_len == 0 {
        return Ok(Vec::new());
    }
    let file = OpenOptions::new().read(true).open(path)?;
    let mut tail = zeroed_bytes(tail_len)?;
    file_io::read_exact_at(&file, &mut tail, valid_length - tail_len as u64)?;
    Ok(tail)
}

fn open_append_file(id: u64, path: &Path, create: bool, tail: Vec<u8>) -> Result<AppendFile> {
    open_append_file_with_failure(id, path.to_path_buf(), create, tail).map_err(|failure| failure.code)
}

fn open_append_file_with_failure(
    id: u64,
    path: PathBuf,
    create: bool,
    tail: Vec<u8>,
) -> std::result::Result<AppendFile, WriteFailure> {
    let mut options = OpenOptions::new();
    options
        .create_new(create)
        .read(true)
        .write(true);
    let file = file_io::open_data_synchronous_read_write_prefer_direct(&mut options, &path)
        .map_err(|error| WriteFailure::from_io(error, &path, "open segment"))?;
    let file = AppendFile { id, path, file, tail };
    if create {
        file_io::extend_file(&file.file, PHYSICAL_BLOCK_SIZE)
            .map_err(|error| WriteFailure::from_io(error, &file.path, "allocate segment"))?;
    }
    Ok(file)
}

impl AppendFile {
    fn publish(&mut self, directory: &Path) -> std::result::Result<(), WriteFailure> {
        self.file
            .sync_all()
            .map_err(|error| WriteFailure::from_io(error, &self.path, "fsync segment"))?;
        let path = segment_path(directory, self.id);
        match fs::symlink_metadata(&path) {
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => (),
            Err(error) => return Err(WriteFailure::from_io(error, &path, "check segment path")),
            Ok(_) => {
                return Err(WriteFailure::from_io(
                    std::io::ErrorKind::AlreadyExists.into(),
                    &path,
                    "publish segment",
                ));
            }
        }
        file_io::publish_file(&self.path, &path)
            .map_err(|error| WriteFailure::from_io(error, &path, "publish segment"))?;
        self.path = path;
        sync_directory_io(directory)
            .map_err(|error| WriteFailure::from_io(error, &self.path, "fsync directory"))
    }

    fn write(
        &mut self,
        start: u64,
        bytes: &[u8],
    ) -> std::result::Result<(), WriteFailure> {
        let prefix_len = self.tail.len();
        let aligned_start = start
            .checked_sub(prefix_len as u64)
            .filter(|offset| *offset % DIO_ALIGNMENT as u64 == 0)
            .ok_or_else(|| WriteFailure::new(Error::Invalid, "align segment write"))?;
        let logical_len = prefix_len
            .checked_add(bytes.len())
            .ok_or_else(|| WriteFailure::new(Error::Invalid, "align segment write"))?;
        let write_len = logical_len
            .checked_add(DIO_ALIGNMENT - 1)
            .map(|len| len / DIO_ALIGNMENT * DIO_ALIGNMENT)
            .ok_or_else(|| WriteFailure::new(Error::Invalid, "align segment write"))?;
        let mut buffer = AlignedBuffer::zeroed(write_len, DIO_ALIGNMENT)
            .map_err(|_| WriteFailure::new(Error::NoMemory, "allocate aligned WAL buffer"))?;
        let output = buffer.as_mut_slice();
        output[..prefix_len].copy_from_slice(&self.tail);
        output[prefix_len..logical_len].copy_from_slice(bytes);
        output[logical_len..].fill(0);
        file_io::write_all_data_synchronous_at(&self.file, output, aligned_start)
            .map_err(|error| WriteFailure::from_io(error, &self.path, "write segment"))?;
        let tail_len = logical_len % DIO_ALIGNMENT;
        let mut tail = Vec::new();
        tail.try_reserve_exact(tail_len)
            .map_err(|_| WriteFailure::new(Error::NoMemory, "save segment tail"))?;
        tail.extend_from_slice(&output[logical_len - tail_len..logical_len]);
        self.tail = tail;
        Ok(())
    }
}

pub(crate) fn sync_directory(directory: &Path) -> Result<()> {
    Ok(sync_directory_io(directory)?)
}

#[cfg(unix)]
fn sync_directory_io(directory: &Path) -> std::io::Result<()> {
    file_io::Directory::open(directory)?.sync()
}

pub(crate) fn directory_is_empty(directory: &Path) -> Result<bool> {
    Ok(fs::read_dir(directory)?.next().transpose()?.is_none())
}

#[cfg(not(unix))]
fn sync_directory_io(_directory: &Path) -> std::io::Result<()> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Seek, SeekFrom, Write};
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(0);

    struct Directory(PathBuf);

    impl Directory {
        fn new() -> Self {
            let sequence = NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed);
            let path =
                std::env::temp_dir().join(format!("seekdb-wal-{}-{sequence}", std::process::id()));
            let _ = fs::remove_dir_all(&path);
            fs::create_dir(&path).unwrap();
            Self(path)
        }
    }

    impl Drop for Directory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn base() -> BaseInfo {
        BaseInfo {
            curr_lsn: 0,
            prev_lsn: 0,
            prev_scn: 0,
            prev_log_id: 0,
            prev_accumulated_checksum: 0,
        }
    }

    fn import(stream: &Stream, lsn: u64, scn: u64, bytes: &[u8]) -> Result<AppendResult> {
        stream.append_imported(&[ImportedGroup {
            source_lsn: lsn,
            source_scn: scn,
            zero_padding_before: 0,
            bytes,
        }])
    }

    #[test]
    fn creation_requires_an_existing_directory() {
        let directory = Directory::new();
        fs::remove_dir(&directory.0).unwrap();
        let path = directory.0.to_string_lossy().into_owned();

        assert_eq!(Stream::create(&path, base(), 0).status(), Err(Error::NotFound));
        assert!(!directory.0.exists());
    }

    #[test]
    fn acknowledged_records_survive_reopen_and_torn_tail_is_removed() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let first = stream.append(b"one", 10).unwrap();
        let second = stream.append(b"two", 11).unwrap();
        assert_eq!(first.lsn, GROUP_HEADER_SIZE as u64);
        assert_eq!(second.lsn, first.end_lsn + GROUP_HEADER_SIZE as u64);
        drop(stream);
        let segment = segment_path(&directory.0, 0);
        let mut file = OpenOptions::new().write(true).open(&segment).unwrap();
        file.seek(SeekFrom::Start(physical_offset(second.end_lsn)))
            .unwrap();
        file.write_all(b"torn").unwrap();
        let reopened = Stream::open(&path);
        assert!(reopened.status().unwrap());
        let progress = reopened.progress().unwrap();
        assert_eq!(progress.end_lsn, second.end_lsn);
        assert!(progress.integrity);
        assert_eq!(progress.used_disk, physical_offset(second.end_lsn));
        let mut cursor = reopened.cursor(first.lsn, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"one");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"two");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn complete_tail_group_survives_reopen() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let confirmed = stream.append(b"confirmed", 10).unwrap();
        let complete_end_lsn;
        {
            let mut state = stream.inner.state.lock().unwrap();
            let lsn = state.end_lsn;
            let entry = encode_entry(b"unconfirmed", 11, false).unwrap();
            let group = encode_group(
                &entry,
                lsn,
                11,
                state.next_log_id,
                state.accumulated_checksum,
                false,
            )
            .unwrap();
            let mut file = OpenOptions::new()
                .write(true)
                .open(segment_path(&state.directory, block_id(lsn)))
                .unwrap();
            file.seek(SeekFrom::Start(physical_offset(lsn))).unwrap();
            file.write_all(&group).unwrap();
            file.sync_data().unwrap();
            complete_end_lsn = lsn + group.len() as u64;
            let length = physical_offset(lsn) + group.len() as u64;
            state.segments.get_mut(&block_id(lsn)).unwrap().length = length;
        }
        drop(stream);

        let reopened = Stream::open(&path);
        assert_eq!(
            reopened.progress().unwrap().end_lsn,
            complete_end_lsn
        );
        reopened.append(b"next", 12).unwrap();
        let mut cursor = reopened.cursor(confirmed.lsn, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"confirmed");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"unconfirmed");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"next");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn incomplete_staged_segment_is_removed_on_reopen() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let confirmed = stream.append(b"confirmed", 10).unwrap();
        drop(stream);
        let staged = directory.0.join(STAGED_SEGMENT_FILE);
        fs::write(&staged, b"torn segment header").unwrap();

        let reopened = Stream::open(&path);
        assert_eq!(reopened.status(), Ok(true));
        assert_eq!(reopened.progress().unwrap().end_lsn, confirmed.end_lsn);
        assert!(!staged.exists());
        let mut cursor = reopened.cursor(confirmed.lsn, false);
        assert_eq!(cursor.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(cursor.payload(), b"confirmed");
    }

    #[test]
    fn segment_header_read_failure_preserves_confirmed_segment() {
        let directory = Directory::new();
        let path = directory.0.to_str().unwrap();
        let stream = Stream::create(path, base(), 0);
        let confirmed = stream.append(b"confirmed", 10).unwrap();
        drop(stream);

        let segment = segment_path(&directory.0, 0);
        let original = fs::read(&segment).unwrap();
        let failed = Stream::from_result(open_state(&directory.0, |_, _, _| {
            Err(std::io::Error::other("injected segment header read failure"))
        }));
        assert_eq!((failed.status(), segment.exists()), (Err(Error::Io), true));
        assert_eq!(fs::read(segment).unwrap(), original);

        let reopened = Stream::open(path);
        assert_eq!(reopened.progress().unwrap().end_lsn, confirmed.end_lsn);
        let mut cursor = reopened.cursor(confirmed.lsn, false);
        assert_eq!(cursor.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(cursor.payload(), b"confirmed");
    }

    #[test]
    fn segment_header_corruption_preserves_published_segment() {
        let directory = Directory::new();
        let path = directory.0.to_str().unwrap();
        let stream = Stream::create(path, base(), 0);
        stream.append(b"confirmed", 10).unwrap();
        drop(stream);

        let segment = segment_path(&directory.0, 0);
        let mut original = fs::read(&segment).unwrap();
        original[0] ^= 1;
        fs::write(&segment, &original).unwrap();

        assert_eq!(Stream::open(path).status(), Err(Error::Corrupt));
        assert_eq!(fs::read(segment).unwrap(), original);
    }

    #[test]
    fn segment_publication_preserves_existing_file() {
        let directory = Directory::new();
        let path = directory.0.to_str().unwrap();
        let stream = Stream::create(path, base(), 0);
        let published = segment_path(&directory.0, 0);
        fs::write(&published, b"existing segment").unwrap();

        assert_eq!(stream.append(b"new record", 10), Err(Error::Io));
        assert_eq!(fs::read(published).unwrap(), b"existing segment");
    }

    #[test]
    fn confirmed_corruption_is_not_truncated() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        stream.append(b"one", 10).unwrap();
        stream.append(b"two", 11).unwrap();
        drop(stream);
        let segment = segment_path(&directory.0, 0);
        let original_length = fs::metadata(&segment).unwrap().len();
        let mut file = OpenOptions::new().write(true).open(&segment).unwrap();
        file.seek(SeekFrom::Start(
            physical_offset(0) + (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64,
        ))
        .unwrap();
        file.write_all(b"X").unwrap();
        file.sync_all().unwrap();
        drop(file);

        assert_eq!(Stream::open(&path).status(), Err(Error::Corrupt));
        assert_eq!(fs::metadata(segment).unwrap().len(), original_length);
        assert_eq!(
            fs::metadata(segment_path(&directory.0, 0)).unwrap().len(),
            original_length
        );
    }

    #[test]
    fn recovery_validates_the_tail_without_scanning_old_segment_data() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let large = vec![7; 1024 * 1024];
        for index in 0..70 {
            stream.append(&large, index + 1).unwrap();
        }
        let tail = stream.append(b"tail", 100).unwrap();
        let expected_scn_position = stream.locate_by_scn(65).unwrap();
        drop(stream);

        let mut file = OpenOptions::new()
            .write(true)
            .open(segment_path(&directory.0, 0))
            .unwrap();
        file.seek(SeekFrom::Start(
            physical_offset(0) + (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64,
        ))
        .unwrap();
        file.write_all(b"X").unwrap();
        file.sync_all().unwrap();
        drop(file);

        let reopened = Stream::open(&path);
        assert_eq!(reopened.status(), Ok(true));
        assert_eq!(reopened.locate_by_scn(65), Ok(expected_scn_position));
        assert_eq!(reopened.locate_by_lsn(tail.lsn), Ok(100));
        let mut old = reopened.cursor(0, true);
        assert_eq!(old.next(u64::MAX), Err(Error::Corrupt));
        let mut tail_cursor = reopened.cursor(block_id(tail.lsn) * BLOCK_SIZE, true);
        while matches!(tail_cursor.next(u64::MAX), Ok(Next::Entry)) {}
        assert_eq!(tail_cursor.current_lsn, tail.lsn - GROUP_HEADER_SIZE as u64);
    }

    #[test]
    fn committed_short_zero_padding_survives_reopen() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let payload_len = BLOCK_SIZE as usize - GROUP_HEADER_SIZE - ENTRY_HEADER_SIZE - 32;
        let entry = encode_entry(&vec![7; payload_len], 10, false).unwrap();
        let group = encode_group(&entry, 0, 10, 1, 0, false).unwrap();
        import(&stream, 0, 10, &group).unwrap();
        let appended = stream.append(b"next", 11).unwrap();
        assert_eq!(block_id(appended.lsn), 1);
        let mut cursor = stream.cursor(0, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), vec![7; payload_len]);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"next");
        drop(stream);

        let reopened = Stream::open(&path);
        assert_eq!(reopened.status(), Ok(true));
        let mut cursor = reopened.cursor(0, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), vec![7; payload_len]);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"next");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn standby_imports_short_zero_padding_on_the_following_group() {
        let source_directory = Directory::new();
        let source_path = source_directory.0.to_string_lossy();
        let source = Stream::create(&source_path, base(), 0);
        let group_overhead = GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE;
        let padding = 32;
        let mut payload_remaining = BLOCK_SIZE as usize - 5 * group_overhead - padding;
        let payload = vec![7; MAX_LOG_BODY_SIZE];
        for scn in 10..15 {
            let length = payload_remaining.min(MAX_LOG_BODY_SIZE);
            source.append(&payload[..length], scn).unwrap();
            payload_remaining -= length;
        }
        source.append(b"next", 15).unwrap();
        source.append(b"after", 16).unwrap();

        let target_directory = Directory::new();
        let target_path = target_directory.0.to_string_lossy();
        let target = Stream::create(&target_path, base(), 0);
        let mut source_cursor = source.cursor(0, true);
        for _ in 0..5 {
            assert_eq!(source_cursor.next(u64::MAX), Ok(Next::Entry));
            assert_eq!(source_cursor.zero_padding_before(), 0);
            import(
                &target,
                source_cursor.lsn(),
                source_cursor.scn(),
                source_cursor.bytes(),
            )
            .unwrap();
        }

        let resume_lsn = target.progress().unwrap().end_lsn;
        assert_eq!(resume_lsn, BLOCK_SIZE - padding as u64);
        let mut resumed = source.cursor(resume_lsn, true);
        assert_eq!(resumed.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(resumed.lsn(), BLOCK_SIZE);
        assert_eq!(resumed.zero_padding_before(), padding as u32);
        assert_eq!(source_cursor.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(
            source_cursor.zero_padding_before(),
            resumed.zero_padding_before()
        );

        let mut group = ImportedGroup {
            source_lsn: resumed.lsn(),
            source_scn: resumed.scn(),
            zero_padding_before: 0,
            bytes: resumed.bytes(),
        };
        for invalid_padding in [0, padding as u32 - 1, group_overhead as u32] {
            group.zero_padding_before = invalid_padding;
            assert_eq!(
                target.append_imported(std::slice::from_ref(&group)),
                Err(Error::Invalid)
            );
        }
        group.zero_padding_before = padding as u32 + 1;
        group.source_lsn += 1;
        assert_eq!(
            target.append_imported(std::slice::from_ref(&group)),
            Err(Error::Invalid)
        );
        assert_eq!(target.progress().unwrap().end_lsn, resume_lsn);

        group.source_lsn = resumed.lsn();
        group.zero_padding_before = resumed.zero_padding_before();
        target.append_imported(&[group]).unwrap();
        assert_eq!(resumed.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(resumed.zero_padding_before(), 0);
        import(&target, resumed.lsn(), resumed.scn(), resumed.bytes()).unwrap();
        assert_eq!(
            target.progress().unwrap().end_lsn,
            source.progress().unwrap().end_lsn
        );
        drop(target);

        let reopened = Stream::open(&target_path);
        let mut cursor = reopened.cursor(resume_lsn, true);
        assert_eq!(cursor.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(cursor.lsn(), BLOCK_SIZE);
        assert_eq!(cursor.zero_padding_before(), padding as u32);
        assert_eq!(cursor.next(u64::MAX), Ok(Next::Entry));
        assert_eq!(cursor.zero_padding_before(), 0);
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn checkpoint_is_persistent_and_old_segments_disappear() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let large = vec![7; 1024 * 1024];
        for index in 0..70 {
            stream.append(&large, index + 1).unwrap();
        }
        assert_eq!(
            stream.progress().unwrap().unrecyclable_disk,
            stream.progress().unwrap().end_lsn
        );
        let checkpoint = BLOCK_SIZE;
        let expected_base = stream.base_info(checkpoint).unwrap();
        let obsolete = segment_path(&directory.0, 0);
        let saved = directory.0.join("obsolete-segment.saved");
        fs::copy(&obsolete, &saved).unwrap();
        let progress = stream.set_base_lsn(checkpoint).unwrap();
        assert_eq!(progress.unrecyclable_disk, progress.end_lsn - checkpoint);
        assert!(!obsolete.exists());
        let expected_begin_scn = stream.progress().unwrap().begin_scn;
        drop(stream);
        fs::rename(saved, &obsolete).unwrap();
        let reopened = Stream::open(&path);
        assert_eq!(reopened.progress().unwrap().base_lsn, checkpoint);
        assert_eq!(reopened.base_info(checkpoint).unwrap(), expected_base);
        assert_eq!(reopened.progress().unwrap().begin_scn, expected_begin_scn);
        assert!(!obsolete.exists());
    }

    #[test]
    fn checkpoint_at_confirmed_tail_releases_segment_before_next_append() {
        const MAX_GROUP_LEN: u64 =
            (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE + MAX_LOG_BODY_SIZE) as u64;

        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        while stream.progress().unwrap().end_lsn < BLOCK_SIZE {
            let remaining = BLOCK_SIZE - stream.progress().unwrap().end_lsn;
            let group_len = if remaining > MAX_GROUP_LEN && remaining < 2 * MAX_GROUP_LEN {
                remaining / 2
            } else {
                remaining.min(MAX_GROUP_LEN)
            };
            let payload_len =
                (group_len - GROUP_HEADER_SIZE as u64 - ENTRY_HEADER_SIZE as u64) as usize;
            stream.append(&vec![7; payload_len], 10).unwrap();
        }
        assert_eq!(stream.progress().unwrap().end_lsn, BLOCK_SIZE);
        assert_eq!(
            stream
                .inner
                .state
                .lock()
                .unwrap()
                .append_file
                .as_ref()
                .map(|file| file.id),
            Some(0)
        );

        stream.set_base_lsn(BLOCK_SIZE).unwrap();
        assert!(stream.inner.state.lock().unwrap().append_file.is_none());
        assert!(!segment_path(&directory.0, 0).exists());

        let appended = stream.append(b"after-checkpoint", 11).unwrap();
        assert_eq!(block_id(appended.lsn), 1);
        drop(stream);

        let reopened = Stream::open(&path);
        assert_eq!(reopened.status(), Ok(true));
        assert_eq!(reopened.progress().unwrap().base_lsn, BLOCK_SIZE);
        assert_eq!(reopened.progress().unwrap().end_lsn, appended.end_lsn);
        let mut cursor = reopened.cursor(BLOCK_SIZE, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"after-checkpoint");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn checkpoint_failure_preserves_segments_and_empty_gc_is_idempotent() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let large = vec![7; 1024 * 1024];
        for index in 0..70 {
            stream.append(&large, index + 1).unwrap();
        }
        stream
            .inner
            .state
            .lock()
            .unwrap()
            .manifest_writer
            .as_mut()
            .unwrap()
            .fail_writes = true;
        assert!(matches!(stream.set_base_lsn(BLOCK_SIZE), Err(Error::Io)));
        assert_eq!(stream.progress().unwrap().base_lsn, 0);
        assert!(segment_path(&directory.0, 0).exists());
        stream
            .inner
            .state
            .lock()
            .unwrap()
            .manifest_writer
            .as_mut()
            .unwrap()
            .fail_writes = false;
        stream.set_base_lsn(BLOCK_SIZE).unwrap();
        assert!(!segment_path(&directory.0, 0).exists());
        assert_eq!(
            stream.set_base_lsn(BLOCK_SIZE).unwrap().base_lsn,
            BLOCK_SIZE
        );
        drop(stream);
        assert_eq!(
            Stream::open(&path).progress().unwrap().base_lsn,
            BLOCK_SIZE
        );
    }

    #[test]
    fn manifest_reopen_uses_latest_intact_generation() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        stream.append(b"one", 10).unwrap();
        let second = stream.append(b"two", 11).unwrap();
        drop(stream);

        let stream = Stream::open(&path);
        assert_eq!(stream.progress().unwrap().end_lsn, second.end_lsn);
        assert_eq!(
            stream
                .inner
                .state
                .lock()
                .unwrap()
                .manifest_writer
                .as_ref()
                .unwrap()
                .generation,
            1
        );
        drop(stream);

        let manifest_path = directory.0.join(MANIFEST_FILE);
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(manifest_path)
            .unwrap();
        file.seek(SeekFrom::Start((MANIFEST_SIZE + 88) as u64))
            .unwrap();
        file.write_all(&4u64.to_le_bytes()).unwrap();
        file.sync_all().unwrap();
        drop(file);

        let stream = Stream::open(&path);
        assert_eq!(stream.status(), Ok(true));
        assert_eq!(stream.progress().unwrap().end_lsn, second.end_lsn);
        let mut cursor = stream.cursor(0, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"one");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"two");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn cursor_owns_current_bytes_but_does_not_pin_old_segments() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let large = vec![7; 1024 * 1024];
        for index in 0..70 {
            stream.append(&large, index + 1).unwrap();
        }
        let mut cursor = stream.cursor(0, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), large);
        stream.set_base_lsn(BLOCK_SIZE).unwrap();
        assert!(!segment_path(&directory.0, 0).exists());
        assert_eq!(cursor.payload(), large);
        assert_eq!(cursor.next(u64::MAX), Err(Error::OutLower));
    }

    #[test]
    fn imported_groups_require_the_exact_next_position() {
        let source_directory = Directory::new();
        let source_path = source_directory.0.to_string_lossy().into_owned();
        let source = Stream::create(&source_path, base(), 0);
        source.append(b"one", 10).unwrap();
        let mut cursor = source.cursor(0, true);
        cursor.next(u64::MAX).unwrap();
        let bytes = cursor.bytes().to_vec();
        let target_directory = Directory::new();
        let target_path = target_directory.0.to_string_lossy().into_owned();
        let target = Stream::create(&target_path, base(), 0);
        assert_eq!(import(&target, 1, 10, &bytes), Err(Error::Invalid));
        assert_eq!(import(&target, 0, 11, &bytes), Err(Error::Invalid));
        let mut corrupted = bytes.clone();
        let last = corrupted.len() - 1;
        corrupted[last] ^= 1;
        assert_eq!(import(&target, 0, 10, &corrupted), Err(Error::Corrupt));
        import(&target, 0, 10, &bytes).unwrap();
        assert_eq!(import(&target, 0, 10, &bytes), Err(Error::Duplicate));
        let header = validate_group(&bytes, 0).unwrap();
        let next_lsn = bytes.len() as u64;
        let entry = encode_entry(b"two", 11, false).unwrap();
        let wrong_id = encode_group(
            &entry,
            next_lsn,
            11,
            header.log_id + 2,
            header.accumulated_checksum,
            false,
        )
        .unwrap();
        assert_eq!(
            import(&target, next_lsn, 11, &wrong_id),
            Err(Error::Invalid)
        );
        let next = encode_group(
            &entry,
            next_lsn,
            11,
            header.log_id + 1,
            header.accumulated_checksum,
            false,
        )
        .unwrap();
        import(&target, next_lsn, 11, &next).unwrap();
    }

    #[test]
    fn imported_batches_publish_together() {
        let source_directory = Directory::new();
        let source_path = source_directory.0.to_string_lossy().into_owned();
        let source = Stream::create(&source_path, base(), 0);
        source.append(b"one", 10).unwrap();
        source.append(b"two", 11).unwrap();
        source.append(b"three", 12).unwrap();
        let mut cursor = source.cursor(0, true);
        let mut records = Vec::new();
        while matches!(cursor.next(u64::MAX), Ok(Next::Entry)) {
            records.push((cursor.lsn(), cursor.scn(), cursor.bytes().to_vec()));
        }
        let groups: Vec<_> = records
            .iter()
            .map(|(lsn, scn, bytes)| ImportedGroup {
                source_lsn: *lsn,
                source_scn: *scn,
                zero_padding_before: 0,
                bytes,
            })
            .collect();

        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let target = Stream::create(&path, base(), 0);
        let result = target.append_imported(&groups).unwrap();
        assert_eq!(result.end_lsn, source.progress().unwrap().end_lsn);
        drop(target);
        let target = Stream::open(&path);
        let mut cursor = target.cursor(0, false);
        for payload in [b"one".as_slice(), b"two", b"three"] {
            cursor.next(u64::MAX).unwrap();
            assert_eq!(cursor.payload(), payload);
        }

        let rejected_directory = Directory::new();
        let rejected_path = rejected_directory.0.to_string_lossy().into_owned();
        let rejected = Stream::create(&rejected_path, base(), 0);
        let mut corrupted = records[1].2.clone();
        let last = corrupted.len() - 1;
        corrupted[last] ^= 1;
        assert_eq!(
            rejected.append_imported(&[
                ImportedGroup {
                    source_lsn: records[0].0,
                    source_scn: records[0].1,
                    zero_padding_before: 0,
                    bytes: &records[0].2,
                },
                ImportedGroup {
                    source_lsn: records[1].0,
                    source_scn: records[1].1,
                    zero_padding_before: 0,
                    bytes: &corrupted,
                },
            ]),
            Err(Error::Corrupt)
        );
        assert_eq!(rejected.progress().unwrap().end_lsn, 0);
    }

    #[test]
    fn product_log_boundaries_survive_reopen_and_import() {
        const OLD_NORMAL_LIMIT: usize = 2 * 1024 * 1024 + 16 * 1024;
        const MAX_GROUP_LEN: u64 =
            (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE + MAX_LOG_BODY_SIZE) as u64;

        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let mut expected = Vec::new();
        for (index, size) in [OLD_NORMAL_LIMIT - 1, OLD_NORMAL_LIMIT, OLD_NORMAL_LIMIT + 1]
            .into_iter()
            .enumerate()
        {
            let payload = vec![index as u8 + 1; size];
            let appended = stream.append(&payload, index as u64 + 10).unwrap();
            expected.push((appended.lsn, appended.scn, payload));
        }

        let target_group_lsn = BLOCK_SIZE - MAX_GROUP_LEN;
        while stream.progress().unwrap().end_lsn < target_group_lsn {
            let remaining = target_group_lsn - stream.progress().unwrap().end_lsn;
            let group_len = if remaining > MAX_GROUP_LEN && remaining < 2 * MAX_GROUP_LEN {
                remaining / 2
            } else {
                remaining.min(MAX_GROUP_LEN)
            };
            let payload_len =
                (group_len - GROUP_HEADER_SIZE as u64 - ENTRY_HEADER_SIZE as u64) as usize;
            let payload = vec![4; payload_len];
            let appended = stream.append(&payload, 20).unwrap();
            expected.push((appended.lsn, appended.scn, payload));
        }
        let largest = vec![5; MAX_LOG_BODY_SIZE];
        let accepted = stream.append(&largest, 30).unwrap();
        assert_eq!(accepted.lsn, target_group_lsn + GROUP_HEADER_SIZE as u64);
        assert_eq!(accepted.end_lsn, BLOCK_SIZE);
        expected.push((accepted.lsn, accepted.scn, largest));
        let next_segment = stream.append(b"next-segment", 31).unwrap();
        assert_eq!(next_segment.lsn, BLOCK_SIZE + GROUP_HEADER_SIZE as u64);
        expected.push((next_segment.lsn, next_segment.scn, b"next-segment".to_vec()));

        assert_eq!(
            stream.append(&vec![6; MAX_LOG_BODY_SIZE + 1], 32),
            Err(Error::Invalid)
        );
        assert_eq!(stream.progress().unwrap().end_lsn, next_segment.end_lsn);
        drop(stream);

        let verify = |stream: &Stream| {
            let mut cursor = stream.cursor(0, false);
            for (_, _, payload) in &expected {
                cursor.next(u64::MAX).unwrap();
                assert_eq!(cursor.payload(), payload);
            }
            assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
        };
        let reopened = Stream::open(&path);
        assert_eq!(reopened.status(), Ok(true));
        verify(&reopened);

        let target_directory = Directory::new();
        let target_path = target_directory.0.to_string_lossy().into_owned();
        let target = Stream::create(&target_path, base(), 0);
        let mut groups = reopened.cursor(0, true);
        for (lsn, scn, _) in &expected {
            groups.next(u64::MAX).unwrap();
            assert_eq!(groups.lsn() + GROUP_HEADER_SIZE as u64, *lsn);
            assert_eq!(groups.scn(), *scn);
            import(&target, groups.lsn(), *scn, groups.bytes()).unwrap();
        }
        assert_eq!(groups.next(u64::MAX), Ok(Next::End));
        drop(target);
        let target = Stream::open(&target_path);
        assert_eq!(target.status(), Ok(true));
        verify(&target);
    }

    #[test]
    fn scn_lookup_returns_a_readable_segment_boundary() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        stream.append(b"one", 10).unwrap();
        stream.append(b"two", 20).unwrap();

        assert_eq!(stream.locate_by_scn(0), Err(Error::OutLower));
        assert_eq!(stream.locate_by_scn(10), Ok(0));
        assert_eq!(stream.locate_by_scn(20), Ok(0));
    }

    #[test]
    fn imported_groups_preserve_every_entry_position() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let mut payload = encode_entry(b"one", 10, false).unwrap();
        let first_entry_len = payload.len() as u64;
        payload.extend(encode_entry(b"two", 10, false).unwrap());
        payload.extend(encode_entry(b"three", 11, false).unwrap());
        let group = encode_group(&payload, 0, 11, 1, 0, false).unwrap();
        import(&stream, 0, 11, &group).unwrap();

        let mut cursor = stream.cursor(GROUP_HEADER_SIZE as u64, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"one");
        assert_eq!(cursor.lsn(), GROUP_HEADER_SIZE as u64);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"two");
        assert_eq!(cursor.lsn(), first_entry_len + GROUP_HEADER_SIZE as u64);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"three");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
        drop(stream);

        let reopened = Stream::open(&path);
        let second_lsn = first_entry_len + GROUP_HEADER_SIZE as u64;
        assert_eq!(reopened.locate_by_lsn(second_lsn), Ok(11));
        let mut cursor = reopened.cursor(second_lsn, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"two");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"three");
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn entry_cursor_honors_replayable_point_within_a_group() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let mut payload = encode_entry(b"one", 10, false).unwrap();
        payload.extend(encode_entry(b"two", 11, false).unwrap());
        payload.extend(encode_entry(b"three", 12, false).unwrap());
        let group = encode_group(&payload, 0, 12, 1, 0, false).unwrap();
        import(&stream, 0, 12, &group).unwrap();

        let mut cursor = stream.cursor(GROUP_HEADER_SIZE as u64, false);
        cursor.next(10).unwrap();
        assert_eq!(cursor.payload(), b"one");
        assert_eq!(cursor.next(10), Ok(Next::BlockedByReplayablePoint));
        assert_eq!(cursor.next_min_scn(), 11);
        cursor.next(11).unwrap();
        assert_eq!(cursor.payload(), b"two");
        assert_eq!(cursor.next(11), Ok(Next::BlockedByReplayablePoint));
        assert_eq!(cursor.next_min_scn(), 12);
        cursor.next(12).unwrap();
        assert_eq!(cursor.payload(), b"three");
    }

    #[test]
    fn only_group_cursor_returns_padding() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let entry = encode_entry(&[0; 8], 10, true).unwrap();
        let group = encode_group(&entry, 0, 10, 1, 0, true).unwrap();
        import(&stream, 0, 10, &group).unwrap();
        stream.append(b"visible", 11).unwrap();

        let mut entries = stream.cursor(0, false);
        entries.next(u64::MAX).unwrap();
        assert_eq!(entries.payload(), b"visible");
        assert!(!entries.padding());

        let mut groups = stream.cursor(0, true);
        groups.next(u64::MAX).unwrap();
        assert!(groups.padding());
        groups.next(u64::MAX).unwrap();
        assert!(!groups.padding());
    }

    #[test]
    fn entry_cursor_reports_position_after_skipping_trailing_padding() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let entry = encode_entry(&[0; 8], 10, true).unwrap();
        let group = encode_group(&entry, 0, 10, 1, 0, true).unwrap();
        let end_lsn = group.len() as u64;
        import(&stream, 0, 10, &group).unwrap();

        let mut entries = stream.cursor(0, false);
        assert_eq!(entries.next(u64::MAX), Ok(Next::End));
        assert_eq!(entries.next_lsn(), end_lsn);
    }

    #[test]
    fn concurrent_appends_complete_once_and_survive_reopen() {
        use std::collections::BTreeSet;
        use std::sync::Barrier;

        const WRITERS: usize = 8;
        const WRITES_PER_WRITER: usize = 16;
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Arc::new(Stream::create(&path, base(), 0));
        let barrier = Arc::new(Barrier::new(WRITERS));
        let (sender, receiver) = std::sync::mpsc::channel();
        let mut threads = Vec::new();
        for writer in 0..WRITERS {
            let stream = Arc::clone(&stream);
            let barrier = Arc::clone(&barrier);
            let sender = sender.clone();
            threads.push(std::thread::spawn(move || {
                barrier.wait();
                for sequence in 0..WRITES_PER_WRITER {
                    let payload = format!("{writer}:{sequence}");
                    let result = stream.append(payload.as_bytes(), sequence as u64 + 1);
                    sender.send((writer, sequence, payload, result)).unwrap();
                }
            }));
        }
        drop(sender);
        let mut positions = BTreeSet::new();
        let mut expected = BTreeSet::new();
        let mut last_lsns = [0; WRITERS];
        let mut end_lsns = BTreeSet::new();
        for (writer, sequence, payload, result) in receiver {
            let result = result.unwrap();
            assert!(sequence == 0 || result.lsn > last_lsns[writer]);
            last_lsns[writer] = result.lsn;
            assert!(positions.insert(result.lsn));
            assert!(end_lsns.insert(result.end_lsn));
            assert!(expected.insert(payload));
        }
        for thread in threads {
            thread.join().unwrap();
        }
        assert_eq!(positions.len(), WRITERS * WRITES_PER_WRITER);
        drop(stream);

        let reopened = Stream::open(&path);
        let mut actual = BTreeSet::new();
        let mut cursor = reopened.cursor(GROUP_HEADER_SIZE as u64, false);
        loop {
            match cursor.next(u64::MAX) {
                Ok(Next::Entry) => {
                    actual.insert(String::from_utf8(cursor.payload().to_vec()).unwrap());
                }
                Ok(Next::End) => break,
                error => panic!("unexpected cursor result: {error:?}"),
            }
        }
        assert_eq!(actual, expected);
    }

    #[test]
    fn accepted_append_plan_failure_is_reported_without_publishing_progress() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let (failures, received_failures) = std::sync::mpsc::channel();
        stream
            .set_failure_callback(move |failure| {
                failures.send((failure.code, failure.operation)).unwrap();
            })
            .unwrap();
        let (flushes, received_flushes) = std::sync::mpsc::channel();
        stream
            .set_flush_callback(move |lsn, scn| flushes.send((lsn, scn)).unwrap())
            .unwrap();
        stream.lock().unwrap().fail_append_plan = true;

        let (reserved, completion) = stream.enqueue_append(b"accepted", 10, true).unwrap();
        assert!(reserved.end_lsn > 0);
        assert_eq!(
            received_failures.recv_timeout(std::time::Duration::from_secs(5)),
            Ok((Error::NoMemory, "plan append batch")),
        );
        assert_eq!(completion.unwrap().wait_durable(), Err(Error::NoMemory));
        assert_eq!(stream.lock().unwrap().end_lsn, 0);
        assert_eq!(stream.submit(b"later", 11), Err(Error::NoMemory));

        stream.close().unwrap();
        assert_eq!(
            received_failures.try_recv(),
            Err(std::sync::mpsc::TryRecvError::Empty),
        );
        assert_eq!(
            received_flushes.try_recv(),
            Err(std::sync::mpsc::TryRecvError::Empty),
        );
        assert_eq!(Stream::open(&path).progress().unwrap().end_lsn, 0);
    }

    #[test]
    fn imported_append_only_reports_terminal_persistence_failure() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let (failures, received_failures) = std::sync::mpsc::channel();
        stream
            .set_failure_callback(move |failure| {
                failures
                    .send((failure.code, failure.operation, failure.raw_os_error))
                    .unwrap();
            })
            .unwrap();
        fs::create_dir(directory.0.join(STAGED_SEGMENT_FILE)).unwrap();
        let entry = encode_entry(b"imported", 10, false).unwrap();
        let group = encode_group(&entry, 0, 10, 1, 0, false).unwrap();

        assert_eq!(import(&stream, 0, 11, &group), Err(Error::Invalid));
        assert_eq!(
            received_failures.try_recv(),
            Err(std::sync::mpsc::TryRecvError::Empty),
        );
        assert_eq!(import(&stream, 0, 10, &group), Err(Error::Io));
        let (code, operation, raw_os_error) = received_failures.try_recv().unwrap();
        assert_eq!((code, operation), (Error::Io, "open segment"));
        assert!(raw_os_error.is_some());
        assert_eq!(stream.lock().unwrap().end_lsn, 0);
        assert_eq!(import(&stream, 0, 10, &group), Err(Error::Io));
        assert_eq!(
            received_failures.try_recv(),
            Err(std::sync::mpsc::TryRecvError::Empty),
        );
    }

    #[test]
    fn submitted_appends_are_drained_by_close() {
        const APPENDS: u64 = 256;

        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let mut expected_end_lsn = 0;
        for index in 0..APPENDS {
            expected_end_lsn = stream
                .submit(index.to_string().as_bytes(), index + 1)
                .unwrap()
                .end_lsn;
        }
        stream.close().unwrap();
        drop(stream);

        let reopened = Stream::open(&path);
        assert_eq!(reopened.progress().unwrap().end_lsn, expected_end_lsn);
        let mut cursor = reopened.cursor(GROUP_HEADER_SIZE as u64, false);
        for index in 0..APPENDS {
            cursor.next(u64::MAX).unwrap();
            assert_eq!(cursor.payload(), index.to_string().as_bytes());
        }
        assert_eq!(cursor.next(u64::MAX), Ok(Next::End));
    }

    #[test]
    fn checkpoints_must_be_segment_boundaries() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0);
        let appended = stream.append(b"one", 10).unwrap();
        assert!(matches!(
            stream.set_base_lsn(appended.lsn),
            Err(Error::Invalid)
        ));
        assert_eq!(stream.progress().unwrap().base_lsn, 0);
        stream.close().unwrap();
        assert_eq!(stream.append(b"two", 11), Err(Error::Closed));
        assert!(matches!(stream.progress(), Err(Error::Closed)));
    }
}
