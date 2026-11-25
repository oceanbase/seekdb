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

use super::{
    ffi,
    requests::{self, ServiceHandle},
};
use crate::file_session::FileSession;
use crate::{
    BlockConfig, BlockError, BlockFile, BlockIndex, BlockRegion, FileSize, GrowthPolicy,
    ReleaseOutcome, SpaceBudget, UnmarkedBlockFile,
};
use file_io::bridge::{error_number, legacy_path};
use std::{
    io,
    ops::Range,
    sync::{Mutex, MutexGuard},
};

enum State {
    Empty,
    Marking(UnmarkedBlockFile),
    Ready(BlockFile),
}

pub struct BlockOwner {
    state: Mutex<State>,
    session: FileSession,
}

pub fn new_block_owner() -> Box<BlockOwner> {
    Box::new(BlockOwner {
        state: Mutex::new(State::Empty),
        session: FileSession::default(),
    })
}

fn file_size(bytes: u64, percentage: u8) -> FileSize {
    if bytes == 0 {
        FileSize::DiskPercentage(percentage)
    } else {
        FileSize::Bytes(bytes)
    }
}

fn space_budget(reserved_bytes: u64) -> SpaceBudget {
    SpaceBudget { reserved_bytes }
}

fn growth_policy(policy: &ffi::BlockGrowth) -> GrowthPolicy {
    GrowthPolicy {
        max_bytes: policy.max_bytes,
        next_bytes: policy.next_bytes,
        usage_upper_percent: policy.usage_upper_percent,
    }
}

fn code_status(code: ffi::BlockCode) -> ffi::BlockStatus {
    ffi::BlockStatus {
        code,
        error: 0,
        os_error: 0,
    }
}

fn failure(error: BlockError) -> ffi::BlockStatus {
    if let BlockError::CreationCleanup { primary, .. } = error {
        return failure(*primary);
    }
    let code = match &error {
        BlockError::InvalidInput(_) | BlockError::InvalidBlock(_) => {
            ffi::BlockCode::InvalidArgument
        }
        BlockError::InvalidHeader => ffi::BlockCode::InvalidHeader,
        BlockError::ChecksumMismatch => ffi::BlockCode::ChecksumMismatch,
        BlockError::NotAllocated(_) => ffi::BlockCode::NotAllocated,
        BlockError::MarkingAborted => ffi::BlockCode::MarkingAborted,
        BlockError::Exhausted => ffi::BlockCode::Exhausted,
        BlockError::Io(_) | BlockError::DirectorySync { .. } => ffi::BlockCode::IoError,
        BlockError::Memory(_) => ffi::BlockCode::Memory,
        BlockError::CreationCleanup { .. } => unreachable!(),
    };
    let mut status = code_status(code);
    let primary = match &error {
        BlockError::Io(error) => Some(error),
        BlockError::DirectorySync { source, .. } => Some(source.primary()),
        _ => None,
    };
    if let Some(error) = primary {
        let detail = file_io::bridge::fs_error(error);
        status.os_error = detail.native_code;
        status.error = detail.error;
    }
    status
}
fn result_status(result: Result<(), BlockError>) -> ffi::BlockStatus {
    result.map_or_else(failure, |()| code_status(ffi::BlockCode::Ok))
}
fn open_result(
    result: Result<ffi::BlockStatus, BlockError>,
    summary: &file_io::DirectorySyncSummary,
) -> ffi::BlockOpenResult {
    use file_io::bridge::{directory_summary, fs_error, no_error, path_bytes, sync_result};
    let mut opened = ffi::BlockOpenResult {
        status: code_status(ffi::BlockCode::Ok),
        primary_error: no_error(),
        sync: sync_result(None),
        sync_path: Vec::new(),
        cleanup_close: no_error(),
        cleanup_remove: no_error(),
        cleanup_sync: sync_result(None),
        directory_summary: directory_summary(summary),
    };
    let error = match result {
        Ok(status) => {
            opened.status = status;
            return opened;
        }
        Err(error) => error,
    };
    let primary = match error {
        BlockError::CreationCleanup { primary, cleanup } => {
            if let Some(error) = cleanup.close {
                opened.cleanup_close = fs_error(&error);
            }
            if let Some(error) = cleanup.remove {
                opened.cleanup_remove = fs_error(&error);
            }
            opened.cleanup_sync = sync_result(cleanup.sync.as_ref());
            *primary
        }
        error => error,
    };
    match &primary {
        BlockError::Io(error) => opened.primary_error = fs_error(error),
        BlockError::DirectorySync { path, source } => {
            opened.primary_error = fs_error(source.primary());
            opened.sync = sync_result(Some(source));
            opened.sync_path = path_bytes(path);
        }
        _ => (),
    }
    opened.status = failure(primary);
    opened
}

fn release_result(result: Result<ReleaseOutcome, ffi::BlockStatus>) -> ffi::BlockRelease {
    let mut release = ffi::BlockRelease {
        status: code_status(ffi::BlockCode::Ok),
        pending: false,
        reclamation_error: 0,
        reclamation_os_error: 0,
    };
    match result {
        Ok(ReleaseOutcome::Pending) => release.pending = true,
        Ok(ReleaseOutcome::Reusable {
            reclamation_error: Some(error),
        }) => {
            release.reclamation_os_error = error.raw_os_error().unwrap_or(0);
            release.reclamation_error = error_number(error);
        }
        Ok(ReleaseOutcome::Reusable {
            reclamation_error: None,
        }) => (),
        Err(status) => release.status = status,
    }
    release
}

impl BlockOwner {
    fn lock(&self) -> MutexGuard<'_, State> {
        self.state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    pub fn open_blocks(&self, path: &[u8], options: &ffi::BlockOptions) -> ffi::BlockOpenResult {
        self.session
            .capture(|endpoint| {
                let mut summary = file_io::DirectorySyncSummary::default();
                let mut state = self.lock();
                if !matches!(*state, State::Empty) {
                    return Ok(open_result(
                        Ok(code_status(ffi::BlockCode::AlreadyOpen)),
                        &summary,
                    ));
                }
                Ok(open_result(
                    (|| {
                        let file = UnmarkedBlockFile::open(
                            &legacy_path(path)?,
                            BlockConfig {
                                block_size: options.block_size,
                                initial_size: file_size(
                                    options.initial_size,
                                    options.initial_percentage,
                                ),
                                direct_io: endpoint.direct_io(),
                                punch_holes: options.punch_holes,
                            },
                            SpaceBudget {
                                reserved_bytes: options.reserved_bytes,
                            },
                            file_io::bridge::application_durability(),
                            &mut summary,
                        )?;
                        *state = State::Marking(file);
                        Ok(code_status(ffi::BlockCode::Ok))
                    })(),
                    &summary,
                ))
            })
            .unwrap_or_else(|error| {
                open_result(
                    Err(BlockError::Io(error)),
                    &file_io::DirectorySyncSummary::default(),
                )
            })
    }

    pub fn prepare_blocks(
        &self,
        service: &ServiceHandle,
        directory: &[u8],
        execution: &ffi::ExecutionOptions,
    ) -> i32 {
        requests::prepare_session(&self.session, service, directory, execution)
    }
    pub fn close_blocks(&self) -> i32 {
        requests::status(self.session.close(|| *self.lock() = State::Empty))
    }
    pub fn validate_block_workers(&self, configured: usize) -> i32 {
        requests::status(self.session.validate_workers(configured))
    }
    pub fn submit_block(
        &self,
        index: u64,
        options: &ffi::RequestOptions,
        bytes: &[u8],
        callback: cxx::UniquePtr<ffi::CallbackAdapter>,
    ) -> Box<crate::Request> {
        self.submit_block_request(index, options, bytes, requests::take_callback(callback))
    }
    pub(super) fn submit_block_request(
        &self,
        index: u64,
        options: &ffi::RequestOptions,
        bytes: &[u8],
        callback: Option<crate::service::CallbackOwner>,
    ) -> Box<crate::Request> {
        let captured = self.session.capture(|endpoint| {
            let plan = requests::logical_plan(options)?;
            let target = checked_region(self, index, plan.offset, plan.length)?;
            Ok((
                std::sync::Arc::clone(endpoint),
                plan,
                crate::Target::Block(target),
            ))
        });
        match captured {
            Ok((endpoint, plan, target)) => {
                requests::submit_owned(&endpoint, target, &plan, options, bytes, callback)
            }
            Err(error) => requests::reject(error, callback),
        }
    }

    pub fn mark_blocks(&self, indices: &[u64]) -> ffi::BlockStatus {
        let mut state = self.lock();
        let file = match &mut *state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Ready(_) => {
                return failure(BlockError::InvalidInput("marking already finished"));
            }
            State::Marking(file) => file,
        };
        let result = (|| {
            file.check_marking()?;
            let mut used = Vec::new();
            used.try_reserve_exact(indices.len())?;
            for &index in indices {
                used.push(BlockIndex::new(index)?);
            }
            file.mark(&used)
        })();
        if result.is_err() {
            file.abort_marking();
        }
        result_status(result)
    }

    pub fn restart_marking(&self) -> ffi::BlockStatus {
        let mut state = self.lock();
        match &mut *state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Ready(_) => {
                return failure(BlockError::InvalidInput("marking already finished"));
            }
            State::Marking(file) => file.restart_marking(),
        }
        code_status(ffi::BlockCode::Ok)
    }

    pub fn finish_marking(&self) -> ffi::BlockStatus {
        let mut state = self.lock();
        match &*state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Ready(_) => {
                return failure(BlockError::InvalidInput("marking already finished"));
            }
            State::Marking(file) => {
                if let Err(error) = file.check_marking() {
                    return failure(error);
                }
            }
        }
        let State::Marking(file) = std::mem::replace(&mut *state, State::Empty) else {
            unreachable!()
        };
        match file.finish_marking() {
            Ok(file) => {
                *state = State::Ready(file);
                code_status(ffi::BlockCode::Ok)
            }
            Err(error) => failure(error),
        }
    }

    fn with_ready<T>(
        &self,
        action: impl FnOnce(&BlockFile) -> Result<T, BlockError>,
    ) -> Result<T, ffi::BlockStatus> {
        let state = self.lock();
        match &*state {
            State::Empty => Err(code_status(ffi::BlockCode::NotOpen)),
            State::Marking(_) => Err(failure(BlockError::InvalidInput("marking not finished"))),
            State::Ready(file) => action(file).map_err(failure),
        }
    }

    pub fn allocate_block(&self, policy: &ffi::BlockGrowth, out: &mut u64) -> ffi::BlockStatus {
        match self.with_ready(|file| {
            file.allocate_with_growth(growth_policy(policy), space_budget(policy.reserved_bytes))
        }) {
            Ok(index) => {
                *out = index.get();
                code_status(ffi::BlockCode::Ok)
            }
            Err(status) => status,
        }
    }

    pub fn release_block(&self, index: u64) -> ffi::BlockRelease {
        let state = self.lock();
        let file = match &*state {
            State::Empty => return release_result(Err(code_status(ffi::BlockCode::NotOpen))),
            State::Marking(_) => {
                return release_result(Err(failure(BlockError::InvalidInput(
                    "marking not finished",
                ))));
            }
            State::Ready(file) => file,
        };
        release_result(
            BlockIndex::new(index)
                .and_then(|index| file.release(index))
                .map_err(failure),
        )
    }

    pub fn sync_blocks(&self) -> ffi::BlockStatus {
        let state = self.lock();
        result_status(match &*state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Marking(file) => file.sync(),
            State::Ready(file) => file.sync(),
        })
    }

    pub fn extend_blocks(&self, policy: &ffi::BlockGrowth) -> ffi::BlockStatus {
        self.with_ready(|file| {
            file.extend_if_needed(growth_policy(policy), space_budget(policy.reserved_bytes))
        })
        .map_or_else(|status| status, |()| code_status(ffi::BlockCode::Ok))
    }

    pub fn resize_blocks(&self, bytes: u64, percentage: u8, reserved: u64) -> ffi::BlockStatus {
        self.with_ready(|file| file.resize(file_size(bytes, percentage), space_budget(reserved)))
            .map_or_else(|status| status, |()| code_status(ffi::BlockCode::Ok))
    }

    pub fn maximum_block_capacity(
        &self,
        max_bytes: u64,
        reserved: u64,
        out: &mut u64,
    ) -> ffi::BlockStatus {
        let state = self.lock();
        *out = match &*state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Marking(file) => file.maximum_capacity(max_bytes, space_budget(reserved)),
            State::Ready(file) => file.maximum_capacity(max_bytes, space_budget(reserved)),
        };
        code_status(ffi::BlockCode::Ok)
    }

    pub fn check_block_space(
        &self,
        required: u64,
        max_bytes: u64,
        reserved: u64,
        limit: u8,
    ) -> ffi::BlockStatus {
        self.with_ready(|file| file.check_usage(required, max_bytes, space_budget(reserved), limit))
            .map_or_else(|status| status, |_| code_status(ffi::BlockCode::Ok))
    }

    pub fn block_stats(&self, stats: &mut ffi::BlockStats) -> ffi::BlockStatus {
        let state = self.lock();
        let snapshot = match &*state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Marking(file) => file.snapshot(),
            State::Ready(file) => file.snapshot(),
        };
        *stats = ffi::BlockStats {
            block_size: snapshot.block_size,
            capacity: snapshot.capacity,
            total_blocks: snapshot.total_blocks,
            free_blocks: snapshot.free_blocks,
            pending_release_blocks: snapshot.pending_release_blocks,
            reclamation_failures: snapshot.reclamation_failures,
        };
        code_status(ffi::BlockCode::Ok)
    }

    fn region(&self, index: BlockIndex, range: Range<u64>) -> io::Result<BlockRegion> {
        let state = self.lock();
        match &*state {
            State::Empty => return Err(io::ErrorKind::NotConnected.into()),
            State::Marking(file) => file.region(index, range),
            State::Ready(file) => file.region(index, range),
        }
        .map_err(|error| match error {
            BlockError::Io(error) => error,
            error => io::Error::new(io::ErrorKind::InvalidInput, error),
        })
    }
}

pub(super) fn checked_region(
    owner: &BlockOwner,
    index: u64,
    offset: u64,
    length: usize,
) -> io::Result<BlockRegion> {
    let end = offset
        .checked_add(length as u64)
        .ok_or(io::ErrorKind::InvalidInput)?;
    let index = BlockIndex::new(index)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidInput, error))?;
    owner.region(index, offset..end)
}
