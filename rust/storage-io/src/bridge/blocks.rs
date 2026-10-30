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

use super::{ffi, requests::RequestSlot};
use crate::{
    BlockConfig, BlockError, BlockFile, BlockIndex, BlockRegion, ReleaseOutcome, SpaceBudget,
    UnmarkedBlockFile,
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

pub struct BlockOwner(Mutex<State>);

pub fn new_block_owner() -> Box<BlockOwner> {
    Box::new(BlockOwner(Mutex::new(State::Empty)))
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
        self.0
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    pub fn open_blocks(&self, path: &[u8], options: &ffi::BlockOptions) -> ffi::BlockOpenResult {
        let mut summary = file_io::DirectorySyncSummary::default();
        let mut state = self.lock();
        if !matches!(*state, State::Empty) {
            return open_result(Ok(code_status(ffi::BlockCode::AlreadyOpen)), &summary);
        }
        open_result(
            (|| {
                let path = legacy_path(path)?;
                let file = UnmarkedBlockFile::open(
                    &path,
                    BlockConfig {
                        block_size: options.block_size,
                        initial_size: options.initial_size,
                        direct_io: options.direct_io,
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
        )
    }

    pub fn close_blocks(&self) {
        let mut state = self.lock();
        *state = State::Empty;
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

    pub fn allocate_blocks(&self, count: usize, out: &mut Vec<u64>) -> ffi::BlockStatus {
        out.clear();
        let state = self.lock();
        let file = match &*state {
            State::Empty => return code_status(ffi::BlockCode::NotOpen),
            State::Marking(_) => return failure(BlockError::InvalidInput("marking not finished")),
            State::Ready(file) => file,
        };
        result_status((|| {
            let capacity = usize::try_from(file.snapshot().total_blocks).unwrap_or(usize::MAX);
            out.try_reserve(count.min(capacity))?;
            out.extend(file.allocate_many(count)?.into_iter().map(BlockIndex::get));
            Ok(())
        })())
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

    pub fn grow_blocks(&self, bytes: u64, reserved: u64) -> ffi::BlockStatus {
        let state = self.lock();
        match &*state {
            State::Empty => code_status(ffi::BlockCode::NotOpen),
            State::Marking(_) => failure(BlockError::InvalidInput("marking not finished")),
            State::Ready(file) => result_status(file.grow(
                bytes,
                SpaceBudget {
                    reserved_bytes: reserved,
                },
            )),
        }
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

pub fn bind_block_target(
    owner: &BlockOwner,
    index: u64,
    slot: &mut RequestSlot,
    lifetime: &mut crate::RequestLifetime,
) -> i32 {
    let target = slot.geometry(lifetime).and_then(|(offset, length)| {
        checked_region(owner, index, offset, length).map(crate::Target::Block)
    });
    slot.bind_target(target, lifetime)
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

pub fn read_block(owner: &BlockOwner, index: u64, offset: u64, bytes: &mut [u8]) -> ffi::IoResult {
    let transfer = match checked_region(owner, index, offset, bytes.len()) {
        Ok(region) => region.read_at(bytes, 0),
        Err(error) => file_io::Transfer {
            bytes: 0,
            result: Err(error),
        },
    };
    ffi::IoResult {
        bytes: transfer.bytes,
        error: transfer.result.err().map_or(0, error_number),
    }
}

pub fn write_block(owner: &BlockOwner, index: u64, offset: u64, bytes: &[u8]) -> ffi::IoResult {
    let transfer = match checked_region(owner, index, offset, bytes.len()) {
        Ok(region) => region.write_at(bytes, 0),
        Err(error) => file_io::Transfer {
            bytes: 0,
            result: Err(error),
        },
    };
    ffi::IoResult {
        bytes: transfer.bytes,
        error: transfer.result.err().map_or(0, error_number),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn open_result_preserves_typed_primary_and_cleanup() {
        let result = open_result(
            Err(BlockError::CreationCleanup {
                primary: Box::new(BlockError::InvalidHeader),
                cleanup: crate::CreationCleanupFailure {
                    close: Some(io::Error::from_raw_os_error(5)),
                    remove: Some(io::Error::from_raw_os_error(13)),
                    sync: None,
                },
            }),
            &file_io::DirectorySyncSummary::default(),
        );
        assert!(result.status.code == ffi::BlockCode::InvalidHeader);
        assert_eq!(result.cleanup_close.native_code, 5);
        assert_eq!(result.cleanup_remove.native_code, 13);
        assert!(result.cleanup_sync.stage == file_io::bridge::ffi::SyncStage::Complete);
        assert_eq!(result.directory_summary.count, 0);
    }

    #[test]
    fn reclamation_failure_is_reported_without_rejecting_the_release() {
        let released = release_result(Ok(ReleaseOutcome::Reusable {
            reclamation_error: Some(io::Error::from_raw_os_error(5)),
        }));
        assert!(released.status.code == ffi::BlockCode::Ok);
        assert!(!released.pending);
        assert_ne!(released.reclamation_error, 0);
        assert_eq!(released.reclamation_os_error, 5);
        let rejected = release_result(Err(failure(BlockError::NotAllocated(1))));
        assert!(rejected.status.code == ffi::BlockCode::NotAllocated);
        assert_eq!(rejected.reclamation_error, 0);
    }
}
