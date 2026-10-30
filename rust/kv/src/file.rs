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

use file_io::{publish_file, DirectoryDurability, DirectorySyncSummary};
pub(crate) use file_io::{read_exact_at, write_all_at};
use std::fs::{self, File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};

use crate::page::{decode_root, encode_root, Node, Root, PAGE_SIZE};
use crate::{Error, Result};

// Zero denotes a free page; MAX denotes a live page. Other values are the
// generation that retired the page. This table is scratch space, never a
// recovery input, and uses eight bytes of disk per data page.
pub(crate) const LIVE: u64 = u64::MAX;

pub(crate) struct FileLock(File);

impl Drop for FileLock {
    fn drop(&mut self) {
        // CLOEXEC descriptors can still be inherited between fork and exec.
        // Closing our copy alone may leave the lock held by that child.
        let _ = self.0.unlock();
    }
}

pub(crate) struct Files {
    pub data: File,
    pub roots: [Option<Root>; 2],
    pub slot: usize,
    pub allocation: Allocation,
    pub lock: FileLock,
}

pub(crate) fn open(
    directory: &Path,
    durability: DirectoryDurability,
    summary: &mut DirectorySyncSummary,
) -> Result<Files> {
    create_directory(directory, durability, summary)?;
    let lock = options()
        .create(true)
        .truncate(false)
        .open(directory.join("lock"))?;
    // std has no file locking on Emscripten (try_lock reports Unsupported);
    // the browser runtime admits one engine per data directory with a Web Lock.
    #[cfg(not(target_os = "emscripten"))]
    {
        lock.try_lock().map_err(|error| match error {
            std::fs::TryLockError::WouldBlock => Error::Busy,
            std::fs::TryLockError::Error(error) => Error::Io(error),
        })?;
    }
    let lock = FileLock(lock);
    let path = directory.join("data");
    if !path.try_exists()? {
        initialize(directory, &path, durability, summary)?;
    }
    let data = options().open(path)?;
    sync_directory(directory, durability, summary)?;
    let length = data.metadata()?.len();
    let pages = length / PAGE_SIZE as u64;
    if pages < 2 {
        return Err(Error::Corrupt("truncated root pages"));
    }
    let mut roots = [None; 2];
    for (slot, root) in roots.iter_mut().enumerate() {
        let mut buffer = [0; PAGE_SIZE];
        // An unreadable slot is an I/O failure, not evidence of a torn write.
        read_exact_at(&data, &mut buffer, slot as u64 * PAGE_SIZE as u64)?;
        *root = decode_root(&buffer, pages);
    }
    let slot = match (roots[0], roots[1]) {
        (None, None) => return Err(Error::Corrupt("no valid committed root")),
        (Some(_), None) => 0,
        (None, Some(_)) => 1,
        (Some(left), Some(right)) => usize::from(right.generation > left.generation),
    };
    let allocation = Allocation::new(directory.join("allocation.tmp"), pages)?;
    Ok(Files {
        data,
        roots,
        slot,
        allocation,
        lock,
    })
}

fn initialize(
    directory: &Path,
    destination: &Path,
    durability: DirectoryDurability,
    summary: &mut DirectorySyncSummary,
) -> Result<()> {
    let temporary = directory.join("data.tmp");
    remove_if_exists(&temporary)?;
    let file = options().create_new(true).open(&temporary)?;
    let root = Root {
        generation: 1,
        page: 2,
        pages: 3,
    };
    write_all_at(
        &file,
        &Node::Leaf(Vec::new()).encode(2)?,
        2 * PAGE_SIZE as u64,
    )?;
    for slot in 0..2 {
        write_all_at(&file, &encode_root(root), slot * PAGE_SIZE as u64)?;
    }
    file.sync_all()?;
    drop(file);
    publish_file(&temporary, destination)?;
    sync_directory(directory, durability, summary)?;
    Ok(())
}

fn options() -> OpenOptions {
    let mut options = OpenOptions::new();
    options.read(true).write(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    options
}

fn create_directory(
    directory: &Path,
    durability: DirectoryDurability,
    summary: &mut DirectorySyncSummary,
) -> Result<()> {
    let mut missing = Vec::new();
    let mut current = directory;
    while !current.try_exists()? {
        missing.push(current.to_path_buf());
        current = current
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
    }
    // The existing prefix may have survived an interrupted directory creation
    // before its parent was synced. Complete that step before extending it.
    sync_directory(
        current
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .unwrap_or(Path::new(".")),
        durability,
        summary,
    )?;
    for path in missing.iter().rev() {
        match fs::create_dir(path) {
            Ok(()) => (),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => (),
            Err(error) => return Err(error.into()),
        }
        sync_directory(
            path.parent()
                .filter(|parent| !parent.as_os_str().is_empty())
                .unwrap_or(Path::new(".")),
            durability,
            summary,
        )?;
    }
    Ok(())
}

fn remove_if_exists(path: &Path) -> io::Result<()> {
    match fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error),
    }
}

pub(crate) struct Allocation {
    file: File,
    path: PathBuf,
    pub pages: u64,
    cursor: u64,
}

impl Allocation {
    fn new(path: PathBuf, pages: u64) -> Result<Self> {
        remove_if_exists(&path)?;
        let file = options().create_new(true).open(&path)?;
        file.set_len(
            pages
                .checked_mul(8)
                .ok_or(Error::Corrupt("allocation table is too large"))?,
        )?;
        Ok(Self {
            file,
            path,
            pages,
            cursor: 2,
        })
    }

    pub fn mark(&self, page: u64, state: u64) -> Result<()> {
        let offset = page
            .checked_mul(8)
            .ok_or(Error::Corrupt("allocation offset overflow"))?;
        write_all_at(&self.file, &state.to_le_bytes(), offset)?;
        Ok(())
    }

    pub fn mark_reachable(&self, page: u64, state: u64) -> Result<()> {
        let mut previous = [0; 8];
        read_exact_at(&self.file, &mut previous, page * 8)?;
        if u64::from_le_bytes(previous) == 0 {
            self.mark(page, state)?;
        }
        Ok(())
    }

    pub fn allocate(&mut self, oldest: u64) -> Result<u64> {
        // The cursor avoids rescanning the allocated prefix for every append.
        // Retiring a page moves it back, so eligible holes are reused first.
        let mut buffer = [0; PAGE_SIZE];
        let mut page = self.cursor;
        while page < self.pages {
            let count = (self.pages - page).min((PAGE_SIZE / 8) as u64) as usize;
            read_exact_at(&self.file, &mut buffer[..count * 8], page * 8)?;
            for index in 0..count {
                let state =
                    u64::from_le_bytes(buffer[index * 8..index * 8 + 8].try_into().unwrap());
                if state == 0 || state <= oldest {
                    let chosen = page + index as u64;
                    self.mark(chosen, LIVE)?;
                    self.cursor = chosen + 1;
                    return Ok(chosen);
                }
            }
            page += count as u64;
        }
        let chosen = self.pages;
        if chosen >= u64::MAX / PAGE_SIZE as u64 {
            return Err(Error::Corrupt("data file is too large"));
        }
        self.mark(chosen, LIVE)?;
        self.pages += 1;
        self.cursor = self.pages;
        Ok(chosen)
    }

    pub fn retire(&mut self, page: u64, generation: u64) -> Result<()> {
        self.mark(page, generation)?;
        self.cursor = self.cursor.min(page);
        Ok(())
    }

    pub fn revisit(&mut self) {
        self.cursor = 2;
    }
}

impl Drop for Allocation {
    fn drop(&mut self) {
        // Windows std::fs opens files with FILE_SHARE_DELETE as well.
        let _ = fs::remove_file(&self.path);
    }
}

fn sync_directory(
    path: &Path,
    durability: DirectoryDurability,
    summary: &mut DirectorySyncSummary,
) -> Result<()> {
    #[cfg(test)]
    if let Some(source) = SYNC_FAULT.with(|slot| {
        let mut slot = slot.borrow_mut();
        let index = slot.calls;
        slot.calls += 1;
        if slot.fail_at == Some(index) {
            slot.failure.take()
        } else {
            None
        }
    }) {
        return Err(Error::DirectorySync {
            path: path.to_owned(),
            source,
        });
    }
    durability
        .sync_directory(path, summary)
        .map_err(|source| Error::DirectorySync {
            path: path.to_owned(),
            source,
        })
}

#[cfg(test)]
#[derive(Default)]
pub(crate) struct SyncFault {
    pub fail_at: Option<usize>,
    pub failure: Option<file_io::DirectorySyncError>,
    pub calls: usize,
}
#[cfg(test)]
thread_local! { pub(crate) static SYNC_FAULT: std::cell::RefCell<SyncFault> = std::cell::RefCell::default(); }
