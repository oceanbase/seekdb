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
    block_id, block_offset, crc64, crc64_with_seed, decode_group_header, encode_entry,
    encode_group, encode_segment_header, group_entries, physical_offset, segment_path,
    validate_group, validate_segment_header, Error, GroupHeader, Result, BLOCK_SIZE,
    ENTRY_HEADER_SIZE, GROUP_HEADER_SIZE, MAX_LOG_BODY_SIZE, PHYSICAL_BLOCK_SIZE,
    SEGMENT_HEADER_SIZE,
};
use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, MutexGuard};

const MANIFEST_FILE: &str = "manifest";
const MANIFEST_SIZE: usize = 128;
const MANIFEST_MAGIC: &[u8; 8] = b"PALFMAN2";

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

struct State {
    directory: PathBuf,
    manifest: Manifest,
    records: BTreeMap<u64, RecordMeta>,
    segment_lengths: BTreeMap<u64, u64>,
    end_lsn: u64,
    end_scn: u64,
    next_log_id: i64,
    accumulated_checksum: u64,
    disk_limit: u64,
    integrity: bool,
    closed: bool,
    failed: Option<Error>,
}

struct StreamInner {
    state: Mutex<State>,
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
            disk_limit: 0,
            integrity: false,
            closed: false,
            failed: Some(error),
        });
        Self {
            inner: Arc::new(StreamInner {
                state: Mutex::new(state),
            }),
        }
    }

    pub fn create(path: &str, base: BaseInfo, ref_scn: u64, disk_limit: u64) -> Self {
        Self::from_result(create_state(Path::new(path), base, ref_scn, disk_limit))
    }

    pub fn open(path: &str, disk_limit: u64) -> Self {
        Self::from_result(open_state(Path::new(path), disk_limit))
    }

    fn lock(&self) -> Result<MutexGuard<'_, State>> {
        self.inner.state.lock().map_err(|_| Error::Io)
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
        let mut state = self.lock()?;
        state.check_writable()?;
        let scn = ref_scn.max(state.end_scn.saturating_add(1));
        let entry = encode_entry(payload, scn, false);
        let group_len = GROUP_HEADER_SIZE + entry.len();
        let mut lsn = state.end_lsn;
        let remaining = BLOCK_SIZE - block_offset(lsn);
        if remaining < group_len as u64 {
            if let Err(error) = state.write_padding(lsn, remaining, scn) {
                state.failed = Some(error);
                return Err(error);
            }
            lsn = state.end_lsn;
        }
        let group = encode_group(
            &entry,
            lsn,
            scn,
            state.next_log_id,
            state.accumulated_checksum,
            false,
        )?;
        state.ensure_capacity(group.len() as u64)?;
        if let Err(error) = state.write_record(lsn, &group) {
            state.failed = Some(error);
            return Err(error);
        }
        let header = validate_group(&group, lsn)?;
        let meta = RecordMeta {
            lsn,
            len: group.len() as u64,
            header: header.clone(),
        };
        if let Err(error) = state.commit_record(meta) {
            state.failed = Some(error);
            return Err(error);
        }
        Ok(AppendResult {
            lsn: lsn + GROUP_HEADER_SIZE as u64,
            scn,
            end_lsn: state.end_lsn,
            end_scn: state.end_scn,
        })
    }

    pub fn append_imported(
        &self,
        source_lsn: u64,
        source_scn: u64,
        bytes: &[u8],
    ) -> Result<AppendResult> {
        let mut state = self.lock()?;
        state.check_writable()?;
        if source_lsn < state.end_lsn {
            return Err(Error::Duplicate);
        }
        if source_lsn != state.end_lsn {
            return Err(Error::Invalid);
        }
        let header = validate_group(bytes, source_lsn)?;
        if header.max_scn != source_scn
            || header.committed_end_lsn != source_lsn
            || header.log_id != state.next_log_id
            || header.accumulated_checksum
                != crc64_with_seed(
                    state.accumulated_checksum,
                    &header.data_checksum.to_le_bytes(),
                )
        {
            return Err(Error::Invalid);
        }
        state.ensure_capacity(bytes.len() as u64)?;
        if let Err(error) = state.write_record(source_lsn, bytes) {
            state.failed = Some(error);
            return Err(error);
        }
        let meta = RecordMeta {
            lsn: source_lsn,
            len: bytes.len() as u64,
            header: header.clone(),
        };
        if let Err(error) = state.commit_record(meta) {
            state.failed = Some(error);
            return Err(error);
        }
        Ok(AppendResult {
            lsn: source_lsn,
            scn: source_scn,
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
        let current_block = block_id(state.end_lsn);
        let unrecyclable_disk = state
            .segment_lengths
            .get(&current_block)
            .copied()
            .unwrap_or(0);
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
        write_manifest(&state.directory, &manifest)?;
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
        self.progress()
    }

    pub fn set_disk_limit(&self, disk_limit: u64) -> Result<()> {
        let mut state = self.lock()?;
        state.check_open()?;
        state.disk_limit = disk_limit;
        Ok(())
    }

    pub fn locate_by_scn(&self, scn: u64) -> Result<u64> {
        let state = self.lock()?;
        state.check_open()?;
        state
            .records
            .iter()
            .filter(|(_, record)| record.header.max_scn <= scn)
            .next_back()
            .map(|(lsn, _)| *lsn)
            .ok_or(Error::End)
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

    pub fn raw_read(&self, lsn: u64, length: usize) -> Result<Vec<u8>> {
        let state = self.lock()?;
        state.check_open()?;
        if lsn < state.manifest.base_lsn
            || length == 0
            || lsn.saturating_add(length as u64) > state.end_lsn
        {
            return Err(Error::Invalid);
        }
        if block_id(lsn) != block_id(lsn + length as u64 - 1) {
            return Err(Error::Invalid);
        }
        let mut file = File::open(segment_path(&state.directory, block_id(lsn)))?;
        file.seek(SeekFrom::Start(physical_offset(lsn)))?;
        let mut bytes = vec![0; length];
        file.read_exact(&mut bytes)?;
        Ok(bytes)
    }

    pub fn close(&self) -> Result<()> {
        let mut state = self.lock()?;
        state.closed = true;
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

    fn check_writable(&self) -> Result<()> {
        self.check_open()
    }

    fn ensure_capacity(&self, additional: u64) -> Result<()> {
        if self.disk_limit == 0 {
            return Ok(());
        }
        let used: u64 = self.segment_lengths.values().sum();
        let id = block_id(self.end_lsn);
        let segment_exists = self.segment_lengths.contains_key(&id);
        let required = additional
            + if segment_exists {
                0
            } else {
                SEGMENT_HEADER_SIZE
            };
        if used.saturating_add(required) > self.disk_limit {
            Err(Error::NoSpace)
        } else {
            Ok(())
        }
    }

    fn write_padding(&mut self, lsn: u64, remaining: u64, scn: u64) -> Result<()> {
        if remaining == 0 {
            return Ok(());
        }
        if remaining < (GROUP_HEADER_SIZE + ENTRY_HEADER_SIZE) as u64 {
            self.ensure_capacity(remaining)?;
            let path = segment_path(&self.directory, block_id(lsn));
            let mut file = OpenOptions::new().write(true).open(path)?;
            file.seek(SeekFrom::Start(physical_offset(lsn)))?;
            file.write_all(&vec![0; remaining as usize])?;
            file.sync_data()?;
            self.segment_lengths
                .insert(block_id(lsn), file.metadata()?.len());
            self.end_lsn = (block_id(lsn) + 1) * BLOCK_SIZE;
            return Ok(());
        }
        let entry_payload_len = remaining as usize - GROUP_HEADER_SIZE - ENTRY_HEADER_SIZE;
        let entry = encode_entry(&vec![0; entry_payload_len], scn, true);
        let group = encode_group(
            &entry,
            lsn,
            scn,
            self.next_log_id,
            self.accumulated_checksum,
            true,
        )?;
        self.ensure_capacity(group.len() as u64)?;
        self.write_record(lsn, &group)?;
        let header = validate_group(&group, lsn)?;
        let meta = RecordMeta {
            lsn,
            len: remaining,
            header: header.clone(),
        };
        self.records.insert(lsn, meta);
        self.end_lsn = lsn + remaining;
        self.accumulated_checksum = header.accumulated_checksum;
        self.next_log_id += 1;
        Ok(())
    }

    fn write_record(&mut self, lsn: u64, bytes: &[u8]) -> Result<()> {
        let id = block_id(lsn);
        let path = segment_path(&self.directory, id);
        let new_segment = !self.segment_lengths.contains_key(&id);
        let mut file = OpenOptions::new()
            .create(true)
            .read(true)
            .write(true)
            .open(&path)?;
        if new_segment {
            file.write_all(&encode_segment_header(id))?;
        }
        file.seek(SeekFrom::Start(physical_offset(lsn)))?;
        file.write_all(bytes)?;
        file.sync_data()?;
        if new_segment {
            sync_directory(&self.directory)?;
        }
        let length = file.metadata()?.len();
        self.segment_lengths.insert(id, length);
        Ok(())
    }

    fn commit_record(&mut self, meta: RecordMeta) -> Result<()> {
        let next_log_id = self.next_log_id.checked_add(1).ok_or(Error::Invalid)?;
        let mut manifest = self.manifest.clone();
        manifest.committed = CommittedTail {
            end_lsn: meta.end_lsn(),
            end_scn: meta.header.max_scn,
            log_id: meta.header.log_id,
            accumulated_checksum: meta.header.accumulated_checksum,
        };
        write_manifest(&self.directory, &manifest)?;
        self.manifest = manifest;
        self.end_lsn = meta.end_lsn();
        self.end_scn = meta.header.max_scn;
        self.next_log_id = next_log_id;
        self.accumulated_checksum = meta.header.accumulated_checksum;
        self.records.insert(meta.lsn, meta);
        Ok(())
    }

    fn read_record(&self, lsn: u64) -> Result<Vec<u8>> {
        let meta = self.records.get(&lsn).ok_or(Error::OutLower)?;
        let mut file = File::open(segment_path(&self.directory, block_id(lsn)))?;
        file.seek(SeekFrom::Start(physical_offset(lsn)))?;
        let mut bytes = vec![0; meta.len as usize];
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

fn create_state(directory: &Path, base: BaseInfo, ref_scn: u64, disk_limit: u64) -> Result<State> {
    if base.curr_lsn < base.prev_lsn || ref_scn == u64::MAX {
        return Err(Error::Invalid);
    }
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
    write_manifest(directory, &manifest)?;
    sync_directory(directory)?;
    Ok(State {
        directory: directory.to_path_buf(),
        end_lsn: base.curr_lsn,
        end_scn: base.prev_scn,
        next_log_id: base.prev_log_id.checked_add(1).ok_or(Error::Invalid)?,
        accumulated_checksum: base.prev_accumulated_checksum,
        manifest,
        records: BTreeMap::new(),
        segment_lengths: BTreeMap::new(),
        disk_limit,
        integrity: true,
        closed: false,
        failed: None,
    })
}

fn open_state(directory: &Path, disk_limit: u64) -> Result<State> {
    let manifest = read_manifest(directory)?;
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
        if length < SEGMENT_HEADER_SIZE || length > PHYSICAL_BLOCK_SIZE {
            return Err(Error::Corrupt);
        }
        let mut segment_header = vec![0; SEGMENT_HEADER_SIZE as usize];
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
                let mut padding = vec![0; remaining as usize];
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
            let mut header_bytes = vec![0; GROUP_HEADER_SIZE];
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
            let mut bytes = vec![0; record_len as usize];
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
        manifest,
        records,
        segment_lengths,
        end_lsn,
        end_scn,
        next_log_id,
        accumulated_checksum,
        disk_limit,
        integrity: true,
        closed: false,
        failed: None,
    })
}

fn write_manifest(directory: &Path, manifest: &Manifest) -> Result<()> {
    let mut bytes = vec![0; MANIFEST_SIZE];
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
    let checksum = crc64(&bytes[..88]);
    bytes[88..96].copy_from_slice(&checksum.to_le_bytes());
    let temporary = directory.join("manifest.tmp");
    let mut file = File::create(&temporary)?;
    file.write_all(&bytes)?;
    file.sync_all()?;
    fs::rename(temporary, directory.join(MANIFEST_FILE))?;
    sync_directory(directory)?;
    Ok(())
}

fn read_manifest(directory: &Path) -> Result<Manifest> {
    let mut bytes = Vec::new();
    File::open(directory.join(MANIFEST_FILE))?.read_to_end(&mut bytes)?;
    if bytes.len() != MANIFEST_SIZE
        || bytes.get(..8) != Some(MANIFEST_MAGIC)
        || crc64(&bytes[..88]) != u64::from_le_bytes(bytes[88..96].try_into().unwrap())
    {
        return Err(Error::Corrupt);
    }
    let number = |offset: usize| u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap());
    let signed = |offset: usize| i64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap());
    let base_lsn = number(8);
    let previous = BaseInfo {
        curr_lsn: base_lsn,
        prev_lsn: number(16),
        prev_scn: number(24),
        prev_log_id: signed(32),
        prev_accumulated_checksum: number(40),
    };
    Ok(Manifest {
        base_lsn,
        previous,
        ref_scn: number(48),
        committed: CommittedTail {
            end_lsn: number(56),
            end_scn: number(64),
            log_id: signed(72),
            accumulated_checksum: number(80),
        },
    })
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
            let entry = encode_entry(b"unconfirmed", 11, false);
            let group = encode_group(
                &entry,
                lsn,
                11,
                state.next_log_id,
                state.accumulated_checksum,
                false,
            )
            .unwrap();
            state.write_record(lsn, &group).unwrap();
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
        let entry = encode_entry(&vec![7; payload_len], 10, false);
        let group = encode_group(&entry, 0, 10, 1, 0, false).unwrap();
        stream.append_imported(0, 10, &group).unwrap();
        let appended = stream.append(b"next", 11).unwrap();
        assert_eq!(block_id(appended.lsn), 1);
        drop(stream);

        let reopened = Stream::open(&path, 0);
        assert_eq!(reopened.status(), Ok(true));
        let mut cursor = reopened.cursor(appended.lsn, false);
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
        let checkpoint = BLOCK_SIZE;
        stream.set_base_lsn(checkpoint).unwrap();
        assert!(!segment_path(&directory.0, 0).exists());
        drop(stream);
        let reopened = Stream::open(&path, 0);
        assert_eq!(reopened.progress().unwrap().base_lsn, checkpoint);
        assert_eq!(reopened.base_info(checkpoint).unwrap().curr_lsn, checkpoint);
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
        assert_eq!(target.append_imported(1, 10, &bytes), Err(Error::Invalid));
        target.append_imported(0, 10, &bytes).unwrap();
        assert_eq!(target.append_imported(0, 10, &bytes), Err(Error::Duplicate));
    }

    #[test]
    fn imported_groups_preserve_every_entry_position() {
        let directory = Directory::new();
        let path = directory.0.to_string_lossy().into_owned();
        let stream = Stream::create(&path, base(), 0, 0);
        let mut payload = encode_entry(b"one", 10, false);
        let first_entry_len = payload.len() as u64;
        payload.extend(encode_entry(b"two", 10, false));
        payload.extend(encode_entry(b"three", 11, false));
        let group = encode_group(&payload, 0, 11, 1, 0, false).unwrap();
        stream.append_imported(0, 11, &group).unwrap();

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
        let mut payload = encode_entry(b"one", 10, false);
        payload.extend(encode_entry(b"two", 11, false));
        payload.extend(encode_entry(b"three", 12, false));
        let group = encode_group(&payload, 0, 12, 1, 0, false).unwrap();
        stream.append_imported(0, 12, &group).unwrap();

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
        let entry = encode_entry(&[0; 8], 10, true);
        let group = encode_group(&entry, 0, 10, 1, 0, true).unwrap();
        stream.append_imported(0, 10, &group).unwrap();
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
