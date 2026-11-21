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
    Result, BLOCK_SIZE, ENTRY_HEADER_SIZE, GROUP_HEADER_SIZE, MAX_LOG_BODY_SIZE,
    PHYSICAL_BLOCK_SIZE, SEGMENT_HEADER_SIZE,
};
use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard};
use std::time::{Duration, Instant};

const MANIFEST_FILE: &str = "manifest";
const MANIFEST_SIZE: usize = 128;
const MANIFEST_FILE_SIZE: usize = MANIFEST_SIZE * 2;
const MANIFEST_MAGIC: &[u8; 8] = b"PALFMAN3";
const THROTTLE_CHECK_INTERVAL: Duration = Duration::from_millis(30);
const APPEND_BATCH_WINDOW: Duration = Duration::from_micros(50);
const MAX_APPEND_BATCH_REQUESTS: usize = 32;
const MAX_APPEND_BATCH_BYTES: usize = 2 * 1024 * 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DiskPolicy {
    pub capacity: u64,
    pub stop_percentage: u8,
    pub throttle_percentage: u8,
    pub max_throttle_duration_us: u64,
}

pub struct ImportedGroup<'a> {
    pub source_lsn: u64,
    pub source_scn: u64,
    pub bytes: &'a [u8],
}

impl DiskPolicy {
    pub const fn with_capacity(capacity: u64) -> Self {
        Self {
            capacity,
            stop_percentage: 100,
            throttle_percentage: 100,
            max_throttle_duration_us: 0,
        }
    }

    fn validate(self) -> Result<()> {
        if self.stop_percentage == 0
            || self.throttle_percentage < 40
            || (self.throttle_percentage < 100 && self.max_throttle_duration_us == 0)
        {
            Err(Error::Invalid)
        } else {
            Ok(())
        }
    }

    fn hard_limit(self) -> u64 {
        self.capacity.saturating_mul(self.stop_percentage as u64) / 100
    }

    fn throttle_limit(self) -> Option<u64> {
        if self.throttle_percentage < self.stop_percentage && self.capacity > 0 {
            Some(
                self.capacity
                    .saturating_mul(self.throttle_percentage as u64)
                    / 100,
            )
        } else {
            None
        }
    }
}

#[derive(Clone, Copy, Debug)]
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
    pub begin_lsn: u64,
    pub begin_scn: u64,
    pub base_lsn: u64,
    pub end_lsn: u64,
    pub end_scn: u64,
    pub ref_scn: u64,
    pub used_disk: u64,
    pub unrecyclable_disk: u64,
    pub integrity: bool,
}

#[derive(Clone, Debug)]
struct RecordMeta {
    lsn: u64,
    len: u64,
    header: GroupHeader,
}

impl RecordMeta {
    fn end_lsn(&self) -> u64 {
        self.lsn + self.len
    }
}

struct PendingAppend {
    id: u64,
    payload: Vec<u8>,
    ref_scn: u64,
}

struct PlannedRecord {
    lsn: u64,
    bytes: Vec<u8>,
    meta: Option<RecordMeta>,
}

struct AppendBatch {
    records: Vec<PlannedRecord>,
    appends: Vec<(u64, u64, u64, u64)>,
    segment_lengths: BTreeMap<u64, u64>,
    end_lsn: u64,
    end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
}

struct BatchCheckpoint {
    records_len: usize,
    appends_len: usize,
    segment_lengths: BTreeMap<u64, u64>,
    end_lsn: u64,
    end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
}

#[derive(Clone, Copy, Debug)]
struct CommittedTail {
    end_lsn: u64,
    end_scn: u64,
    log_id: i64,
    accumulated_checksum: u64,
}

#[derive(Clone, Debug)]
struct Manifest {
    base_lsn: u64,
    previous: BaseInfo,
    ref_scn: u64,
    committed: CommittedTail,
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
    manifest: Manifest,
    records: BTreeMap<u64, RecordMeta>,
    segment_lengths: BTreeMap<u64, u64>,
    end_lsn: u64,
    end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
    disk_policy: DiskPolicy,
    integrity: bool,
    closed: bool,
    failed: Option<Error>,
    next_append_id: u64,
    pending_appends: Vec<PendingAppend>,
    pending_append_bytes: usize,
    append_leader: bool,
    gather_appends: bool,
    append_results: BTreeMap<u64, Result<AppendResult>>,
}

struct StreamInner {
    state: Mutex<State>,
    changed: Condvar,
    append_callers: AtomicUsize,
}

struct AppendCallGuard<'a>(&'a AtomicUsize);

impl Drop for AppendCallGuard<'_> {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::Relaxed);
    }
}

pub struct Stream {
    inner: Arc<StreamInner>,
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
    next_min_scn: u64,
}

impl Stream {
    fn from_result(result: Result<State>) -> Self {
        let state = result.unwrap_or_else(|error| State {
            directory: PathBuf::new(),
            manifest_writer: None,
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
                committed: CommittedTail {
                    end_lsn: 0,
                    end_scn: 0,
                    log_id: 0,
                    accumulated_checksum: 0,
                },
            },
            records: BTreeMap::new(),
            segment_lengths: BTreeMap::new(),
            end_lsn: 0,
            end_scn: 0,
            next_log_id: 1,
            accumulated_checksum: 0,
            disk_policy: DiskPolicy::with_capacity(0),
            integrity: false,
            closed: false,
            failed: Some(error),
            next_append_id: 0,
            pending_appends: Vec::new(),
            pending_append_bytes: 0,
            append_leader: false,
            gather_appends: false,
            append_results: BTreeMap::new(),
        });
        Self {
            inner: Arc::new(StreamInner {
                state: Mutex::new(state),
                changed: Condvar::new(),
                append_callers: AtomicUsize::new(0),
            }),
        }
    }

    pub fn create(path: &str, base: BaseInfo, ref_scn: u64, disk_limit: u64) -> Self {
        Self::create_with_policy(path, base, ref_scn, DiskPolicy::with_capacity(disk_limit))
    }

    pub fn create_with_policy(
        path: &str,
        base: BaseInfo,
        ref_scn: u64,
        disk_policy: DiskPolicy,
    ) -> Self {
        Self::from_result(create_state(Path::new(path), base, ref_scn, disk_policy))
    }

    pub fn open(path: &str, disk_limit: u64) -> Self {
        Self::open_with_policy(path, DiskPolicy::with_capacity(disk_limit))
    }

    pub fn open_with_policy(path: &str, disk_policy: DiskPolicy) -> Self {
        Self::from_result(open_state(Path::new(path), disk_policy))
    }

    fn lock(&self) -> Result<MutexGuard<'_, State>> {
        self.inner.state.lock().map_err(|_| Error::Io)
    }

    fn lock_for_write(&self, write_size: u64) -> Result<MutexGuard<'_, State>> {
        let mut state = self.lock()?;
        loop {
            state.check_writable()?;
            let Some(mut remaining) = state.throttle_delay(write_size) else {
                return Ok(state);
            };
            loop {
                let wait = remaining.min(THROTTLE_CHECK_INTERVAL);
                let (next, timeout) = self
                    .inner
                    .changed
                    .wait_timeout(state, wait)
                    .map_err(|_| Error::Io)?;
                state = next;
                state.check_writable()?;
                if timeout.timed_out() {
                    remaining = remaining.saturating_sub(wait);
                    if remaining.is_zero() {
                        return Ok(state);
                    }
                } else {
                    break;
                }
            }
        }
    }

    pub fn status(&self) -> Result<bool> {
        let state = self.lock()?;
        state.check_open()?;
        Ok(state.integrity)
    }

    pub fn append(&self, payload: &[u8], ref_scn: u64) -> Result<AppendResult> {
        if payload.is_empty() || payload.len() > MAX_LOG_BODY_SIZE || ref_scn == u64::MAX {
            return Err(Error::Invalid);
        }
        self.inner.append_callers.fetch_add(1, Ordering::Relaxed);
        let _call_guard = AppendCallGuard(&self.inner.append_callers);
        let mut state = self.lock_for_write(payload.len() as u64)?;
        let id = state.next_append_id;
        state.next_append_id = state.next_append_id.checked_add(1).ok_or(Error::Invalid)?;
        let mut request = Some(PendingAppend {
            id,
            payload: copied_bytes(payload)?,
            ref_scn,
        });
        loop {
            if let Some(result) = state.append_results.remove(&id) {
                return result;
            }
            state.check_writable()?;
            if let Some(pending) = request.as_ref() {
                if state.can_queue_append(pending.payload.len()) {
                    state.enqueue_append(request.take().unwrap());
                    self.inner.changed.notify_all();
                }
            }
            if !state.append_leader && !state.pending_appends.is_empty() {
                state.append_leader = true;
                if self.inner.append_callers.load(Ordering::Relaxed) > 1 || state.gather_appends {
                    let deadline = Instant::now() + APPEND_BATCH_WINDOW;
                    while state.append_batch_has_capacity()
                        && !state.closed
                        && state.failed.is_none()
                    {
                        let now = Instant::now();
                        if now >= deadline {
                            break;
                        }
                        let (next, timeout) = self
                            .inner
                            .changed
                            .wait_timeout(state, deadline - now)
                            .map_err(|_| Error::Io)?;
                        state = next;
                        if timeout.timed_out() {
                            break;
                        }
                    }
                }
                let requests = state.take_append_batch();
                let (results, retry) = state.execute_append_batch(requests);
                state.requeue_appends(retry);
                state.append_results.extend(results);
                state.append_leader = false;
                self.inner.changed.notify_all();
            } else {
                state = self.inner.changed.wait(state).map_err(|_| Error::Io)?;
            }
        }
    }

    pub fn append_imported(&self, groups: &[ImportedGroup]) -> Result<AppendResult> {
        if groups.is_empty() {
            return Err(Error::Invalid);
        }
        let write_size = groups.iter().try_fold(0u64, |total, group| {
            total.checked_add(group.bytes.len() as u64).ok_or(Error::Invalid)
        })?;
        let mut state = self.lock_for_write(write_size)?;
        let mut plan = AppendBatch::new(&state);
        for group in groups {
            plan.push_imported(&state, group)?;
        }
        let last = groups.last().ok_or(Error::Invalid)?;
        if let Err(error) = state.persist_append_batch(&plan) {
            state.failed = Some(error);
            return Err(error);
        }
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
        let begin_lsn = state.manifest.base_lsn;
        let begin_scn = state
            .records
            .range(begin_lsn..)
            .next()
            .map(|(_, record)| record.header.max_scn)
            .unwrap_or(state.manifest.previous.prev_scn);
        let unrecyclable_disk = state.end_lsn.saturating_sub(state.manifest.base_lsn);
        Ok(Progress {
            begin_lsn,
            begin_scn,
            base_lsn: state.manifest.base_lsn,
            end_lsn: state.end_lsn,
            end_scn: state.end_scn,
            ref_scn: state.manifest.ref_scn,
            used_disk: state.segment_lengths.values().sum(),
            unrecyclable_disk,
            integrity: state.integrity,
        })
    }

    pub fn base_info(&self, lsn: u64) -> Result<BaseInfo> {
        let state = self.lock()?;
        state.check_open()?;
        if lsn > state.end_lsn || lsn < state.manifest.base_lsn {
            return Err(Error::Invalid);
        }
        let previous = state.previous_info(lsn);
        Ok(BaseInfo {
            curr_lsn: lsn,
            ..previous
        })
    }

    pub fn set_base_lsn(&self, lsn: u64) -> Result<Progress> {
        let mut state = self.lock()?;
        state.check_open()?;
        if lsn > state.end_lsn || lsn < state.manifest.base_lsn || block_offset(lsn) != 0 {
            return Err(Error::Invalid);
        }
        let previous = state.previous_info(lsn);
        let mut manifest = state.manifest.clone();
        manifest.base_lsn = lsn;
        manifest.previous = BaseInfo {
            curr_lsn: lsn,
            ..previous
        };
        state.publish_manifest(&manifest)?;
        state.manifest = manifest;
        let minimum_block = block_id(lsn);
        let old_blocks: Vec<u64> = state
            .segment_lengths
            .range(..minimum_block)
            .map(|(id, _)| *id)
            .collect();
        for id in old_blocks {
            match fs::remove_file(segment_path(&state.directory, id)) {
                Ok(()) => {
                    state.segment_lengths.remove(&id);
                }
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                    state.segment_lengths.remove(&id);
                }
                Err(error) => return Err(error.into()),
            }
        }
        sync_directory(&state.directory)?;
        state.records = state.records.split_off(&lsn);
        drop(state);
        self.inner.changed.notify_all();
        self.progress()
    }

    pub fn set_disk_limit(&self, disk_limit: u64) -> Result<()> {
        self.set_disk_policy(DiskPolicy::with_capacity(disk_limit))
    }

    pub fn set_disk_policy(&self, disk_policy: DiskPolicy) -> Result<()> {
        disk_policy.validate()?;
        let mut state = self.lock()?;
        state.check_open()?;
        state.disk_policy = disk_policy;
        self.inner.changed.notify_all();
        Ok(())
    }

    pub fn locate_by_scn(&self, scn: u64) -> Result<u64> {
        let state = self.lock()?;
        state.check_open()?;
        let lsn = state
            .records
            .iter()
            .rfind(|(_, record)| record.header.max_scn <= scn)
            .map(|(lsn, _)| *lsn)
            .ok_or(Error::OutLower)?;
        Ok((block_id(lsn) * BLOCK_SIZE).max(state.manifest.base_lsn))
    }

    pub fn locate_by_lsn(&self, lsn: u64) -> Result<u64> {
        let state = self.lock()?;
        state.check_open()?;
        let (_, record) = state
            .records
            .range(..=lsn)
            .next_back()
            .ok_or(Error::OutLower)?;
        if lsn >= record.end_lsn() {
            return Err(Error::OutUpper);
        }
        Ok(record.header.max_scn)
    }

    pub fn close(&self) -> Result<()> {
        let mut state = self.lock()?;
        state.closed = true;
        self.inner.changed.notify_all();
        Ok(())
    }

    pub fn cursor(&self, lsn: u64, group_mode: bool) -> Cursor {
        Cursor::new(Arc::clone(&self.inner), lsn, group_mode)
    }

    pub fn cursor_by_scn(&self, scn: u64, group_mode: bool) -> Result<Cursor> {
        let lsn = self.locate_by_scn(scn)?;
        Ok(self.cursor(lsn, group_mode))
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
            next_min_scn: 0,
        }
    }

    fn lock(&self) -> Result<MutexGuard<'_, State>> {
        self.inner.state.lock().map_err(|_| Error::Io)
    }

    pub fn reuse(&mut self, lsn: u64) -> Result<()> {
        self.next_lsn = lsn;
        self.bytes.clear();
        self.entry_offset = 0;
        Ok(())
    }

    pub fn next(&mut self, replayable_point: u64) -> Result<()> {
        loop {
            if !self.group_mode && !self.bytes.is_empty() {
                let entries = group_entries(&self.bytes[GROUP_HEADER_SIZE..], self.group_lsn)?;
                let mut index = self.entry_offset + 1;
                while index < entries.len() {
                    let entry = &entries[index];
                    if entry.header.scn > replayable_point {
                        self.next_min_scn = entry.header.scn;
                        return Err(Error::End);
                    }
                    if !entry.header.padding {
                        self.entry_offset = index;
                        return self.set_entry(entry);
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
                return Err(Error::End);
            }
            let meta = state
                .records
                .range(..=self.next_lsn)
                .next_back()
                .map(|(_, meta)| meta.clone())
                .ok_or(Error::OutLower)?;
            if self.next_lsn >= meta.end_lsn() {
                let remaining = BLOCK_SIZE - block_offset(self.next_lsn);
                if remaining < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64
                    && self.next_lsn + remaining <= state.end_lsn
                {
                    let next_lsn = self.next_lsn + remaining;
                    drop(state);
                    self.next_lsn = next_lsn;
                    self.bytes.clear();
                    continue;
                }
                return Err(Error::Partial);
            }
            let bytes = state.read_record(meta.lsn)?;
            let header = validate_group(&bytes, meta.lsn)?;
            if self.group_mode {
                if self.next_lsn != meta.lsn {
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
                self.next_lsn = meta.end_lsn();
                self.next_min_scn = 0;
                return Ok(());
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
                return Err(Error::End);
            }
            drop(state);
            self.bytes = bytes;
            self.entry_offset = index;
            self.group_lsn = meta.lsn;
            self.current_lsn = meta.lsn;
            self.current_end_lsn = meta.end_lsn();
            self.next_lsn = meta.end_lsn();
            return self.set_entry(&entries[index]);
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
    pub fn next_min_scn(&self) -> u64 {
        self.next_min_scn
    }

    pub fn is_last(&self) -> Result<bool> {
        let state = self.lock()?;
        state.check_open()?;
        Ok(self.current_end_lsn >= state.end_lsn)
    }
}

impl AppendBatch {
    fn new(state: &State) -> Self {
        Self {
            records: Vec::new(),
            appends: Vec::new(),
            segment_lengths: state.segment_lengths.clone(),
            end_lsn: state.end_lsn,
            end_scn: state.end_scn,
            next_log_id: state.next_log_id,
            accumulated_checksum: state.accumulated_checksum,
        }
    }

    fn checkpoint(&self) -> BatchCheckpoint {
        BatchCheckpoint {
            records_len: self.records.len(),
            appends_len: self.appends.len(),
            segment_lengths: self.segment_lengths.clone(),
            end_lsn: self.end_lsn,
            end_scn: self.end_scn,
            next_log_id: self.next_log_id,
            accumulated_checksum: self.accumulated_checksum,
        }
    }

    fn rollback(&mut self, checkpoint: BatchCheckpoint) {
        self.records.truncate(checkpoint.records_len);
        self.appends.truncate(checkpoint.appends_len);
        self.segment_lengths = checkpoint.segment_lengths;
        self.end_lsn = checkpoint.end_lsn;
        self.end_scn = checkpoint.end_scn;
        self.next_log_id = checkpoint.next_log_id;
        self.accumulated_checksum = checkpoint.accumulated_checksum;
    }

    fn ensure_capacity(&self, state: &State, additional: u64) -> Result<()> {
        state.ensure_capacity_at(&self.segment_lengths, self.end_lsn, additional)
    }

    fn note_write(&mut self, lsn: u64, length: usize) {
        let segment_length = self
            .segment_lengths
            .entry(block_id(lsn))
            .or_insert(SEGMENT_HEADER_SIZE);
        *segment_length = (*segment_length).max(physical_offset(lsn) + length as u64);
    }

    fn push(&mut self, state: &State, request: &PendingAppend) -> Result<()> {
        let scn = request.ref_scn.max(self.end_scn.saturating_add(1));
        let entry = encode_entry(&request.payload, scn, false)?;
        let group_len = GROUP_HEADER_SIZE + entry.len();
        let mut lsn = self.end_lsn;
        let remaining = BLOCK_SIZE - block_offset(lsn);
        if remaining < group_len as u64 {
            if remaining < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64 {
                self.ensure_capacity(state, remaining)?;
                let bytes = zeroed_bytes(remaining as usize)?;
                self.note_write(lsn, bytes.len());
                self.records.push(PlannedRecord {
                    lsn,
                    bytes,
                    meta: None,
                });
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
                self.ensure_capacity(state, group.len() as u64)?;
                let header = validate_group(&group, lsn)?;
                self.note_write(lsn, group.len());
                self.records.push(PlannedRecord {
                    lsn,
                    bytes: group,
                    meta: Some(RecordMeta {
                        lsn,
                        len: remaining,
                        header: header.clone(),
                    }),
                });
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
        self.ensure_capacity(state, group.len() as u64)?;
        let header = validate_group(&group, lsn)?;
        self.note_write(lsn, group.len());
        self.records.push(PlannedRecord {
            lsn,
            bytes: group,
            meta: Some(RecordMeta {
                lsn,
                len: group_len as u64,
                header: header.clone(),
            }),
        });
        self.end_lsn = lsn + group_len as u64;
        self.end_scn = scn;
        self.appends.push((
            request.id,
            lsn + GROUP_HEADER_SIZE as u64,
            scn,
            self.end_lsn,
        ));
        self.accumulated_checksum = header.accumulated_checksum;
        self.next_log_id = self.next_log_id.checked_add(1).ok_or(Error::Invalid)?;
        Ok(())
    }

    fn push_imported(&mut self, state: &State, group: &ImportedGroup) -> Result<()> {
        if group.source_lsn < self.end_lsn {
            return Err(Error::Duplicate);
        }
        if group.source_lsn != self.end_lsn || group.source_scn == u64::MAX {
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
        self.ensure_capacity(state, group.bytes.len() as u64)?;
        self.note_write(group.source_lsn, group.bytes.len());
        self.records.push(PlannedRecord {
            lsn: group.source_lsn,
            bytes: copied_bytes(group.bytes)?,
            meta: Some(RecordMeta {
                lsn: group.source_lsn,
                len: group.bytes.len() as u64,
                header: header.clone(),
            }),
        });
        self.end_lsn = group.source_lsn + group.bytes.len() as u64;
        self.end_scn = group.source_scn;
        self.next_log_id = self.next_log_id.checked_add(1).ok_or(Error::Invalid)?;
        self.accumulated_checksum = header.accumulated_checksum;
        Ok(())
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

    fn can_queue_append(&self, payload_len: usize) -> bool {
        self.pending_appends.len() < MAX_APPEND_BATCH_REQUESTS
            && (self.pending_appends.is_empty()
                || self.pending_append_bytes.saturating_add(payload_len) <= MAX_APPEND_BATCH_BYTES)
    }

    fn append_batch_has_capacity(&self) -> bool {
        self.pending_appends.len() < MAX_APPEND_BATCH_REQUESTS
            && self.pending_append_bytes < MAX_APPEND_BATCH_BYTES
    }

    fn enqueue_append(&mut self, request: PendingAppend) {
        self.pending_append_bytes = self
            .pending_append_bytes
            .saturating_add(request.payload.len());
        self.pending_appends.push(request);
    }

    fn take_append_batch(&mut self) -> Vec<PendingAppend> {
        self.pending_append_bytes = 0;
        std::mem::take(&mut self.pending_appends)
    }

    fn requeue_appends(&mut self, requests: Vec<PendingAppend>) {
        self.pending_append_bytes = requests.iter().fold(0usize, |bytes, request| {
            bytes.saturating_add(request.payload.len())
        });
        self.pending_appends = requests;
    }

    fn execute_append_batch(
        &mut self,
        requests: Vec<PendingAppend>,
    ) -> (Vec<(u64, Result<AppendResult>)>, Vec<PendingAppend>) {
        self.gather_appends = requests.len() > 1;
        if let Err(error) = self.check_writable() {
            return (
                requests
                    .into_iter()
                    .map(|request| (request.id, Err(error)))
                    .collect(),
                Vec::new(),
            );
        }
        let mut plan = AppendBatch::new(self);
        let mut results = Vec::new();
        let mut requests = requests.into_iter();
        while let Some(request) = requests.next() {
            let checkpoint = plan.checkpoint();
            if let Err(error) = plan.push(self, &request) {
                plan.rollback(checkpoint);
                results.push((request.id, Err(error)));
                return self.finish_append_batch(plan, results, requests.collect());
            }
        }
        self.finish_append_batch(plan, results, Vec::new())
    }

    fn finish_append_batch(
        &mut self,
        plan: AppendBatch,
        mut results: Vec<(u64, Result<AppendResult>)>,
        retry: Vec<PendingAppend>,
    ) -> (Vec<(u64, Result<AppendResult>)>, Vec<PendingAppend>) {
        if plan.appends.is_empty() {
            return (results, retry);
        }
        if let Err(error) = self.persist_append_batch(&plan) {
            self.failed = Some(error);
            results.extend(plan.appends.iter().map(|(id, _, _, _)| (*id, Err(error))));
            results.extend(retry.into_iter().map(|request| (request.id, Err(error))));
            return (results, Vec::new());
        }
        results.extend(plan.appends.into_iter().map(|(id, lsn, scn, end_lsn)| {
            (
                id,
                Ok(AppendResult {
                    lsn,
                    scn,
                    end_lsn,
                    end_scn: scn,
                }),
            )
        }));
        (results, retry)
    }

    fn persist_append_batch(&mut self, plan: &AppendBatch) -> Result<()> {
        self.write_planned_records(&plan.records)?;
        let committed = plan
            .records
            .iter()
            .rev()
            .find_map(|record| record.meta.as_ref())
            .ok_or(Error::Invalid)?;
        let mut manifest = self.manifest.clone();
        manifest.committed = CommittedTail {
            end_lsn: plan.end_lsn,
            end_scn: plan.end_scn,
            log_id: committed.header.log_id,
            accumulated_checksum: committed.header.accumulated_checksum,
        };
        self.publish_manifest(&manifest)?;
        self.manifest = manifest;
        self.segment_lengths = plan.segment_lengths.clone();
        self.end_lsn = plan.end_lsn;
        self.end_scn = plan.end_scn;
        self.next_log_id = plan.next_log_id;
        self.accumulated_checksum = plan.accumulated_checksum;
        for record in &plan.records {
            if let Some(meta) = &record.meta {
                self.records.insert(meta.lsn, meta.clone());
            }
        }
        Ok(())
    }

    fn write_planned_records(&mut self, records: &[PlannedRecord]) -> Result<()> {
        let mut index = 0;
        let mut new_segment = false;
        while index < records.len() {
            let id = block_id(records[index].lsn);
            let path = segment_path(&self.directory, id);
            let create = !self.segment_lengths.contains_key(&id);
            let mut file = OpenOptions::new()
                .create(true)
                .truncate(false)
                .read(true)
                .write(true)
                .open(path)?;
            if create {
                file.write_all(&encode_segment_header(id)?)?;
                new_segment = true;
            }
            while index < records.len() && block_id(records[index].lsn) == id {
                let record = &records[index];
                file.seek(SeekFrom::Start(physical_offset(record.lsn)))?;
                file.write_all(&record.bytes)?;
                index += 1;
            }
            file.sync_data()?;
        }
        if new_segment {
            sync_directory(&self.directory)?;
        }
        Ok(())
    }

    fn ensure_capacity_at(
        &self,
        segment_lengths: &BTreeMap<u64, u64>,
        end_lsn: u64,
        additional: u64,
    ) -> Result<()> {
        let hard_limit = self.disk_policy.hard_limit();
        if hard_limit == 0 {
            return Ok(());
        }
        let used: u64 = segment_lengths.values().sum();
        let id = block_id(end_lsn);
        let segment_exists = segment_lengths.contains_key(&id);
        let required = additional
            + if segment_exists {
                0
            } else {
                SEGMENT_HEADER_SIZE
            };
        if used.saturating_add(required) > hard_limit {
            Err(Error::NoSpace)
        } else {
            Ok(())
        }
    }

    fn throttle_delay(&self, write_size: u64) -> Option<Duration> {
        let trigger = self.disk_policy.throttle_limit()?;
        let current = self.end_lsn.saturating_sub(self.manifest.base_lsn);
        if current <= trigger {
            return None;
        }
        let available = self.disk_policy.hard_limit().saturating_sub(trigger);
        if available == 0 {
            return None;
        }
        let after = current
            .saturating_sub(trigger)
            .saturating_add(write_size)
            .min(available);
        let delay_us = (self.disk_policy.max_throttle_duration_us as u128)
            .saturating_mul(write_size as u128)
            .saturating_mul(after as u128)
            / (available as u128 * available as u128);
        Some(Duration::from_micros(delay_us.min(u64::MAX as u128) as u64))
    }

    fn read_record(&self, lsn: u64) -> Result<Vec<u8>> {
        let meta = self.records.get(&lsn).ok_or(Error::OutLower)?;
        let mut file = File::open(segment_path(&self.directory, block_id(lsn)))?;
        file.seek(SeekFrom::Start(physical_offset(lsn)))?;
        let mut bytes = zeroed_bytes(meta.len as usize)?;
        file.read_exact(&mut bytes)?;
        validate_group(&bytes, lsn)?;
        Ok(bytes)
    }

    fn previous_info(&self, lsn: u64) -> BaseInfo {
        if let Some((_, record)) = self.records.range(..lsn).next_back() {
            BaseInfo {
                curr_lsn: lsn,
                prev_lsn: record.lsn,
                prev_scn: record.header.max_scn,
                prev_log_id: record.header.log_id,
                prev_accumulated_checksum: record.header.accumulated_checksum,
            }
        } else {
            BaseInfo {
                curr_lsn: lsn,
                ..self.manifest.previous
            }
        }
    }
}

fn create_state(
    directory: &Path,
    base: BaseInfo,
    ref_scn: u64,
    disk_policy: DiskPolicy,
) -> Result<State> {
    if base.curr_lsn < base.prev_lsn || ref_scn == u64::MAX {
        return Err(Error::Invalid);
    }
    disk_policy.validate()?;
    fs::create_dir_all(directory)?;
    let manifest = Manifest {
        base_lsn: base.curr_lsn,
        previous: base,
        ref_scn,
        committed: CommittedTail {
            end_lsn: base.curr_lsn,
            end_scn: base.prev_scn,
            log_id: base.prev_log_id,
            accumulated_checksum: base.prev_accumulated_checksum,
        },
    };
    let manifest_writer = ManifestWriter::create(directory, &manifest)?;
    sync_directory(directory)?;
    Ok(State {
        directory: directory.to_path_buf(),
        manifest_writer: Some(manifest_writer),
        end_lsn: base.curr_lsn,
        end_scn: base.prev_scn,
        next_log_id: base.prev_log_id.checked_add(1).ok_or(Error::Invalid)?,
        accumulated_checksum: base.prev_accumulated_checksum,
        manifest,
        records: BTreeMap::new(),
        segment_lengths: BTreeMap::new(),
        disk_policy,
        integrity: true,
        closed: false,
        failed: None,
        next_append_id: 0,
        pending_appends: Vec::new(),
        pending_append_bytes: 0,
        append_leader: false,
        gather_appends: false,
        append_results: BTreeMap::new(),
    })
}

fn open_state(directory: &Path, disk_policy: DiskPolicy) -> Result<State> {
    disk_policy.validate()?;
    let (manifest, manifest_writer) = ManifestWriter::open(directory)?;
    let committed = manifest.committed;
    if committed.end_lsn < manifest.base_lsn {
        return Err(Error::Corrupt);
    }
    let mut records = BTreeMap::new();
    let mut segment_lengths = BTreeMap::new();
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
    if ids
        .first()
        .is_some_and(|id| *id < block_id(manifest.base_lsn))
    {
        return Err(Error::Corrupt);
    }
    let mut end_lsn = manifest.base_lsn;
    let mut end_scn = manifest.previous.prev_scn;
    let mut next_log_id = manifest
        .previous
        .prev_log_id
        .checked_add(1)
        .ok_or(Error::Corrupt)?;
    let mut accumulated_checksum = manifest.previous.prev_accumulated_checksum;
    for id in ids.iter().copied() {
        if end_lsn == committed.end_lsn {
            break;
        }
        if id != block_id(end_lsn) {
            return Err(Error::Corrupt);
        }
        let path = segment_path(directory, id);
        let mut file = OpenOptions::new().read(true).write(true).open(&path)?;
        let length = file.metadata()?.len();
        if !(SEGMENT_HEADER_SIZE..=PHYSICAL_BLOCK_SIZE).contains(&length) {
            return Err(Error::Corrupt);
        }
        let mut segment_header = zeroed_bytes(SEGMENT_HEADER_SIZE as usize)?;
        file.read_exact(&mut segment_header)?;
        validate_segment_header(&segment_header, id)?;
        segment_lengths.insert(id, length);
        while end_lsn < committed.end_lsn && block_id(end_lsn) == id {
            let lsn = end_lsn;
            let remaining = BLOCK_SIZE - block_offset(lsn);
            if remaining < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64 {
                if physical_offset(lsn) + remaining > length {
                    return Err(Error::Corrupt);
                }
                let mut padding = zeroed_bytes(remaining as usize)?;
                file.seek(SeekFrom::Start(physical_offset(lsn)))?;
                file.read_exact(&mut padding)?;
                if padding.iter().any(|byte| *byte != 0) {
                    return Err(Error::Corrupt);
                }
                end_lsn += remaining;
                continue;
            }
            if physical_offset(lsn) + GROUP_HEADER_SIZE as u64 > length {
                return Err(Error::Corrupt);
            }
            file.seek(SeekFrom::Start(physical_offset(lsn)))?;
            let mut header_bytes = zeroed_bytes(GROUP_HEADER_SIZE)?;
            file.read_exact(&mut header_bytes)?;
            if header_bytes.iter().all(|byte| *byte == 0) {
                return Err(Error::Corrupt);
            }
            let header = decode_group_header(&header_bytes)?;
            let record_len = GROUP_HEADER_SIZE as u64 + header.data_len as u64;
            let record_end = lsn.checked_add(record_len).ok_or(Error::Corrupt)?;
            if block_offset(lsn) + record_len > BLOCK_SIZE
                || record_end > committed.end_lsn
                || physical_offset(lsn) + record_len > length
            {
                return Err(Error::Corrupt);
            }
            let mut bytes = zeroed_bytes(record_len as usize)?;
            file.seek(SeekFrom::Start(physical_offset(lsn)))?;
            file.read_exact(&mut bytes)?;
            let header = validate_group(&bytes, lsn)?;
            if header.committed_end_lsn != lsn
                || header.log_id != next_log_id
                || header.accumulated_checksum
                    != crc64_with_seed(accumulated_checksum, &header.data_checksum.to_le_bytes())
            {
                return Err(Error::Corrupt);
            }
            let meta = RecordMeta {
                lsn,
                len: record_len,
                header: header.clone(),
            };
            records.insert(lsn, meta);
            end_lsn = record_end;
            end_scn = header.max_scn;
            next_log_id = header.log_id.checked_add(1).ok_or(Error::Corrupt)?;
            accumulated_checksum = header.accumulated_checksum;
        }
        if end_lsn != committed.end_lsn.min((id + 1) * BLOCK_SIZE) {
            return Err(Error::Corrupt);
        }
    }
    if end_lsn != committed.end_lsn
        || end_scn != committed.end_scn
        || next_log_id != committed.log_id.checked_add(1).ok_or(Error::Corrupt)?
        || accumulated_checksum != committed.accumulated_checksum
    {
        return Err(Error::Corrupt);
    }
    let last_required = records
        .values()
        .next_back()
        .map(|record| block_id(record.lsn));
    let mut removed_segment = false;
    for id in ids {
        if Some(id) == last_required {
            let valid_length = if id == block_id(committed.end_lsn) {
                physical_offset(committed.end_lsn)
            } else {
                PHYSICAL_BLOCK_SIZE
            };
            let path = segment_path(directory, id);
            OpenOptions::new()
                .write(true)
                .open(&path)?
                .set_len(valid_length)?;
            segment_lengths.insert(id, valid_length);
        } else if last_required.is_none_or(|last| id > last) {
            fs::remove_file(segment_path(directory, id))?;
            removed_segment = true;
        }
    }
    if removed_segment {
        sync_directory(directory)?;
    }
    Ok(State {
        directory: directory.to_path_buf(),
        manifest_writer: Some(manifest_writer),
        manifest,
        records,
        segment_lengths,
        end_lsn,
        end_scn,
        next_log_id,
        accumulated_checksum,
        disk_policy,
        integrity: true,
        closed: false,
        failed: None,
        next_append_id: 0,
        pending_appends: Vec::new(),
        pending_append_bytes: 0,
        append_leader: false,
        gather_appends: false,
        append_results: BTreeMap::new(),
    })
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
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(directory.join(MANIFEST_FILE))?;
        if file.metadata()?.len() != MANIFEST_FILE_SIZE as u64 {
            return Err(Error::Corrupt);
        }
        let mut bytes = [0; MANIFEST_FILE_SIZE];
        file.read_exact(&mut bytes)?;
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
        self.file
            .seek(SeekFrom::Start(slot * MANIFEST_SIZE as u64))?;
        self.file.write_all(&bytes)?;
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
        bytes[56..64].copy_from_slice(&manifest.committed.end_lsn.to_le_bytes());
        bytes[64..72].copy_from_slice(&manifest.committed.end_scn.to_le_bytes());
        bytes[72..80].copy_from_slice(&manifest.committed.log_id.to_le_bytes());
        bytes[80..88].copy_from_slice(&manifest.committed.accumulated_checksum.to_le_bytes());
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
                committed: CommittedTail {
                    end_lsn: number(56),
                    end_scn: number(64),
                    log_id: signed(72),
                    accumulated_checksum: number(80),
                },
            },
        ))
    }
}

#[cfg(unix)]
fn sync_directory(directory: &Path) -> Result<()> {
    File::open(directory)?.sync_all()?;
    Ok(())
}

#[cfg(not(unix))]
fn sync_directory(_directory: &Path) -> Result<()> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(0);

    struct Directory(PathBuf);

    impl Directory {
        fn new() -> Self {
            let sequence = NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed);
            let path =
                std::env::temp_dir().join(format!("seekdb-palf-{}-{sequence}", std::process::id()));
            let _ = fs::remove_dir_all(&path);
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
            bytes,
        }])
    }

    #[test]
    fn acknowledged_records_survive_reopen_and_torn_tail_is_removed() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        let first = stream.append(b"one", 10).unwrap();
        let second = stream.append(b"two", 11).unwrap();
        assert_eq!(first.lsn, GROUP_HEADER_SIZE as u64);
        assert_eq!(second.lsn, first.end_lsn + GROUP_HEADER_SIZE as u64);
        drop(stream);
        let segment = segment_path(&directory.0, 0);
        OpenOptions::new()
            .append(true)
            .open(&segment)
            .unwrap()
            .write_all(b"torn")
            .unwrap();
        let reopened = Stream::open(&path, 0);
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
        assert_eq!(cursor.next(u64::MAX), Err(Error::End));
    }

    #[test]
    fn complete_but_unconfirmed_group_is_removed() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        let confirmed = stream.append(b"confirmed", 10).unwrap();
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
            let length = file.metadata().unwrap().len();
            state.segment_lengths.insert(block_id(lsn), length);
        }
        drop(stream);

        let reopened = Stream::open(&path, 0);
        assert_eq!(reopened.progress().unwrap().end_lsn, confirmed.end_lsn);
        reopened.append(b"next", 11).unwrap();
        let mut cursor = reopened.cursor(confirmed.lsn, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"confirmed");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"next");
        assert_eq!(cursor.next(u64::MAX), Err(Error::End));
    }

    #[test]
    fn confirmed_corruption_is_not_truncated() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        stream.append(b"one", 10).unwrap();
        let second = stream.append(b"two", 11).unwrap();
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

        assert_eq!(Stream::open(&path, 0).status(), Err(Error::Corrupt));
        assert_eq!(fs::metadata(segment).unwrap().len(), original_length);
        assert_eq!(
            fs::metadata(segment_path(&directory.0, 0)).unwrap().len(),
            physical_offset(second.end_lsn)
        );
    }

    #[test]
    fn committed_short_zero_padding_survives_reopen() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
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

        let reopened = Stream::open(&path, 0);
        assert_eq!(reopened.status(), Ok(true));
        let mut cursor = reopened.cursor(0, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), vec![7; payload_len]);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"next");
        assert_eq!(cursor.next(u64::MAX), Err(Error::End));
    }

    #[test]
    fn checkpoint_is_persistent_and_old_segments_disappear() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        let large = vec![7; 1024 * 1024];
        for index in 0..70 {
            stream.append(&large, index + 1).unwrap();
        }
        assert_eq!(
            stream.progress().unwrap().unrecyclable_disk,
            stream.progress().unwrap().end_lsn
        );
        let checkpoint = BLOCK_SIZE;
        let progress = stream.set_base_lsn(checkpoint).unwrap();
        assert_eq!(progress.unrecyclable_disk, progress.end_lsn - checkpoint);
        assert!(!segment_path(&directory.0, 0).exists());
        drop(stream);
        let reopened = Stream::open(&path, 0);
        assert_eq!(reopened.progress().unwrap().base_lsn, checkpoint);
        assert_eq!(reopened.base_info(checkpoint).unwrap().curr_lsn, checkpoint);
    }

    #[test]
    fn checkpoint_failure_preserves_segments_and_empty_gc_is_idempotent() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
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
            Stream::open(&path, 0).progress().unwrap().base_lsn,
            BLOCK_SIZE
        );
    }

    #[test]
    fn manifest_reopen_uses_latest_intact_generation() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        stream.append(b"one", 10).unwrap();
        let second = stream.append(b"two", 11).unwrap();
        drop(stream);

        let stream = Stream::open(&path, 0);
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
            3
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

        let stream = Stream::open(&path, 0);
        assert_eq!(stream.status(), Ok(true));
        assert_eq!(stream.progress().unwrap().end_lsn, second.end_lsn);
        let mut cursor = stream.cursor(0, false);
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"one");
        cursor.next(u64::MAX).unwrap();
        assert_eq!(cursor.payload(), b"two");
        assert_eq!(cursor.next(u64::MAX), Err(Error::End));
    }

    #[test]
    fn cursor_owns_current_bytes_but_does_not_pin_old_segments() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
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
        let source = Stream::create(&source_path, base(), 0, 0);
        source.append(b"one", 10).unwrap();
        let mut cursor = source.cursor(0, true);
        cursor.next(u64::MAX).unwrap();
        let bytes = cursor.bytes().to_vec();
        let target_directory = Directory::new();
        let target_path = target_directory.0.to_string_lossy().into_owned();
        let target = Stream::create(&target_path, base(), 0, 0);
        assert_eq!(import(&target, 1, 10, &bytes), Err(Error::Invalid));
        assert_eq!(import(&target, 0, 11, &bytes), Err(Error::Invalid));
        let mut corrupted = bytes.clone();
        let last = corrupted.len() - 1;
        corrupted[last] ^= 1;
        assert_eq!(
            import(&target, 0, 10, &corrupted),
            Err(Error::Corrupt)
        );
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
        let source = Stream::create(&source_path, base(), 0, 0);
        source.append(b"one", 10).unwrap();
        source.append(b"two", 11).unwrap();
        source.append(b"three", 12).unwrap();
        let mut cursor = source.cursor(0, true);
        let mut records = Vec::new();
        while cursor.next(u64::MAX).is_ok() {
            records.push((cursor.lsn(), cursor.scn(), cursor.bytes().to_vec()));
        }
        let groups: Vec<_> = records
            .iter()
            .map(|(lsn, scn, bytes)| ImportedGroup {
                source_lsn: *lsn,
                source_scn: *scn,
                bytes,
            })
            .collect();

        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let target = Stream::create(&path, base(), 0, 0);
        let result = target.append_imported(&groups).unwrap();
        assert_eq!(result.end_lsn, source.progress().unwrap().end_lsn);
        drop(target);
        let target = Stream::open(&path, 0);
        let mut cursor = target.cursor(0, false);
        for payload in [b"one".as_slice(), b"two", b"three"] {
            cursor.next(u64::MAX).unwrap();
            assert_eq!(cursor.payload(), payload);
        }

        let rejected_directory = Directory::new();
        let rejected_path = rejected_directory.0.to_string_lossy().into_owned();
        let rejected = Stream::create(&rejected_path, base(), 0, 0);
        let mut corrupted = records[1].2.clone();
        let last = corrupted.len() - 1;
        corrupted[last] ^= 1;
        assert_eq!(
            rejected.append_imported(&[
                ImportedGroup {
                    source_lsn: records[0].0,
                    source_scn: records[0].1,
                    bytes: &records[0].2,
                },
                ImportedGroup {
                    source_lsn: records[1].0,
                    source_scn: records[1].1,
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
        let stream = Stream::create(&path, base(), 0, 0);
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
        assert_eq!(
            accepted.lsn,
            target_group_lsn + GROUP_HEADER_SIZE as u64
        );
        assert_eq!(accepted.end_lsn, BLOCK_SIZE);
        expected.push((accepted.lsn, accepted.scn, largest));
        let next_segment = stream.append(b"next-segment", 31).unwrap();
        assert_eq!(next_segment.lsn, BLOCK_SIZE + GROUP_HEADER_SIZE as u64);
        expected.push((
            next_segment.lsn,
            next_segment.scn,
            b"next-segment".to_vec(),
        ));

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
            assert_eq!(cursor.next(u64::MAX), Err(Error::End));
        };
        let reopened = Stream::open(&path, 0);
        assert_eq!(reopened.status(), Ok(true));
        verify(&reopened);

        let target_directory = Directory::new();
        let target_path = target_directory.0.to_string_lossy().into_owned();
        let target = Stream::create(&target_path, base(), 0, 0);
        let mut groups = reopened.cursor(0, true);
        for (lsn, scn, _) in &expected {
            groups.next(u64::MAX).unwrap();
            assert_eq!(groups.lsn() + GROUP_HEADER_SIZE as u64, *lsn);
            assert_eq!(groups.scn(), *scn);
            import(&target, groups.lsn(), *scn, groups.bytes()).unwrap();
        }
        assert_eq!(groups.next(u64::MAX), Err(Error::End));
        drop(target);
        let target = Stream::open(&target_path, 0);
        assert_eq!(target.status(), Ok(true));
        verify(&target);
    }

    #[test]
    fn scn_lookup_returns_a_readable_segment_boundary() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
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
        let stream = Stream::create(&path, base(), 0, 0);
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
        assert_eq!(cursor.next(u64::MAX), Err(Error::End));
    }

    #[test]
    fn entry_cursor_honors_replayable_point_within_a_group() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        let mut payload = encode_entry(b"one", 10, false).unwrap();
        payload.extend(encode_entry(b"two", 11, false).unwrap());
        payload.extend(encode_entry(b"three", 12, false).unwrap());
        let group = encode_group(&payload, 0, 12, 1, 0, false).unwrap();
        import(&stream, 0, 12, &group).unwrap();

        let mut cursor = stream.cursor(GROUP_HEADER_SIZE as u64, false);
        cursor.next(10).unwrap();
        assert_eq!(cursor.payload(), b"one");
        assert_eq!(cursor.next(10), Err(Error::End));
        assert_eq!(cursor.next_min_scn(), 11);
        cursor.next(11).unwrap();
        assert_eq!(cursor.payload(), b"two");
        assert_eq!(cursor.next(11), Err(Error::End));
        assert_eq!(cursor.next_min_scn(), 12);
        cursor.next(12).unwrap();
        assert_eq!(cursor.payload(), b"three");
    }

    #[test]
    fn only_group_cursor_returns_padding() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
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
    fn disk_limit_can_be_relieved_without_reopening() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 128);
        assert_eq!(stream.append(b"one", 10), Err(Error::NoSpace));
        stream.set_disk_limit(0).unwrap();
        stream.append(b"one", 10).unwrap();
        stream.set_disk_limit(1).unwrap();
        assert_eq!(stream.append(b"two", 11), Err(Error::NoSpace));
    }

    #[test]
    fn stop_threshold_bounds_writes_and_can_be_raised() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        stream
            .set_disk_policy(DiskPolicy {
                capacity: 16_384,
                stop_percentage: 50,
                throttle_percentage: 100,
                max_throttle_duration_us: 0,
            })
            .unwrap();
        stream.append(&vec![0; 3000], 10).unwrap();
        assert_eq!(stream.append(&vec![0; 3000], 11), Err(Error::NoSpace));
        assert!(stream.progress().unwrap().used_disk <= 8192);
        stream.set_disk_limit(16_384).unwrap();
        stream.append(&vec![0; 3000], 11).unwrap();
    }

    #[test]
    fn concurrent_appends_complete_once_and_survive_reopen() {
        use std::collections::BTreeSet;
        use std::sync::Barrier;

        const WRITERS: usize = 8;
        const WRITES_PER_WRITER: usize = 16;
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Arc::new(Stream::create(&path, base(), 0, 0));
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
        let mut last_lsns = vec![0; WRITERS];
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

        let reopened = Stream::open(&path, 0);
        let mut actual = BTreeSet::new();
        let mut cursor = reopened.cursor(GROUP_HEADER_SIZE as u64, false);
        loop {
            match cursor.next(u64::MAX) {
                Ok(()) => {
                    actual.insert(String::from_utf8(cursor.payload().to_vec()).unwrap());
                }
                Err(Error::End) => break,
                error => panic!("unexpected cursor result: {error:?}"),
            }
        }
        assert_eq!(actual, expected);
    }

    #[test]
    fn close_interrupts_throttled_write_before_io() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Arc::new(Stream::create(&path, base(), 0, 0));
        stream.append(&vec![0; 400], 10).unwrap();
        stream
            .set_disk_policy(DiskPolicy {
                capacity: 1024,
                stop_percentage: 100,
                throttle_percentage: 40,
                max_throttle_duration_us: 5_000_000,
            })
            .unwrap();
        let writer = Arc::clone(&stream);
        let (sender, receiver) = std::sync::mpsc::channel();
        let thread = std::thread::spawn(move || {
            sender.send(writer.append(&vec![0; 400], 11)).unwrap();
        });
        std::thread::sleep(Duration::from_millis(30));
        assert!(matches!(
            receiver.try_recv(),
            Err(std::sync::mpsc::TryRecvError::Empty)
        ));
        stream.close().unwrap();
        assert_eq!(
            receiver.recv_timeout(Duration::from_secs(1)),
            Ok(Err(Error::Closed))
        );
        thread.join().unwrap();
        assert!(matches!(stream.progress(), Err(Error::Closed)));
    }

    #[test]
    fn checkpoints_must_be_segment_boundaries() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
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
