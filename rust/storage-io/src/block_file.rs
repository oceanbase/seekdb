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

use crate::{encode_block_header, validate_block_header, BlockError, HEADER_PAGE_SIZE};
use file_io::{allocate, AlignedBuffer, Allocation, ExclusiveFile, Transfer};
use std::collections::VecDeque;
use std::fs::File;
use std::io;
use std::ops::Range;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, MutexGuard};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BlockIndex(u64);

impl BlockIndex {
    pub fn new(index: u64) -> Result<Self, BlockError> {
        if index == 0 || index > i64::MAX as u64 {
            return Err(BlockError::InvalidBlock(index));
        }
        Ok(Self(index))
    }

    pub fn get(self) -> u64 {
        self.0
    }
}

#[derive(Clone, Copy, Debug)]
pub struct BlockConfig {
    pub block_size: u64,
    pub initial_size: u64,
    pub direct_io: bool,
    pub punch_holes: bool,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct SpaceBudget {
    pub reserved_bytes: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BlockSnapshot {
    pub block_size: u64,
    pub capacity: u64,
    pub total_blocks: u64,
    pub free_blocks: u64,
    pub pending_release_blocks: u64,
    pub reclamation_failures: u64,
}

#[derive(Debug)]
pub enum ReleaseOutcome {
    Pending,
    Reusable {
        reclamation_error: Option<io::Error>,
    },
}

enum Slot {
    Header,
    Unmarked { live: bool, leases: usize },
    Allocated { leases: usize },
    Pending { leases: usize },
    Free,
}

struct State {
    slots: Vec<Slot>,
    free: VecDeque<usize>,
    retired: VecDeque<usize>,
    punch_holes: bool,
    reclamation_failures: u64,
}

struct Inner {
    file: Arc<ExclusiveFile>,
    path: PathBuf,
    block_size: u64,
    state: Mutex<State>,
}

pub struct UnmarkedBlockFile {
    inner: Arc<Inner>,
    marking_valid: bool,
}

pub struct BlockFile {
    inner: Arc<Inner>,
}

struct RegionLease {
    inner: Arc<Inner>,
    index: usize,
    #[cfg(any(test, feature = "ownership-test-support"))]
    drop_witness: crate::drop_witness::DropRegistration,
}

/// Keeps a block unavailable for reuse until all regions sharing its lease retire.
#[derive(Clone)]
pub struct BlockRegion {
    lease: Arc<RegionLease>,
    start: u64,
    length: u64,
}

fn block_count(bytes: u64, block_size: u64) -> Result<usize, BlockError> {
    if bytes <= block_size || bytes > i64::MAX as u64 || !bytes.is_multiple_of(block_size) {
        return Err(BlockError::InvalidInput(
            "invalid physical block file length",
        ));
    }
    usize::try_from(bytes / block_size)
        .map_err(|_| BlockError::InvalidInput("physical block count exceeds address space"))
}

fn check_space(path: &Path, additional: u64, budget: SpaceBudget) -> Result<(), BlockError> {
    if additional
        > file_io::space(path)?
            .available
            .saturating_sub(budget.reserved_bytes)
    {
        return Err(BlockError::Exhausted);
    }
    Ok(())
}

struct CreatedEntry {
    parent: file_io::Directory,
    name: std::ffi::OsString,
    identity: Option<file_io::FileIdentity>,
}

enum InitialFile {
    Unlocked(File),
    Locked(ExclusiveFile),
}
impl InitialFile {
    fn close(self) -> io::Result<()> {
        match self {
            Self::Unlocked(file) => file_io::close_file(file),
            Self::Locked(file) => file.close(),
        }
    }
}

impl UnmarkedBlockFile {
    /// The caller must control the pathname and its ancestors during creation,
    /// including the working directory for relative paths. The file lock does
    /// not prevent another actor from replacing the directory entry.
    pub fn open(
        path: &Path,
        config: BlockConfig,
        budget: SpaceBudget,
        durability: file_io::DirectoryDurability,
        summary: &mut file_io::DirectorySyncSummary,
    ) -> Result<Self, BlockError> {
        Self::open_with(
            path,
            config,
            budget,
            durability,
            summary,
            file_io::extend_file,
            |parent, name, identity| parent.remove_created_file(name, identity),
        )
    }

    pub(crate) fn open_with(
        path: &Path,
        config: BlockConfig,
        budget: SpaceBudget,
        durability: file_io::DirectoryDurability,
        summary: &mut file_io::DirectorySyncSummary,
        extend: impl FnOnce(&File, u64) -> io::Result<()>,
        remove: impl FnOnce(
            &file_io::Directory,
            &std::ffi::OsStr,
            file_io::FileIdentity,
        ) -> io::Result<()>,
    ) -> Result<Self, BlockError> {
        summary.clear();
        crate::header::check_block_size(config.block_size)?;
        let parent_path = path
            .parent()
            .filter(|path| !path.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
        let (file, mut created) = match file_io::open_read_write(path) {
            Ok(file) => (file, None),
            Err(error) if error.kind() == io::ErrorKind::NotFound => {
                block_count(config.initial_size, config.block_size)?;
                check_space(parent_path, config.initial_size, budget)?;
                let parent = file_io::Directory::open_with(
                    parent_path,
                    file_io::BindingRequirement::ResolvedPathAllowed,
                )?;
                let name = path
                    .file_name()
                    .ok_or(BlockError::InvalidInput("missing creation leaf"))?
                    .to_owned();
                let file = file_io::create_new_read_write(path, 0o644)?;
                (
                    file,
                    Some(CreatedEntry {
                        parent,
                        name,
                        identity: None,
                    }),
                )
            }
            Err(error) => return Err(error.into()),
        };
        let identified = (|| {
            if let Some(created) = &mut created {
                created.identity = Some(file_io::FileIdentity::of(&file)?);
            }
            #[cfg(test)]
            creation_fault(CreationStage::Claim)?;
            Ok::<_, BlockError>(())
        })();
        if let Err(primary) = identified {
            return Err(cleanup_creation(
                primary,
                InitialFile::Unlocked(file),
                created,
                remove,
            ));
        }
        let file = match ExclusiveFile::try_new_retaining(file) {
            Ok(file) => file,
            Err((error, file)) => {
                return Err(cleanup_creation(
                    error.into(),
                    InitialFile::Unlocked(file),
                    created,
                    remove,
                ))
            }
        };
        let initialized = (|| {
            if config.direct_io {
                file_io::enable_direct_io(file.as_file())?;
            }
            let mut page = AlignedBuffer::zeroed(HEADER_PAGE_SIZE, HEADER_PAGE_SIZE)?;
            if created.is_some() {
                extend(file.as_file(), config.initial_size)?;
                encode_block_header(config.block_size, page.as_mut_slice())?;
                file_io::write_all_at(file.as_file(), page.as_slice(), 0)?;
                #[cfg(test)]
                creation_fault(CreationStage::FileSync)?;
                file.as_file().sync_all()?;
                #[cfg(test)]
                if let Some(source) =
                    CREATION_FAULT.with(|slot| slot.borrow_mut().directory_sync.take())
                {
                    return Err(BlockError::DirectorySync {
                        path: parent_path.to_owned(),
                        source,
                    });
                }
                durability
                    .sync_directory(parent_path, summary)
                    .map_err(|source| BlockError::DirectorySync {
                        path: parent_path.to_owned(),
                        source,
                    })?;
            } else {
                let read = file_io::read_up_to_at(file.as_file(), page.as_mut_slice(), 0);
                read.result?;
                if read.bytes != HEADER_PAGE_SIZE {
                    return Err(BlockError::InvalidHeader);
                }
                validate_block_header(config.block_size, page.as_slice())?;
            }
            #[cfg(test)]
            creation_fault(CreationStage::StateAllocation)?;
            let count = block_count(file.as_file().metadata()?.len(), config.block_size)?;
            let mut slots = Vec::new();
            let mut free = VecDeque::new();
            let mut retired = VecDeque::new();
            slots.try_reserve_exact(count)?;
            free.try_reserve_exact(count)?;
            retired.try_reserve_exact(count)?;
            slots.push(Slot::Header);
            slots.resize_with(count, || Slot::Unmarked {
                live: false,
                leases: 0,
            });
            Ok(State {
                slots,
                free,
                retired,
                punch_holes: config.punch_holes,
                reclamation_failures: 0,
            })
        })();
        match initialized {
            Ok(state) => Ok(Self {
                inner: Arc::new(Inner {
                    file: Arc::new(file),
                    path: path.to_owned(),
                    block_size: config.block_size,
                    state: Mutex::new(state),
                }),
                marking_valid: true,
            }),
            Err(primary) => Err(cleanup_creation(
                primary,
                InitialFile::Locked(file),
                created,
                remove,
            )),
        }
    }

    pub fn mark(&mut self, used: &[BlockIndex]) -> Result<(), BlockError> {
        if !self.marking_valid {
            return Err(BlockError::MarkingAborted);
        }
        let mut state = self.inner.lock();
        if let Some(index) = used
            .iter()
            .find(|index| index.0 >= state.slots.len() as u64)
        {
            self.marking_valid = false;
            return Err(BlockError::InvalidBlock(index.0));
        }
        for index in used {
            if let Slot::Unmarked { live, .. } = &mut state.slots[index.0 as usize] {
                *live = true;
            }
        }
        Ok(())
    }

    pub fn abort_marking(&mut self) {
        self.marking_valid = false;
    }

    pub fn restart_marking(&mut self) {
        for slot in &mut self.inner.lock().slots {
            if let Slot::Unmarked { live, .. } = slot {
                *live = false;
            }
        }
        self.marking_valid = true;
    }

    pub(crate) fn check_marking(&self) -> Result<(), BlockError> {
        if !self.marking_valid {
            Err(BlockError::MarkingAborted)
        } else {
            Ok(())
        }
    }

    pub fn finish_marking(self) -> Result<BlockFile, BlockError> {
        self.check_marking()?;
        {
            let mut state = self.inner.lock();
            for index in 1..state.slots.len() {
                let Slot::Unmarked { live, leases } = state.slots[index] else {
                    unreachable!("unmarked owner holds unmarked slots");
                };
                state.slots[index] = if live {
                    Slot::Allocated { leases }
                } else {
                    Slot::Pending { leases }
                };
                if !live && leases == 0 {
                    self.inner.reclaim(&mut state, index);
                }
            }
        }
        Ok(BlockFile { inner: self.inner })
    }

    pub fn region(&self, index: BlockIndex, range: Range<u64>) -> Result<BlockRegion, BlockError> {
        self.inner.region(index, range)
    }

    pub fn sync(&self) -> Result<(), BlockError> {
        Ok(self.inner.file.as_file().sync_all()?)
    }

    pub fn snapshot(&self) -> BlockSnapshot {
        self.inner.snapshot()
    }
}

fn cleanup_creation(
    primary: BlockError,
    file: InitialFile,
    created: Option<CreatedEntry>,
    remove: impl FnOnce(&file_io::Directory, &std::ffi::OsStr, file_io::FileIdentity) -> io::Result<()>,
) -> BlockError {
    let Some(created) = created else {
        return primary;
    };
    let close = file.close().err();
    #[cfg(test)]
    let close = CREATION_FAULT.with(|slot| {
        let mut slot = slot.borrow_mut();
        slot.closes += 1;
        slot.close
            .take()
            .map(io::Error::from_raw_os_error)
            .or(close)
    });
    let removed = match created.identity {
        Some(identity) => {
            #[cfg(test)]
            if let Some(code) = CREATION_FAULT.with(|slot| slot.borrow_mut().remove.take()) {
                return BlockError::CreationCleanup {
                    primary: Box::new(primary),
                    cleanup: crate::CreationCleanupFailure {
                        close,
                        remove: Some(io::Error::from_raw_os_error(code)),
                        sync: None,
                    },
                };
            }
            remove(&created.parent, &created.name, identity)
        }
        None => Err(io::Error::other(
            "created file identity unavailable; refusing removal",
        )),
    };
    let (remove, sync) = match removed {
        Ok(()) => (
            None,
            created
                .parent
                .sync()
                .err()
                .map(|source| file_io::DirectorySyncError::Flush {
                    source,
                    close_error: None,
                }),
        ),
        Err(error) => (Some(error), None),
    };
    #[cfg(test)]
    let sync = if remove.is_none() {
        CREATION_FAULT
            .with(|slot| slot.borrow_mut().sync.take())
            .map(|code| file_io::DirectorySyncError::Flush {
                source: io::Error::from_raw_os_error(code),
                close_error: None,
            })
            .or(sync)
    } else {
        sync
    };
    if close.is_some() || remove.is_some() || sync.is_some() {
        BlockError::CreationCleanup {
            primary: Box::new(primary),
            cleanup: crate::CreationCleanupFailure {
                close,
                remove,
                sync,
            },
        }
    } else {
        primary
    }
}

impl BlockFile {
    pub fn allocate(&self) -> Result<BlockIndex, BlockError> {
        let mut state = self.inner.lock();
        self.inner.reclaim_retired(&mut state);
        let index = state.free.pop_front().ok_or(BlockError::Exhausted)?;
        state.slots[index] = Slot::Allocated { leases: 0 };
        Ok(BlockIndex(index as u64))
    }

    pub fn allocate_many(&self, count: usize) -> Result<Vec<BlockIndex>, BlockError> {
        let mut state = self.inner.lock();
        self.inner.reclaim_retired(&mut state);
        let count = count.min(state.free.len());
        let mut result = Vec::new();
        result.try_reserve_exact(count)?;
        for _ in 0..count {
            let index = state.free.pop_front().expect("checked free count");
            state.slots[index] = Slot::Allocated { leases: 0 };
            result.push(BlockIndex(index as u64));
        }
        Ok(result)
    }

    pub fn release(&self, index: BlockIndex) -> Result<ReleaseOutcome, BlockError> {
        let mut state = self.inner.lock();
        let position = usize::try_from(index.0).map_err(|_| BlockError::InvalidBlock(index.0))?;
        let slot = state
            .slots
            .get_mut(position)
            .ok_or(BlockError::InvalidBlock(index.0))?;
        let Slot::Allocated { leases } = *slot else {
            return Err(BlockError::NotAllocated(index.0));
        };
        *slot = Slot::Pending { leases };
        if leases != 0 {
            Ok(ReleaseOutcome::Pending)
        } else {
            let reclamation_error = self.inner.reclaim(&mut state, position);
            Ok(ReleaseOutcome::Reusable { reclamation_error })
        }
    }

    pub fn reclaim_released(&self) {
        self.inner.reclaim_retired(&mut self.inner.lock());
    }

    pub fn region(&self, index: BlockIndex, range: Range<u64>) -> Result<BlockRegion, BlockError> {
        self.inner.region(index, range)
    }

    pub fn sync(&self) -> Result<(), BlockError> {
        Ok(self.inner.file.as_file().sync_all()?)
    }

    pub fn snapshot(&self) -> BlockSnapshot {
        self.inner.snapshot()
    }

    pub fn grow(&self, target_bytes: u64, budget: SpaceBudget) -> Result<(), BlockError> {
        self.grow_with(target_bytes, budget, |file, range| {
            if !range.is_empty() {
                file_io::extend_file(file, range.end)?;
            }
            file.sync_all()
        })
    }

    pub(crate) fn grow_with(
        &self,
        target_bytes: u64,
        budget: SpaceBudget,
        extend_and_sync: impl FnOnce(&File, Range<u64>) -> io::Result<()>,
    ) -> Result<(), BlockError> {
        let count = block_count(target_bytes, self.inner.block_size)?;
        let mut state = self.inner.lock();
        let old_count = state.slots.len();
        if count < old_count {
            return Err(BlockError::InvalidInput(
                "physical block file cannot shrink",
            ));
        }
        if count == old_count {
            return Ok(());
        }
        let actual_size = self.inner.file.as_file().metadata()?.len();
        check_space(
            &self.inner.path,
            target_bytes.saturating_sub(actual_size),
            budget,
        )?;
        state.slots.try_reserve_exact(count - old_count)?;
        let free_capacity = count - state.free.len();
        state.free.try_reserve(free_capacity)?;
        let retired_capacity = count - state.retired.len();
        state.retired.try_reserve(retired_capacity)?;
        extend_and_sync(
            self.inner.file.as_file(),
            actual_size.min(target_bytes)..target_bytes,
        )?;
        state.slots.resize_with(count, || Slot::Free);
        state.free.extend(old_count..count);
        Ok(())
    }
}

impl Inner {
    fn lock(&self) -> MutexGuard<'_, State> {
        self.state.lock().expect("physical block state poisoned")
    }

    fn region(
        self: &Arc<Self>,
        index: BlockIndex,
        range: Range<u64>,
    ) -> Result<BlockRegion, BlockError> {
        if range.start > range.end || range.end > self.block_size {
            return Err(BlockError::InvalidInput("range exceeds physical block"));
        }
        let mut state = self.lock();
        let position = usize::try_from(index.0).map_err(|_| BlockError::InvalidBlock(index.0))?;
        let slot = state
            .slots
            .get_mut(position)
            .ok_or(BlockError::InvalidBlock(index.0))?;
        match slot {
            Slot::Allocated { leases } | Slot::Unmarked { leases, .. } => {
                *leases = leases
                    .checked_add(1)
                    .ok_or(BlockError::InvalidInput("too many region leases"))?;
            }
            _ => return Err(BlockError::NotAllocated(index.0)),
        }
        Ok(BlockRegion {
            lease: Arc::new(RegionLease {
                inner: Arc::clone(self),
                index: position,
                #[cfg(any(test, feature = "ownership-test-support"))]
                drop_witness: crate::drop_witness::DropRegistration::default(),
            }),
            start: index.0 * self.block_size + range.start,
            length: range.end - range.start,
        })
    }

    fn reclaim(&self, state: &mut State, index: usize) -> Option<io::Error> {
        assert!(matches!(state.slots[index], Slot::Pending { leases: 0 }));
        let mut failure = None;
        if state.punch_holes {
            let start = index as u64 * self.block_size;
            match allocate(
                self.file.as_file(),
                start..start + self.block_size,
                Allocation::PunchHole,
            ) {
                Ok(()) => (),
                Err(error) if error.kind() == io::ErrorKind::Unsupported => {
                    state.punch_holes = false
                }
                Err(error) => {
                    state.reclamation_failures = state.reclamation_failures.saturating_add(1);
                    failure = Some(error);
                }
            }
        }
        state.slots[index] = Slot::Free;
        state.free.push_back(index);
        failure
    }

    fn reclaim_retired(&self, state: &mut State) {
        while let Some(index) = state.retired.pop_front() {
            self.reclaim(state, index);
        }
    }

    fn snapshot(&self) -> BlockSnapshot {
        let state = self.lock();
        BlockSnapshot {
            block_size: self.block_size,
            capacity: state.slots.len() as u64 * self.block_size,
            total_blocks: state.slots.len() as u64,
            free_blocks: state.free.len() as u64,
            pending_release_blocks: state
                .slots
                .iter()
                .filter(|slot| matches!(slot, Slot::Pending { .. }))
                .count() as u64,
            reclamation_failures: state.reclamation_failures,
        }
    }
}

impl BlockRegion {

    #[cfg(any(test, feature = "ownership-test-support"))]
    pub(crate) fn watch_final_lease(&self) -> io::Result<crate::DropWitness> {
        self.lease.drop_witness.watch()
    }

    pub(crate) fn file_region(&self) -> (Arc<ExclusiveFile>, u64, u64) {
        (Arc::clone(&self.lease.inner.file), self.start, self.length)
    }

    /// Narrows this region without acquiring another physical block lease.
    pub fn subregion(&self, relative: Range<u64>) -> Result<Self, BlockError> {
        if relative.start > relative.end || relative.end > self.length {
            return Err(BlockError::InvalidInput("range exceeds parent region"));
        }
        let start = self
            .start
            .checked_add(relative.start)
            .ok_or(BlockError::InvalidInput("region offset overflow"))?;
        Ok(Self {
            lease: Arc::clone(&self.lease),
            start,
            length: relative.end - relative.start,
        })
    }

    pub fn len(&self) -> u64 {
        self.length
    }

    pub fn is_empty(&self) -> bool {
        self.length == 0
    }

    fn offset(&self, offset: u64, length: usize) -> io::Result<u64> {
        if offset
            .checked_add(length as u64)
            .is_none_or(|end| end > self.length)
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "I/O exceeds physical block region",
            ));
        }
        Ok(self.start + offset)
    }

    pub fn read_at(&self, bytes: &mut [u8], offset: u64) -> Transfer {
        match self.offset(offset, bytes.len()) {
            Ok(offset) => file_io::read_up_to_at(self.lease.inner.file.as_file(), bytes, offset),
            Err(error) => Transfer {
                bytes: 0,
                result: Err(error),
            },
        }
    }

    pub fn write_at(&self, bytes: &[u8], offset: u64) -> Transfer {
        match self.offset(offset, bytes.len()) {
            Ok(offset) => file_io::write_fully_at(self.lease.inner.file.as_file(), bytes, offset),
            Err(error) => Transfer {
                bytes: 0,
                result: Err(error),
            },
        }
    }
}

pub(crate) fn region_lease_payload_size() -> usize {
    std::mem::size_of::<RegionLease>()
}

impl Drop for RegionLease {
    fn drop(&mut self) {
        #[cfg(any(test, feature = "ownership-test-support"))]
        let _witness = self.drop_witness.enter();
        let mut state = self.inner.lock();
        match &mut state.slots[self.index] {
            Slot::Allocated { leases } | Slot::Unmarked { leases, .. } => *leases -= 1,
            Slot::Pending { leases } => {
                *leases -= 1;
                if *leases == 0 {
                    state.retired.push_back(self.index);
                }
            }
            _ => unreachable!("leased region cannot be free"),
        }
    }
}

#[cfg(test)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum CreationStage {
    Claim,
    FileSync,
    StateAllocation,
}
#[cfg(test)]
#[derive(Default)]
pub(crate) struct CreationFault {
    pub directory_sync: Option<file_io::DirectorySyncError>,
    pub stage: Option<CreationStage>,
    pub close: Option<i32>,
    pub remove: Option<i32>,
    pub sync: Option<i32>,
    pub closes: usize,
}
#[cfg(test)]
thread_local! { pub(crate) static CREATION_FAULT: std::cell::RefCell<CreationFault> = std::cell::RefCell::default(); }
#[cfg(test)]
fn creation_fault(stage: CreationStage) -> Result<(), BlockError> {
    CREATION_FAULT.with(|slot| {
        let mut slot = slot.borrow_mut();
        if slot.stage != Some(stage) {
            return Ok(());
        }
        slot.stage = None;
        Err(match stage {
            CreationStage::StateAllocation => {
                BlockError::Memory(Vec::<u8>::new().try_reserve(usize::MAX).unwrap_err())
            }
            _ => BlockError::Io(io::Error::from_raw_os_error(28)),
        })
    })
}
