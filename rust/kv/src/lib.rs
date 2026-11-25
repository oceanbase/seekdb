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

//! Local, ordered byte-key storage. One writer holds a transaction through
//! read/modify/write; readers pin immutable roots and do not hold its lock.
//!
//! Commits write only modified tree paths and overflow pages, sync them, then
//! publish one of two checksummed root pages and sync again. Concurrent commits
//! share root publication; callers wait for durability before returning. No WAL, SLOG,
//! database dependency, full key index, or background thread is required.
//! The module owns `data`, `lock`, and a rebuildable `allocation.tmp` in its
//! directory. Opening scans the two recovery trees to rebuild allocation
//! state with bounded memory. Freed pages are reused; the file is not compacted
//! or shrunk. An I/O error during a write requires reopening the store: a
//! failed commit may already have reached disk.
//!
//! Keys are compared lexicographically as bytes. Callers encode their own
//! namespaces and records; transactions may atomically change multiple keys.
//! Limits bound transaction bookkeeping and snapshot retention. Returned
//! values belong to callers and can be up to [`MAX_VALUE_SIZE`] bytes.

mod file;
mod page;

use std::collections::{BTreeMap, BTreeSet};
use std::fmt;
use std::fs::File;
use std::io;
use std::ops::Bound;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard};

use file::{read_exact_at, write_all_at, Allocation, FileLock, LIVE};
use page::{decode_overflow, encode_overflow, encode_root, Child, Entry, Node, Root, Value};
use page::{INLINE_VALUE_SIZE, MAX_DEPTH, PAYLOAD_SIZE};
pub use page::{MAX_KEY_SIZE, MAX_VALUE_SIZE, PAGE_SIZE};

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    DirectorySync {
        path: std::path::PathBuf,
        source: file_io::DirectorySyncError,
    },
    Corrupt(&'static str),
    Busy,
    KeyTooLarge,
    ValueTooLarge,
    TransactionTooLarge,
    TransactionAborted,
    Poisoned,
    InvalidOptions,
}

impl fmt::Display for Error {
    fn fmt(&self, output: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(output, "KV I/O error: {error}"),
            Self::DirectorySync { path, source } => {
                write!(output, "KV directory sync {}: {source}", path.display())
            }
            Self::Corrupt(message) => write!(output, "corrupt KV storage: {message}"),
            Self::Busy => output.write_str("KV storage is already open"),
            Self::KeyTooLarge => output.write_str("KV key exceeds the size limit"),
            Self::ValueTooLarge => output.write_str("KV value exceeds the size limit"),
            Self::TransactionTooLarge => output.write_str("KV transaction exceeds the page limit"),
            Self::TransactionAborted => output.write_str("KV transaction has a failed mutation"),
            Self::Poisoned => output.write_str("KV storage must be reopened after a write failure"),
            Self::InvalidOptions => output.write_str("KV limits must be nonzero"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            Self::DirectorySync { source, .. } => Some(source),
            _ => None,
        }
    }
}

impl From<io::Error> for Error {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Options {
    /// Maximum total allocated and retired pages tracked by one transaction.
    pub max_transaction_pages: usize,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            max_transaction_pages: 4096,
        }
    }
}

#[derive(Clone, Debug)]
pub struct Range {
    pub start: Bound<Vec<u8>>,
    pub end: Bound<Vec<u8>>,
}

impl Default for Range {
    fn default() -> Self {
        Self::all()
    }
}

impl Range {
    pub fn all() -> Self {
        Self {
            start: Bound::Unbounded,
            end: Bound::Unbounded,
        }
    }

    pub fn prefix(prefix: &[u8]) -> Self {
        let mut upper = prefix.to_vec();
        let end = loop {
            match upper.pop() {
                Some(255) => (),
                Some(byte) => {
                    upper.push(byte + 1);
                    break Bound::Excluded(upper);
                }
                None => break Bound::Unbounded,
            }
        };
        Self {
            start: Bound::Included(prefix.to_vec()),
            end,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CommitInfo {
    pub generation: u64,
    /// Data-file page writes by this transaction. A shared root write is
    /// counted only for the caller that performs the group publication.
    pub pages_written: u64,
    pub bytes_written: u64,
}

#[derive(Clone)]
pub struct Store {
    shared: Arc<Shared>,
}

struct Shared {
    data: File,
    writer: Mutex<Writer>,
    committed: Condvar,
    visible: Mutex<Visible>,
    poisoned: AtomicBool,
    options: Options,
    // Keep last: allocation scratch cleanup must finish before unlocking.
    _lock: FileLock,
}

// Bound unacknowledged work while a caller performs I/O without the writer lock.
const MAX_PENDING_COMMITS: u64 = 64;

struct Writer {
    // Includes staged transactions. Only `roots` and `visible` are durable.
    head: Root,
    flushing: bool,
    roots: [Option<Root>; 2],
    slot: usize,
    allocation: Allocation,
    writes: u64,
    oldest_seen: u64,
}

// A panicking publisher must wake all waiters and reject dependent writes.
struct Flush<'a> {
    shared: &'a Shared,
    finished: bool,
}

impl Drop for Flush<'_> {
    fn drop(&mut self) {
        if !self.finished {
            // Pair the notification with the mutex used by condition waiters.
            let _writer = self.shared.writer.lock();
            self.shared.poison();
        }
    }
}

struct Visible {
    root: Root,
    pins: BTreeMap<u64, usize>,
}

impl Store {
    pub fn open(directory: impl AsRef<Path>) -> Result<Self> {
        Self::open_with_options(
            directory,
            Options::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        )
    }

    pub fn open_with_options(
        directory: impl AsRef<Path>,
        options: Options,
        durability: file_io::DirectoryDurability,
        summary: &mut file_io::DirectorySyncSummary,
    ) -> Result<Self> {
        summary.clear();
        if options.max_transaction_pages == 0 {
            return Err(Error::InvalidOptions);
        }
        let files = file::open(directory.as_ref(), durability, summary)?;
        let root = files.roots[files.slot].unwrap();
        let store = Self {
            shared: Arc::new(Shared {
                data: files.data,
                writer: Mutex::new(Writer {
                    head: root,
                    flushing: false,
                    roots: files.roots,
                    slot: files.slot,
                    allocation: files.allocation,
                    writes: 0,
                    oldest_seen: 0,
                }),
                committed: Condvar::new(),
                visible: Mutex::new(Visible {
                    root,
                    pins: BTreeMap::new(),
                }),
                poisoned: AtomicBool::new(false),
                options,
                _lock: files.lock,
            }),
        };
        let writer = store.shared.writer.lock().map_err(|_| Error::Poisoned)?;
        rebuild(&store.shared, &writer.allocation, root, root.page, 0, LIVE)?;
        if let Some(previous) = writer.roots[1 - writer.slot] {
            // Pages unique to the fallback root become reusable when that
            // root is superseded. They must not remain LIVE forever.
            rebuild(
                &store.shared,
                &writer.allocation,
                previous,
                previous.page,
                0,
                root.generation,
            )?;
        }
        // Validate both recovery trees before modifying the authoritative
        // file. Only an uncommitted append can leave a partial tail.
        let complete_length = writer.allocation.pages * PAGE_SIZE as u64;
        if store.shared.data.metadata()?.len() != complete_length {
            store.shared.data.set_len(complete_length)?;
        }
        drop(writer);
        Ok(store)
    }

    pub fn snapshot(&self) -> Result<Snapshot> {
        self.shared.check()?;
        let mut visible = self.shared.visible.lock().map_err(|_| Error::Poisoned)?;
        let root = visible.root;
        *visible.pins.entry(root.generation).or_default() += 1;
        Ok(Snapshot {
            shared: Arc::clone(&self.shared),
            root,
        })
    }

    pub fn get(&self, key: &[u8]) -> Result<Option<Vec<u8>>> {
        self.snapshot()?.get(key)
    }

    /// Serializes read/modify/write. Commit releases the writer while waiting
    /// for durability; later writers may build on its staged root. Do not reenter
    /// this method from a transaction. Existing snapshots remain readable.
    pub fn begin(&self) -> Result<Transaction<'_>> {
        let mut writer = self.shared.writer.lock().map_err(|_| Error::Poisoned)?;
        loop {
            self.shared.check()?;
            if writer.head.generation - writer.roots[writer.slot].unwrap().generation
                < MAX_PENDING_COMMITS
            {
                break;
            }
            writer = self
                .shared
                .committed
                .wait(writer)
                .map_err(|_| Error::Poisoned)?;
        }
        let visible = self.shared.visible.lock().map_err(|_| Error::Poisoned)?;
        let mut oldest = writer
            .roots
            .iter()
            .flatten()
            .map(|root| root.generation)
            .min()
            .unwrap();
        if let Some((&generation, _)) = visible.pins.first_key_value() {
            oldest = oldest.min(generation);
        }
        drop(visible);
        if oldest != writer.oldest_seen {
            writer.allocation.revisit();
            writer.oldest_seen = oldest;
        }
        let root = writer.head;
        let initial_writes = writer.writes;
        Ok(Transaction {
            shared: &self.shared,
            writer,
            root: root.page,
            generation: root.generation,
            oldest,
            allocated: BTreeSet::new(),
            retired: BTreeSet::new(),
            initial_writes,
            dirty: false,
            failed: false,
            completed: false,
        })
    }
}

impl Shared {
    fn check(&self) -> Result<()> {
        if self.poisoned.load(Ordering::Acquire) {
            Err(Error::Poisoned)
        } else {
            Ok(())
        }
    }

    fn poison(&self) {
        self.poisoned.store(true, Ordering::Release);
        self.committed.notify_all();
    }

    fn wait_durable(&self, generation: u64) -> Result<u64> {
        let mut writer = self.writer.lock().map_err(|_| Error::Poisoned)?;
        loop {
            self.check()?;
            if writer.roots[writer.slot].unwrap().generation >= generation {
                return Ok(0);
            }
            if writer.flushing {
                writer = self.committed.wait(writer).map_err(|_| Error::Poisoned)?;
                continue;
            }
            let root = writer.head;
            let slot = 1 - writer.slot;
            writer.flushing = true;
            let mut flush = Flush {
                shared: self,
                finished: false,
            };
            drop(writer);
            // New transactions build on head while this immutable root syncs.
            // Their retire generations are newer than both durable roots, so
            // they cannot reuse pages referenced by this publication.
            let result = self.publish(root, slot);
            flush.finished = true;
            writer = self.writer.lock().map_err(|_| Error::Poisoned)?;
            writer.flushing = false;
            if let Err(error) = result {
                self.poison();
                return Err(error);
            }
            self.check()?;
            writer.roots[slot] = Some(root);
            writer.slot = slot;
            match self.visible.lock() {
                Ok(mut visible) => visible.root = root,
                Err(_) => {
                    self.poison();
                    return Err(Error::Poisoned);
                }
            }
            self.committed.notify_all();
            return Ok(1);
        }
    }

    fn publish(&self, root: Root, slot: usize) -> Result<()> {
        #[cfg(test)]
        tests::at("before-data-sync")?;
        self.data.sync_all()?;
        #[cfg(test)]
        tests::at("after-data-sync")?;
        let buffer = encode_root(root);
        #[cfg(test)]
        if tests::is_crash_stage("during-root-write") {
            write_all_at(&self.data, &buffer[..24], slot as u64 * PAGE_SIZE as u64)?;
            self.data.sync_all()?;
            tests::at("during-root-write")?;
        }
        write_all_at(&self.data, &buffer, slot as u64 * PAGE_SIZE as u64)?;
        #[cfg(test)]
        tests::at("after-root-write")?;
        self.data.sync_all()?;
        #[cfg(test)]
        tests::at("after-root-sync")?;
        Ok(())
    }

    fn read_page(&self, id: u64, pages: u64) -> Result<[u8; PAGE_SIZE]> {
        if id < 2 || id >= pages || id > u64::MAX / PAGE_SIZE as u64 {
            return Err(Error::Corrupt("page address outside committed storage"));
        }
        let mut page = [0; PAGE_SIZE];
        read_exact_at(&self.data, &mut page, id * PAGE_SIZE as u64)?;
        Ok(page)
    }

    fn node(&self, id: u64, pages: u64) -> Result<Node> {
        Node::decode(&self.read_page(id, pages)?, id)
    }

    fn value(&self, value: &Value, pages: u64) -> Result<Vec<u8>> {
        if value.head == 0 {
            return Ok(value.inline.clone());
        }
        let mut result = Vec::with_capacity(value.length);
        self.walk_value(value, pages, |_, bytes| {
            result.extend_from_slice(bytes);
            Ok(())
        })?;
        Ok(result)
    }

    fn walk_value(
        &self,
        value: &Value,
        pages: u64,
        mut visit: impl FnMut(u64, &[u8]) -> Result<()>,
    ) -> Result<()> {
        let mut id = value.head;
        let mut remaining = value.length;
        while id != 0 {
            let page = self.read_page(id, pages)?;
            let (next, bytes) = decode_overflow(&page, id)?;
            if bytes.len() > remaining || bytes.len() != remaining.min(PAYLOAD_SIZE - 8) {
                return Err(Error::Corrupt("invalid overflow chain length"));
            }
            visit(id, bytes)?;
            remaining -= bytes.len();
            id = next;
        }
        if remaining != 0 {
            return Err(Error::Corrupt("truncated overflow chain"));
        }
        Ok(())
    }
}

pub struct Snapshot {
    shared: Arc<Shared>,
    root: Root,
}

impl Snapshot {
    pub fn generation(&self) -> u64 {
        self.root.generation
    }

    pub fn get(&self, key: &[u8]) -> Result<Option<Vec<u8>>> {
        self.shared.check()?;
        get(&self.shared, self.root.page, self.root.pages, key)
    }

    pub fn scan(&self, range: Range) -> Result<Cursor<'_>> {
        self.shared.check()?;
        Cursor::new(&self.shared, self.root.page, self.root.pages, range)
    }
}

impl Drop for Snapshot {
    fn drop(&mut self) {
        match self.shared.visible.lock() {
            Ok(mut visible) => {
                let count = visible.pins.get_mut(&self.root.generation).unwrap();
                *count -= 1;
                if *count == 0 {
                    visible.pins.remove(&self.root.generation);
                }
            }
            Err(error) => {
                // Release visible before acquiring writer, preserving lock order.
                drop(error);
                let _writer = self.shared.writer.lock();
                self.shared.poison();
            }
        }
    }
}

pub struct Transaction<'a> {
    shared: &'a Arc<Shared>,
    writer: MutexGuard<'a, Writer>,
    root: u64,
    generation: u64,
    oldest: u64,
    allocated: BTreeSet<u64>,
    retired: BTreeSet<u64>,
    initial_writes: u64,
    dirty: bool,
    failed: bool,
    completed: bool,
}

impl Transaction<'_> {
    pub fn get(&self, key: &[u8]) -> Result<Option<Vec<u8>>> {
        self.check()?;
        get(self.shared, self.root, self.writer.allocation.pages, key)
    }

    pub fn scan(&self, range: Range) -> Result<Cursor<'_>> {
        self.check()?;
        Cursor::new(self.shared, self.root, self.writer.allocation.pages, range)
    }

    pub fn put(&mut self, key: &[u8], value: &[u8]) -> Result<()> {
        self.check()?;
        let result = self.put_inner(key, value);
        self.finish_mutation(result)
    }

    pub fn delete(&mut self, key: &[u8]) -> Result<bool> {
        self.check()?;
        let result = self.delete_inner(key);
        self.finish_mutation(result)
    }

    fn check(&self) -> Result<()> {
        self.shared.check()?;
        if self.failed {
            Err(Error::TransactionAborted)
        } else {
            Ok(())
        }
    }

    fn finish_mutation<T>(&mut self, result: Result<T>) -> Result<T> {
        if let Err(error) = &result {
            self.failed = true;
            if matches!(error, Error::Io(_) | Error::Corrupt(_)) {
                self.shared.poison();
            }
        }
        result
    }

    fn put_inner(&mut self, key: &[u8], bytes: &[u8]) -> Result<()> {
        check_key(key)?;
        if bytes.len() > MAX_VALUE_SIZE {
            return Err(Error::ValueTooLarge);
        }
        let value = self.make_value(bytes)?;
        let children = self.modify(self.root, key, Some(value), 0)?;
        self.set_root(children)?;
        self.dirty = true;
        Ok(())
    }

    fn delete_inner(&mut self, key: &[u8]) -> Result<bool> {
        check_key(key)?;
        if find(self.shared, self.root, self.writer.allocation.pages, key)?.is_none() {
            return Ok(false);
        }
        let children = self.modify(self.root, key, None, 0)?;
        self.set_root(children)?;
        self.dirty = true;
        Ok(true)
    }

    fn set_root(&mut self, mut children: Vec<Child>) -> Result<()> {
        self.root = match children.len() {
            0 => self.write_node(None, Node::Leaf(Vec::new()))?,
            1 => children.pop().unwrap().page,
            _ => self.write_node(None, Node::Branch(children))?,
        };
        // Lazy deletion removes empty pages. Collapse unary roots without
        // forcing neighboring leaf rewrites or a full tree compaction.
        while let Node::Branch(children) =
            self.shared.node(self.root, self.writer.allocation.pages)?
        {
            if children.len() != 1 {
                break;
            }
            let old = self.root;
            self.root = children[0].page;
            self.retire(old)?;
        }
        Ok(())
    }

    fn modify(
        &mut self,
        id: u64,
        key: &[u8],
        value: Option<Value>,
        depth: usize,
    ) -> Result<Vec<Child>> {
        if depth >= MAX_DEPTH {
            return Err(Error::Corrupt("tree depth limit exceeded"));
        }
        let mut node = self.shared.node(id, self.writer.allocation.pages)?;
        match &mut node {
            Node::Leaf(entries) => {
                let position = entries.binary_search_by(|entry| entry.key.as_slice().cmp(key));
                match (position, value) {
                    (Ok(index), Some(value)) => {
                        self.retire_value(&entries[index].value)?;
                        entries[index].value = value;
                    }
                    (Ok(index), None) => {
                        self.retire_value(&entries[index].value)?;
                        entries.remove(index);
                    }
                    (Err(index), Some(value)) => entries.insert(
                        index,
                        Entry {
                            key: key.to_vec(),
                            value,
                        },
                    ),
                    (Err(_), None) => return Err(Error::Corrupt("missing delete target")),
                }
            }
            Node::Branch(children) => {
                let index = child_index(children, key);
                let replacement = self.modify(children[index].page, key, value, depth + 1)?;
                children.splice(index..index + 1, replacement);
            }
        }
        if node.len() == 0 {
            self.retire(id)?;
            return Ok(Vec::new());
        }
        let right = if node.encoded_size() > PAYLOAD_SIZE {
            Some(node.split()?)
        } else {
            None
        };
        let key = node.first_key().unwrap().to_vec();
        let mut children = vec![Child {
            key,
            page: self.write_node(Some(id), node)?,
        }];
        if let Some(right) = right {
            let key = right.first_key().unwrap().to_vec();
            children.push(Child {
                key,
                page: self.write_node(None, right)?,
            });
        }
        Ok(children)
    }

    fn reserve_tracking(&self) -> Result<()> {
        if self.allocated.len() + self.retired.len() >= self.shared.options.max_transaction_pages {
            Err(Error::TransactionTooLarge)
        } else {
            Ok(())
        }
    }

    fn allocate(&mut self) -> Result<u64> {
        self.reserve_tracking()?;
        let page = self.writer.allocation.allocate(self.oldest)?;
        self.allocated.insert(page);
        Ok(page)
    }

    fn retire(&mut self, page: u64) -> Result<()> {
        if self.allocated.remove(&page) {
            self.writer.allocation.retire(page, 0)?;
        } else if !self.retired.contains(&page) {
            self.reserve_tracking()?;
            self.retired.insert(page);
        }
        Ok(())
    }

    fn write_page(&mut self, id: u64, bytes: &[u8; PAGE_SIZE]) -> Result<()> {
        write_all_at(&self.shared.data, bytes, id * PAGE_SIZE as u64)?;
        self.writer.writes += 1;
        Ok(())
    }

    fn write_node(&mut self, previous: Option<u64>, node: Node) -> Result<u64> {
        let id = match previous {
            Some(id) if self.allocated.contains(&id) => id,
            previous => {
                if let Some(id) = previous {
                    self.retire(id)?;
                }
                self.allocate()?
            }
        };
        self.write_page(id, &node.encode(id)?)?;
        Ok(id)
    }

    fn make_value(&mut self, bytes: &[u8]) -> Result<Value> {
        if bytes.len() <= INLINE_VALUE_SIZE {
            return Ok(Value {
                length: bytes.len(),
                head: 0,
                inline: bytes.to_vec(),
            });
        }
        let mut head = 0;
        for chunk in bytes.chunks(PAYLOAD_SIZE - 8).rev() {
            let id = self.allocate()?;
            self.write_page(id, &encode_overflow(id, head, chunk)?)?;
            head = id;
        }
        Ok(Value {
            length: bytes.len(),
            head,
            inline: Vec::new(),
        })
    }

    fn retire_value(&mut self, value: &Value) -> Result<()> {
        if value.head == 0 {
            return Ok(());
        }
        // Copy just the current page, not the entire value or chain index.
        let shared = Arc::clone(self.shared);
        shared.walk_value(value, self.writer.allocation.pages, |id, _| self.retire(id))
    }

    /// Returns only after this transaction and its predecessors are durable.
    /// On publication failure, durability is uncertain and the store requires
    /// reopening. A failed group must not release potentially published pages.
    pub fn commit(mut self) -> Result<CommitInfo> {
        self.check()?;
        let generation = if self.dirty {
            self.generation
                .checked_add(1)
                .filter(|generation| *generation < LIVE)
                .ok_or(Error::Corrupt("generation exhausted"))?
        } else {
            self.generation
        };
        if self.dirty {
            // Retiring before publication is safe: allocation remains pinned by
            // both durable roots (and readers), which precede this generation.
            self.completed = true;
            for &page in &self.retired {
                if let Err(error) = self.writer.allocation.retire(page, generation) {
                    self.shared.poison();
                    return Err(error);
                }
            }
            self.writer.head = Root {
                generation,
                page: self.root,
                pages: self.writer.allocation.pages,
            };
        }
        self.completed = true;
        let mut info = self.info(generation);
        let shared = self.shared;
        drop(self);
        // Read-only transactions can depend on a staged predecessor too.
        let root_writes = shared.wait_durable(generation)?;
        info.pages_written += root_writes;
        info.bytes_written += root_writes * PAGE_SIZE as u64;
        Ok(info)
    }

    fn info(&self, generation: u64) -> CommitInfo {
        let pages_written = self.writer.writes - self.initial_writes;
        CommitInfo {
            generation,
            pages_written,
            bytes_written: pages_written * PAGE_SIZE as u64,
        }
    }

    pub fn rollback(self) {
        // Drop performs the bounded cleanup of unpublished pages.
    }
}

impl Drop for Transaction<'_> {
    fn drop(&mut self) {
        if std::thread::panicking() {
            self.shared.poison();
        }
        if !self.completed {
            for &page in &self.allocated {
                if self.writer.allocation.retire(page, 0).is_err() {
                    self.shared.poison();
                    break;
                }
            }
        }
    }
}

fn check_key(key: &[u8]) -> Result<()> {
    if key.len() > MAX_KEY_SIZE {
        Err(Error::KeyTooLarge)
    } else {
        Ok(())
    }
}

fn child_index(children: &[Child], key: &[u8]) -> usize {
    children
        .partition_point(|child| child.key.as_slice() <= key)
        .saturating_sub(1)
}

fn find(shared: &Shared, mut id: u64, pages: u64, key: &[u8]) -> Result<Option<Value>> {
    check_key(key)?;
    for _ in 0..MAX_DEPTH {
        match shared.node(id, pages)? {
            Node::Leaf(mut entries) => {
                return Ok(entries
                    .binary_search_by(|entry| entry.key.as_slice().cmp(key))
                    .ok()
                    .map(|index| entries.remove(index).value));
            }
            Node::Branch(children) => id = children[child_index(&children, key)].page,
        }
    }
    Err(Error::Corrupt("tree depth limit exceeded"))
}

fn get(shared: &Shared, id: u64, pages: u64, key: &[u8]) -> Result<Option<Vec<u8>>> {
    find(shared, id, pages, key)?
        .map(|value| shared.value(&value, pages))
        .transpose()
}

pub type Record = (Vec<u8>, Vec<u8>);

/// Streams the tree in order. Holds one page per tree level and one result
/// value, rather than collecting all matching keys. Its borrow keeps the
/// source snapshot alive, or prevents mutation of its source transaction.
pub struct Cursor<'a> {
    shared: &'a Shared,
    state: CursorState,
}

struct CursorState {
    pages: u64,
    range: Range,
    stack: Vec<(Vec<Child>, usize)>,
    leaf: std::vec::IntoIter<Entry>,
    done: bool,
}

impl<'a> Cursor<'a> {
    fn new(shared: &'a Shared, root: u64, pages: u64, range: Range) -> Result<Self> {
        Ok(Self {
            shared,
            state: CursorState::new(shared, root, pages, range)?,
        })
    }
}

impl CursorState {
    fn new(shared: &Shared, root: u64, pages: u64, range: Range) -> Result<Self> {
        for bound in [&range.start, &range.end] {
            if let Bound::Included(key) | Bound::Excluded(key) = bound {
                check_key(key)?;
            }
        }
        let mut cursor = Self {
            pages,
            range,
            stack: Vec::new(),
            leaf: Vec::new().into_iter(),
            done: false,
        };
        let start = match &cursor.range.start {
            Bound::Included(key) | Bound::Excluded(key) => key.clone(),
            Bound::Unbounded => Vec::new(),
        };
        cursor.descend(shared, root, Some(&start))?;
        Ok(cursor)
    }

    fn descend(&mut self, shared: &Shared, mut id: u64, key: Option<&[u8]>) -> Result<()> {
        loop {
            if self.stack.len() >= MAX_DEPTH {
                return Err(Error::Corrupt("tree depth limit exceeded"));
            }
            match shared.node(id, self.pages)? {
                Node::Leaf(entries) => {
                    self.leaf = entries.into_iter();
                    return Ok(());
                }
                Node::Branch(children) => {
                    let index = key.map_or(0, |key| child_index(&children, key));
                    id = children[index].page;
                    self.stack.push((children, index));
                }
            }
        }
    }

    fn advance(&mut self, shared: &Shared) -> Result<bool> {
        while let Some((children, index)) = self.stack.last_mut() {
            *index += 1;
            if *index < children.len() {
                let page = children[*index].page;
                self.descend(shared, page, None)?;
                return Ok(true);
            }
            self.stack.pop();
        }
        Ok(false)
    }

    fn next_record(&mut self, shared: &Shared) -> Result<Option<Record>> {
        shared.check()?;
        loop {
            if let Some(entry) = self.leaf.next() {
                let before = match &self.range.start {
                    Bound::Included(key) => entry.key < *key,
                    Bound::Excluded(key) => entry.key <= *key,
                    Bound::Unbounded => false,
                };
                if before {
                    continue;
                }
                let after = match &self.range.end {
                    Bound::Included(key) => entry.key > *key,
                    Bound::Excluded(key) => entry.key >= *key,
                    Bound::Unbounded => false,
                };
                if after {
                    return Ok(None);
                }
                let value = shared.value(&entry.value, self.pages)?;
                return Ok(Some((entry.key, value)));
            }
            if !self.advance(shared)? {
                return Ok(None);
            }
        }
    }
    fn next(&mut self, shared: &Shared) -> Option<Result<Record>> {
        if self.done {
            return None;
        }
        match self.next_record(shared) {
            Ok(Some(record)) => Some(Ok(record)),
            Ok(None) => {
                self.done = true;
                None
            }
            Err(error) => {
                self.done = true;
                Some(Err(error))
            }
        }
    }
}

impl Iterator for Cursor<'_> {
    type Item = Result<Record>;

    fn next(&mut self) -> Option<Self::Item> {
        self.state.next(self.shared)
    }
}

// Recovery walks only committed trees. Allocation and retire state are
// reconstructed on disk, so neither crash cleanup nor open needs an O(N)
// in-memory bitmap, page-id set, or key index.
fn rebuild(
    shared: &Shared,
    allocation: &Allocation,
    root: Root,
    id: u64,
    depth: usize,
    state: u64,
) -> Result<Vec<u8>> {
    if depth >= MAX_DEPTH {
        return Err(Error::Corrupt("tree depth limit exceeded"));
    }
    let node = shared.node(id, root.pages)?;
    allocation.mark_reachable(id, state)?;
    let first = node.first_key().unwrap_or_default().to_vec();
    match node {
        Node::Leaf(entries) => {
            for entry in entries {
                if entry.value.head != 0 {
                    shared.walk_value(&entry.value, root.pages, |page, _| {
                        allocation.mark_reachable(page, state)
                    })?;
                }
            }
        }
        Node::Branch(children) => {
            for child in children {
                let first = rebuild(shared, allocation, root, child.page, depth + 1, state)?;
                if first != child.key {
                    return Err(Error::Corrupt("branch separator differs from child"));
                }
            }
        }
    }
    Ok(first)
}

#[cfg(test)]
mod tests;
