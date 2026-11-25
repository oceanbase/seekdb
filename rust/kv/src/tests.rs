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

use super::*;
use std::cell::{Cell, RefCell};
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::AtomicU64;
use std::sync::mpsc;
use std::thread;
use std::time::Duration;

static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(0);
thread_local! {
    static FAILURE: Cell<Option<&'static str>> = const { Cell::new(None) };
    static SKIP_CRASH: Cell<bool> = const { Cell::new(false) };
    static PAUSE: RefCell<Option<(mpsc::Sender<()>, mpsc::Receiver<()>)>> = const { RefCell::new(None) };
}

pub(super) fn is_crash_stage(stage: &str) -> bool {
    !SKIP_CRASH.get()
        && std::env::var("KV_TEST_CRASH_STAGE").is_ok_and(|selected| selected == stage)
}

pub(super) fn at(stage: &str) -> Result<()> {
    if stage == "before-data-sync" {
        if let Some((entered, resume)) = PAUSE.take() {
            entered.send(()).unwrap();
            resume.recv_timeout(Duration::from_secs(10)).unwrap();
            if FAILURE.get() == Some("panic") {
                panic!("injected publisher panic");
            }
        }
    }
    if is_crash_stage(stage) {
        std::process::exit(91);
    }
    if FAILURE.with(|selected| selected.get() == Some(stage)) {
        return Err(Error::Io(io::Error::other("injected write failure")));
    }
    Ok(())
}

#[test]
fn concurrent_commits_prepare_while_another_commit_syncs() {
    let directory = Directory::new();
    let store = directory.open();
    let (entered, syncing) = mpsc::channel();
    let (resume, proceed) = mpsc::channel();
    let first_store = store.clone();
    let first = thread::spawn(move || {
        PAUSE.set(Some((entered, proceed)));
        write(&first_store, b"first", b"value")
    });
    syncing.recv_timeout(Duration::from_secs(10)).unwrap();
    let (prepared, preparing) = mpsc::channel();
    let mut followers = Vec::new();
    for i in 0..8u8 {
        let store = store.clone();
        let prepared = prepared.clone();
        followers.push(thread::spawn(move || {
            let mut tx = store.begin().unwrap();
            tx.put(&[i], &[i]).unwrap();
            prepared.send(()).unwrap();
            tx.commit().unwrap()
        }));
    }
    let progressed = (0..8).all(|_| preparing.recv_timeout(Duration::from_secs(1)).is_ok());
    assert_eq!(store.get(b"first").unwrap(), None);
    if progressed {
        wait_staged(&store, 9);
    }
    resume.send(()).unwrap();
    let mut pages = first.join().unwrap().pages_written;
    for follower in followers {
        pages += follower.join().unwrap().pages_written;
    }
    // Nine leaf writes and only two root publications (first + eight followers).
    assert_eq!(pages, 11);
    let writer = store.shared.writer.lock().unwrap();
    assert_eq!(
        writer.roots[writer.slot].unwrap().generation
            - writer.roots[1 - writer.slot].unwrap().generation,
        8
    );
    drop(writer);
    assert!(
        progressed,
        "writers stalled behind another transaction's fsync"
    );
    drop(store);
    let store = directory.open();
    for i in 0..8u8 {
        assert_eq!(store.get(&[i]).unwrap(), Some(vec![i]));
    }
}

// Wait for staging, not commit completion: the publisher is deliberately paused.
fn wait_staged(store: &Store, count: u64) {
    let deadline = std::time::Instant::now() + Duration::from_secs(5);
    loop {
        let writer = store.shared.writer.lock().unwrap();
        if writer.head.generation - writer.roots[writer.slot].unwrap().generation == count {
            return;
        }
        drop(writer);
        assert!(
            std::time::Instant::now() < deadline,
            "staging did not progress"
        );
        thread::yield_now();
    }
}

#[test]
fn failed_publisher_wakes_writers_and_dependent_read_only_commits() {
    for stage in ["before-data-sync", "after-root-sync", "panic"] {
        let directory = Directory::new();
        let store = directory.open();
        let (entered, syncing) = mpsc::channel();
        let (resume, proceed) = mpsc::channel();
        let first_store = store.clone();
        let first = thread::spawn(move || {
            PAUSE.set(Some((entered, proceed)));
            FAILURE.with(|failure| failure.set(Some(stage)));
            let mut tx = first_store.begin().unwrap();
            tx.put(b"first", b"value").unwrap();
            tx.commit()
        });
        syncing.recv_timeout(Duration::from_secs(10)).unwrap();
        let (prepared, preparing) = mpsc::channel();
        let (finished, finishing) = mpsc::channel();
        let mut followers = Vec::new();
        for readonly in [true, false] {
            let store = store.clone();
            let prepared = prepared.clone();
            let finished = finished.clone();
            followers.push(thread::spawn(move || {
                let mut tx = store.begin().unwrap();
                assert_eq!(tx.get(b"first").unwrap(), Some(b"value".to_vec()));
                if !readonly {
                    tx.put(b"dependent", b"value").unwrap();
                }
                prepared.send(()).unwrap();
                let result = tx.commit();
                finished.send(()).unwrap();
                result
            }));
        }
        for _ in 0..2 {
            preparing.recv_timeout(Duration::from_secs(5)).unwrap();
        }
        wait_staged(&store, 2);
        assert!(
            finishing.try_recv().is_err(),
            "commit returned before durability"
        );
        assert_eq!(store.get(b"first").unwrap(), None);
        // A rolled back modification must not replace the staged head.
        let mut rollback = store.begin().unwrap();
        rollback.put(b"first", b"rolled back").unwrap();
        rollback.rollback();
        resume.send(()).unwrap();
        let result = first.join();
        if stage == "panic" {
            assert!(result.is_err());
        } else {
            assert!(matches!(result.unwrap(), Err(Error::Io(_))));
        }
        for follower in followers {
            finishing.recv_timeout(Duration::from_secs(5)).unwrap();
            assert!(matches!(follower.join().unwrap(), Err(Error::Poisoned)));
        }
        assert!(matches!(store.begin(), Err(Error::Poisoned)));
        drop(store);
        let store = directory.open();
        assert_eq!(store.get(b"dependent").unwrap(), None);
        assert_eq!(
            store.get(b"first").unwrap(),
            if stage == "after-root-sync" {
                Some(b"value".to_vec())
            } else {
                None
            }
        );
    }
}

#[test]
fn group_crash_child() {
    let Ok(directory) = std::env::var("KV_TEST_GROUP_DIRECTORY") else {
        return;
    };
    let store = Store::open(directory).unwrap();
    let (entered, syncing) = mpsc::channel();
    let (resume, proceed) = mpsc::channel();
    let first_store = store.clone();
    let first = thread::spawn(move || {
        SKIP_CRASH.set(true);
        PAUSE.set(Some((entered, proceed)));
        write(&first_store, b"acknowledged", b"value")
    });
    syncing.recv_timeout(Duration::from_secs(10)).unwrap();
    let mut followers = Vec::new();
    for i in 0..8u8 {
        let store = store.clone();
        followers.push(thread::spawn(move || write(&store, &[i], &vec![2; 65_537])));
    }
    wait_staged(&store, 9);
    resume.send(()).unwrap();
    first.join().unwrap();
    for follower in followers {
        follower.join().unwrap();
    }
    panic!("group crash stage was not reached");
}

#[test]
fn interrupted_group_preserves_acknowledged_root_and_atomic_batch() {
    for stage in [
        "before-data-sync",
        "after-data-sync",
        "during-root-write",
        "after-root-write",
        "after-root-sync",
    ] {
        let directory = Directory::new();
        let store = directory.open();
        for i in 0..8u8 {
            write(&store, &[i], &vec![1; 65_537]);
        }
        drop(store);
        let output = Command::new(std::env::current_exe().unwrap())
            .args(["--exact", "tests::group_crash_child", "--nocapture"])
            .env("KV_TEST_GROUP_DIRECTORY", &directory.0)
            .env("KV_TEST_CRASH_STAGE", stage)
            .output()
            .unwrap();
        assert_eq!(
            output.status.code(),
            Some(91),
            "{stage}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let store = directory.open();
        assert_eq!(store.get(b"acknowledged").unwrap(), Some(b"value".to_vec()));
        let value = store.get(&[0]).unwrap().unwrap();
        for i in 1..8u8 {
            assert_eq!(store.get(&[i]).unwrap().unwrap(), value);
        }
        match stage {
            "after-root-sync" => assert_eq!(value, vec![2; 65_537]),
            "after-root-write" => assert!(value == vec![1; 65_537] || value == vec![2; 65_537]),
            _ => assert_eq!(value, vec![1; 65_537]),
        }
    }
}

#[test]
fn pending_limit_allows_publisher_to_drain_waiting_writers() {
    let directory = Directory::new();
    let store = directory.open();
    let (entered, syncing) = mpsc::channel();
    let (resume, proceed) = mpsc::channel();
    let first_store = store.clone();
    let first = thread::spawn(move || {
        PAUSE.set(Some((entered, proceed)));
        write(&first_store, b"first", b"value")
    });
    syncing.recv_timeout(Duration::from_secs(10)).unwrap();
    let followers: Vec<_> = (0..MAX_PENDING_COMMITS + 8)
        .map(|i| {
            let store = store.clone();
            thread::spawn(move || write(&store, &i.to_be_bytes(), b"value"))
        })
        .collect();
    wait_staged(&store, MAX_PENDING_COMMITS);
    assert_eq!(store.get(b"first").unwrap(), None);
    resume.send(()).unwrap();
    first.join().unwrap();
    for follower in followers {
        follower.join().unwrap();
    }
    drop(store);
    let store = directory.open();
    for i in 0..MAX_PENDING_COMMITS + 8 {
        assert_eq!(
            store.get(&i.to_be_bytes()).unwrap(),
            Some(b"value".to_vec())
        );
    }
}

struct Directory(PathBuf);

impl Directory {
    fn new() -> Self {
        Self(std::env::temp_dir().join(format!(
            "kv-test-{}-{}",
            std::process::id(),
            NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed)
        )))
    }

    fn open(&self) -> Store {
        Store::open(&self.0).unwrap()
    }

    fn size(&self) -> u64 {
        fs::metadata(self.0.join("data")).unwrap().len()
    }
}

impl Drop for Directory {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn write(store: &Store, key: &[u8], value: &[u8]) -> CommitInfo {
    let mut transaction = store.begin().unwrap();
    transaction.put(key, value).unwrap();
    transaction.commit().unwrap()
}

fn rows(snapshot: &Snapshot, range: Range) -> Vec<Record> {
    snapshot
        .scan(range)
        .unwrap()
        .collect::<Result<Vec<_>>>()
        .unwrap()
}

#[test]
fn read_modify_write_rollback_and_restart() {
    let directory = Directory::new();
    let store = directory.open();
    assert_eq!(store.get(b"counter").unwrap(), None);
    let original = store.snapshot().unwrap();
    let mut transaction = store.begin().unwrap();
    transaction.put(b"counter", b"7").unwrap();
    assert_eq!(transaction.get(b"counter").unwrap().unwrap(), b"7");
    transaction.put(b"counter", b"8").unwrap();
    transaction.put(b"other", b"value").unwrap();
    assert_eq!(transaction.scan(Range::all()).unwrap().count(), 2);
    assert_eq!(store.get(b"counter").unwrap(), None);
    transaction.commit().unwrap();
    assert_eq!(original.get(b"counter").unwrap(), None);
    let mut transaction = store.begin().unwrap();
    assert!(transaction.delete(b"counter").unwrap());
    assert!(!transaction.delete(b"missing").unwrap());
    transaction.put(b"other", b"uncommitted").unwrap();
    transaction.rollback();
    assert_eq!(store.get(b"counter").unwrap().unwrap(), b"8");
    drop(original);
    drop(store);
    let store = directory.open();
    assert_eq!(store.get(b"counter").unwrap().unwrap(), b"8");
    assert_eq!(store.get(b"other").unwrap().unwrap(), b"value");
    let info = store.begin().unwrap().commit().unwrap();
    assert_eq!(info.bytes_written, 0);
}

#[test]
fn binary_keys_empty_values_and_scan_bounds() {
    let directory = Directory::new();
    let store = directory.open();
    let keys: &[&[u8]] = &[
        b"",
        b"\0",
        b"a",
        b"a\0",
        b"aa",
        b"b",
        b"\xff",
        b"\xff\xff",
        b"\xff\xff\0",
    ];
    let mut transaction = store.begin().unwrap();
    for key in keys {
        transaction.put(key, b"").unwrap();
    }
    transaction.commit().unwrap();
    let snapshot = store.snapshot().unwrap();
    assert_eq!(
        rows(&snapshot, Range::all())
            .iter()
            .map(|(key, _)| key.as_slice())
            .collect::<Vec<_>>(),
        keys
    );
    assert_eq!(rows(&snapshot, Range::prefix(b"a")).len(), 3);
    assert_eq!(rows(&snapshot, Range::prefix(b"\xff\xff")).len(), 2);
    assert_eq!(rows(&snapshot, Range::prefix(b"")).len(), keys.len());
    assert_eq!(
        rows(
            &snapshot,
            Range {
                start: Bound::Excluded(b"a".to_vec()),
                end: Bound::Included(b"aa".to_vec())
            }
        )
        .len(),
        2
    );
    assert!(rows(
        &snapshot,
        Range {
            start: Bound::Included(b"b".to_vec()),
            end: Bound::Excluded(b"a".to_vec())
        }
    )
    .is_empty());
}

#[test]
fn overflow_values_survive_replacement_deletion_and_restart() {
    let directory = Directory::new();
    let store = directory.open();
    for length in [
        INLINE_VALUE_SIZE,
        INLINE_VALUE_SIZE + 1,
        PAYLOAD_SIZE - 8,
        PAYLOAD_SIZE - 7,
        65_537,
    ] {
        let value: Vec<_> = (0..length).map(|index| index as u8).collect();
        write(&store, b"large", &value);
        assert_eq!(store.get(b"large").unwrap().unwrap(), value);
        let previous = store.snapshot().unwrap();
        write(&store, b"large", b"small");
        assert_eq!(previous.get(b"large").unwrap().unwrap(), value);
        drop(previous);
    }
    let mut transaction = store.begin().unwrap();
    transaction.delete(b"large").unwrap();
    transaction.commit().unwrap();
    drop(store);
    assert_eq!(directory.open().get(b"large").unwrap(), None);
}

#[test]
fn splits_deletions_and_snapshots_match_ordered_reference() {
    let directory = Directory::new();
    let store = directory.open();
    let mut reference = BTreeMap::new();
    let mut random = 37u64;
    for round in 0..80 {
        let previous = store.snapshot().unwrap();
        let previous_rows = rows(&previous, Range::all());
        let mut transaction = store.begin().unwrap();
        for index in 0..80 {
            random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
            let number = (random >> 32) % 600;
            let mut key = vec![b'k'; MAX_KEY_SIZE];
            key[..8].copy_from_slice(&number.to_be_bytes());
            if random & 3 == 0 {
                assert_eq!(
                    transaction.delete(&key).unwrap(),
                    reference.remove(&key).is_some()
                );
            } else {
                let value = vec![(round + index) as u8; 600];
                transaction.put(&key, &value).unwrap();
                reference.insert(key, value);
            }
        }
        transaction.commit().unwrap();
        assert_eq!(rows(&previous, Range::all()), previous_rows);
        assert_eq!(
            rows(&store.snapshot().unwrap(), Range::all()),
            reference
                .iter()
                .map(|(key, value)| (key.clone(), value.clone()))
                .collect::<Vec<_>>()
        );
    }
    let mut transaction = store.begin().unwrap();
    for key in reference.keys() {
        assert!(transaction.delete(key).unwrap());
    }
    transaction.commit().unwrap();
    assert!(rows(&store.snapshot().unwrap(), Range::all()).is_empty());
    drop(store);
    assert!(rows(&directory.open().snapshot().unwrap(), Range::all()).is_empty());
}

#[test]
fn independent_writers_serialize_read_modify_write() {
    let directory = Directory::new();
    let store = directory.open();
    write(&store, b"counter", &0u64.to_le_bytes());
    let workers: Vec<_> = (0..4)
        .map(|_| {
            let store = store.clone();
            thread::spawn(move || {
                for _ in 0..40 {
                    let mut transaction = store.begin().unwrap();
                    let value = u64::from_le_bytes(
                        transaction
                            .get(b"counter")
                            .unwrap()
                            .unwrap()
                            .try_into()
                            .unwrap(),
                    );
                    transaction
                        .put(b"counter", &(value + 1).to_le_bytes())
                        .unwrap();
                    transaction.commit().unwrap();
                }
            })
        })
        .collect();
    for worker in workers {
        worker.join().unwrap();
    }
    assert_eq!(
        store.get(b"counter").unwrap().unwrap(),
        160u64.to_le_bytes()
    );
}

#[test]
fn readers_never_observe_partial_transactions() {
    let directory = Directory::new();
    let store = directory.open();
    let mut transaction = store.begin().unwrap();
    transaction.put(b"a", b"0").unwrap();
    transaction.put(b"b", b"0").unwrap();
    transaction.commit().unwrap();
    let finished = Arc::new(AtomicBool::new(false));
    let writer = {
        let store = store.clone();
        let finished = Arc::clone(&finished);
        thread::spawn(move || {
            for number in 1..100 {
                let mut transaction = store.begin().unwrap();
                let value = number.to_string();
                transaction.put(b"a", value.as_bytes()).unwrap();
                thread::yield_now();
                transaction.put(b"b", value.as_bytes()).unwrap();
                transaction.commit().unwrap();
            }
            finished.store(true, Ordering::Release);
        })
    };
    let mut reads = 0;
    while !finished.load(Ordering::Acquire) || reads < 100 {
        let snapshot = store.snapshot().unwrap();
        assert_eq!(snapshot.get(b"a").unwrap(), snapshot.get(b"b").unwrap());
        reads += 1;
    }
    writer.join().unwrap();
}

#[test]
fn limits_abort_the_whole_transaction_and_release_unpublished_pages() {
    let directory = Directory::new();
    let store = Store::open_with_options(
        &directory.0,
        Options {
            max_transaction_pages: 4,
        },
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    write(&store, b"existing", b"old");
    let snapshot = store.snapshot().unwrap();
    let mut transaction = store.begin().unwrap();
    transaction.put(b"existing", b"new").unwrap();
    assert!(matches!(
        transaction.put(b"too-large", &vec![9; 32_000]),
        Err(Error::TransactionTooLarge)
    ));
    assert!(matches!(
        transaction.commit(),
        Err(Error::TransactionAborted)
    ));
    assert_eq!(snapshot.get(b"existing").unwrap().unwrap(), b"old");
    drop(snapshot);
    assert_eq!(store.get(b"existing").unwrap().unwrap(), b"old");
    write(&store, b"existing", b"accepted");
    let mut transaction = store.begin().unwrap();
    assert!(matches!(
        transaction.put(&vec![0; MAX_KEY_SIZE + 1], b"v"),
        Err(Error::KeyTooLarge)
    ));
    drop(transaction);
    assert_eq!(store.get(b"existing").unwrap().unwrap(), b"accepted");
}

#[test]
fn pinned_roots_and_fallback_roots_protect_pages_then_allow_reuse() {
    let directory = Directory::new();
    let store = directory.open();
    write(&store, b"key", &vec![1; 20_000]);
    let snapshot = store.snapshot().unwrap();
    for number in 2..40 {
        write(&store, b"key", &vec![number; 20_000]);
    }
    assert_eq!(snapshot.get(b"key").unwrap().unwrap(), vec![1; 20_000]);
    drop(snapshot);
    let size = directory.size();
    for number in 40..100 {
        write(&store, b"key", &vec![number; 20_000]);
    }
    assert_eq!(directory.size(), size);
    drop(store);
    let store = directory.open();
    for number in 100..140 {
        write(&store, b"key", &vec![number; 20_000]);
    }
    assert_eq!(directory.size(), size);
}

#[test]
fn handles_keep_exclusive_ownership_until_last_snapshot_drops() {
    let directory = Directory::new();
    let store = directory.open();
    let snapshot = store.snapshot().unwrap();
    assert!(matches!(Store::open(&directory.0), Err(Error::Busy)));
    drop(store);
    assert!(matches!(Store::open(&directory.0), Err(Error::Busy)));
    assert!(snapshot.get(b"k").unwrap().is_none());
    drop(snapshot);
    drop(directory.open());
}

#[cfg(unix)]
#[test]
fn closing_store_releases_lock_while_a_child_waits_to_exec() {
    use std::io::{Read, Write};
    use std::os::fd::AsRawFd;
    use std::os::unix::net::UnixStream;
    use std::os::unix::process::CommandExt;
    use std::time::Duration;

    unsafe extern "C" {
        fn read(fd: i32, buffer: *mut std::ffi::c_void, length: usize) -> isize;
        fn write(fd: i32, buffer: *const std::ffi::c_void, length: usize) -> isize;
    }

    let directory = Directory::new();
    let store = directory.open();
    let (mut parent, child) = UnixStream::pair().unwrap();
    parent
        .set_read_timeout(Some(Duration::from_secs(10)))
        .unwrap();
    let process = thread::spawn(move || {
        let fd = child.as_raw_fd();
        let mut command = Command::new(std::env::current_exe().unwrap());
        command.args(["--exact", "tests::crash_child"]);
        unsafe {
            command.pre_exec(move || {
                // Use only async-signal-safe syscalls between fork and exec.
                let mut byte = 1_u8;
                if write(fd, (&byte as *const u8).cast(), 1) != 1
                    || read(fd, (&mut byte as *mut u8).cast(), 1) != 1
                {
                    return Err(io::Error::last_os_error());
                }
                Ok(())
            });
        }
        command.output().unwrap()
    });
    let mut ready = [0];
    parent.read_exact(&mut ready).unwrap();
    // The child still has the inherited CLOEXEC descriptor at this point.
    drop(store);
    let reopened = Store::open(&directory.0);
    parent.write_all(&[1]).unwrap();
    assert!(process.join().unwrap().status.success());
    assert!(
        reopened.is_ok(),
        "lock retained by child: {:?}",
        reopened.err()
    );
}

#[test]
fn torn_latest_root_recovers_fallback_even_after_page_reuse() {
    let directory = Directory::new();
    let store = directory.open();
    for number in 0..100u64 {
        write(&store, b"key", &number.to_le_bytes());
    }
    let slot = store.shared.writer.lock().unwrap().slot;
    drop(store);
    let file = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(directory.0.join("data"))
        .unwrap();
    write_all_at(&file, &[0; 8], slot as u64 * PAGE_SIZE as u64).unwrap();
    file.sync_all().unwrap();
    drop(file);
    assert_eq!(
        directory.open().get(b"key").unwrap().unwrap(),
        98u64.to_le_bytes()
    );
}

#[test]
fn corrupted_storage_is_rejected_without_reinitializing_it() {
    let directory = Directory::new();
    let store = directory.open();
    write(&store, b"important", b"record");
    let root = store.shared.visible.lock().unwrap().root;
    drop(store);
    let file = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(directory.0.join("data"))
        .unwrap();
    write_all_at(&file, b"broken", root.page * PAGE_SIZE as u64 + 30).unwrap();
    file.sync_all().unwrap();
    drop(file);
    let bytes = fs::read(directory.0.join("data")).unwrap();
    assert!(matches!(Store::open(&directory.0), Err(Error::Corrupt(_))));
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), bytes);
}

#[test]
fn partial_uncommitted_append_is_discarded_after_recovery() {
    let directory = Directory::new();
    let store = directory.open();
    write(&store, b"key", b"committed");
    drop(store);
    let size = directory.size();
    let file = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(directory.0.join("data"))
        .unwrap();
    write_all_at(&file, &[42; 100], size).unwrap();
    file.sync_all().unwrap();
    drop(file);
    let store = directory.open();
    assert_eq!(store.get(b"key").unwrap().unwrap(), b"committed");
    assert_eq!(directory.size(), size);
}

#[test]
fn two_invalid_roots_are_rejected_without_overwriting_data() {
    let directory = Directory::new();
    let store = directory.open();
    write(&store, b"key", b"committed");
    drop(store);
    let file = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(directory.0.join("data"))
        .unwrap();
    for slot in 0..2 {
        write_all_at(&file, &[0; 8], slot * PAGE_SIZE as u64).unwrap();
    }
    file.sync_all().unwrap();
    drop(file);
    let bytes = fs::read(directory.0.join("data")).unwrap();
    assert!(matches!(Store::open(&directory.0), Err(Error::Corrupt(_))));
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), bytes);
}

#[test]
fn failed_commit_requires_reopen_and_never_frees_published_pages() {
    for stage in ["before-data-sync", "after-root-sync"] {
        let directory = Directory::new();
        let store = directory.open();
        write(&store, b"key", b"old");
        let mut transaction = store.begin().unwrap();
        transaction.put(b"key", b"new").unwrap();
        FAILURE.with(|failure| failure.set(Some(stage)));
        assert!(matches!(transaction.commit(), Err(Error::Io(_))));
        FAILURE.with(|failure| failure.set(None));
        assert!(matches!(store.get(b"key"), Err(Error::Poisoned)));
        assert!(matches!(store.begin(), Err(Error::Poisoned)));
        drop(store);
        let expected = if stage == "before-data-sync" {
            b"old"
        } else {
            b"new"
        };
        assert_eq!(directory.open().get(b"key").unwrap().unwrap(), expected);
    }
}

#[test]
fn crash_child() {
    let Ok(directory) = std::env::var("KV_TEST_CRASH_DIRECTORY") else {
        return;
    };
    let store = Store::open(directory).unwrap();
    let mut transaction = store.begin().unwrap();
    transaction.put(b"a", &vec![2; 65_537]).unwrap();
    transaction.put(b"b", &vec![2; 65_537]).unwrap();
    transaction.commit().unwrap();
    panic!("crash stage was not reached");
}

#[test]
fn interrupted_commit_recovers_all_or_none_in_a_new_process() {
    for stage in [
        "before-data-sync",
        "after-data-sync",
        "during-root-write",
        "after-root-write",
        "after-root-sync",
    ] {
        let directory = Directory::new();
        let store = directory.open();
        let mut transaction = store.begin().unwrap();
        transaction.put(b"a", &vec![1; 65_537]).unwrap();
        transaction.put(b"b", &vec![1; 65_537]).unwrap();
        transaction.commit().unwrap();
        drop(store);
        let output = Command::new(std::env::current_exe().unwrap())
            .args(["--exact", "tests::crash_child", "--nocapture"])
            .env("KV_TEST_CRASH_DIRECTORY", &directory.0)
            .env("KV_TEST_CRASH_STAGE", stage)
            .output()
            .unwrap();
        assert_eq!(
            output.status.code(),
            Some(91),
            "stage {stage}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let store = directory.open();
        let snapshot = store.snapshot().unwrap();
        let left = snapshot.get(b"a").unwrap().unwrap();
        let right = snapshot.get(b"b").unwrap().unwrap();
        assert_eq!(left, right, "partially committed transaction at {stage}");
        match stage {
            "after-root-sync" => assert_eq!(left, vec![2; 65_537]),
            "after-root-write" => assert!(left == vec![1; 65_537] || left == vec![2; 65_537]),
            _ => assert_eq!(left, vec![1; 65_537]),
        }
    }
}

#[test]
fn each_directory_sync_site_preserves_failure_stage_and_path() {
    use crate::file::{SyncFault, SYNC_FAULT};
    use file_io::DirectorySyncError;
    for site in 0..4 {
        for stage in 0..4 {
            let directory = Directory::new();
            let source = std::io::Error::from_raw_os_error(13);
            let failure = match stage {
                0 => DirectorySyncError::Open(source),
                1 => DirectorySyncError::Flush {
                    source,
                    close_error: None,
                },
                2 => DirectorySyncError::Flush {
                    source,
                    close_error: Some(std::io::Error::from_raw_os_error(5)),
                },
                _ => DirectorySyncError::Close(source),
            };
            SYNC_FAULT.with(|slot| {
                *slot.borrow_mut() = SyncFault {
                    fail_at: Some(site),
                    failure: Some(failure),
                    calls: 0,
                }
            });
            let mut summary = file_io::DirectorySyncSummary::default();
            let error = Store::open_with_options(
                &directory.0,
                Options::default(),
                file_io::DirectoryDurability::Required,
                &mut summary,
            )
            .err()
            .unwrap();
            let Error::DirectorySync { path, source } = error else {
                panic!("sync failure lost");
            };
            let parent = directory.0.parent().unwrap();
            assert_eq!(
                path,
                match site {
                    0 => parent.parent().unwrap(),
                    1 => parent,
                    _ => &directory.0,
                }
            );
            assert_eq!(source.primary().raw_os_error(), Some(13));
            match stage {
                0 => assert!(matches!(source, DirectorySyncError::Open(_))),
                1 => assert!(matches!(
                    source,
                    DirectorySyncError::Flush {
                        close_error: None,
                        ..
                    }
                )),
                2 => assert!(
                    matches!(source, DirectorySyncError::Flush { close_error: Some(e), .. } if e.raw_os_error() == Some(5))
                ),
                _ => assert!(matches!(source, DirectorySyncError::Close(_))),
            }
            assert_eq!(summary.count(), 0);
            SYNC_FAULT.with(|slot| assert_eq!(slot.borrow().calls, site + 1));
        }
    }
}

#[test]
fn failed_publication_sync_keeps_data_and_reopen_retries_sync() {
    use crate::file::{SyncFault, SYNC_FAULT};
    let directory = Directory::new();
    SYNC_FAULT.with(|slot| {
        *slot.borrow_mut() = SyncFault {
            fail_at: Some(2),
            failure: Some(file_io::DirectorySyncError::Flush {
                source: std::io::Error::from_raw_os_error(5),
                close_error: None,
            }),
            calls: 0,
        }
    });
    assert!(matches!(
        Store::open(&directory.0),
        Err(Error::DirectorySync { .. })
    ));
    let before = fs::read(directory.0.join("data")).unwrap();
    SYNC_FAULT.with(|slot| *slot.borrow_mut() = SyncFault::default());
    drop(Store::open(&directory.0).unwrap());
    SYNC_FAULT.with(|slot| assert_eq!(slot.borrow().calls, 2));
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), before);
}

#[cfg(windows)]
#[test]
fn compatibility_summary_remains_available_after_later_corruption() {
    let directory = Directory::new();
    fs::create_dir_all(&directory.0).unwrap();
    fs::write(directory.0.join("data"), [0_u8; PAGE_SIZE * 3]).unwrap();
    let mut summary = file_io::DirectorySyncSummary::default();
    let result = Store::open_with_options(
        &directory.0,
        Options::default(),
        file_io::DirectoryDurability::WindowsCompatible,
        &mut summary,
    );
    assert!(matches!(result, Err(Error::Corrupt(_))));
    eprintln!("Windows failed-open directory observations: {summary:?}");
    assert_eq!(summary.first().is_none(), summary.count() == 0);
    if let Some(first) = summary.first() {
        assert!(summary.count() >= 1);
        assert!(first.source.raw_os_error().is_some());
        assert!(!first.path.as_os_str().is_empty());
    }
}
