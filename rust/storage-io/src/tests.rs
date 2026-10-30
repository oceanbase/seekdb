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
use std::fs::{self, OpenOptions};
use std::io;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

const BLOCK: u64 = HEADER_PAGE_SIZE as u64;

struct Directory(PathBuf);

impl Directory {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = std::env::temp_dir().join(format!(
            "storage-io-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }

    fn path(&self) -> PathBuf {
        self.0.join("block_file")
    }

    fn open(&self, count: u64, punch_holes: bool) -> UnmarkedBlockFile {
        UnmarkedBlockFile::open(
            &self.path(),
            BlockConfig {
                block_size: BLOCK,
                initial_size: BLOCK * count,
                direct_io: false,
                punch_holes,
            },
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        )
        .unwrap()
    }
}

impl Drop for Directory {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}

fn index(value: u64) -> BlockIndex {
    BlockIndex::new(value).unwrap()
}

#[test]
#[cfg(target_endian = "little")]
fn header_matches_cpp_database_fixture() {
    let expected = [
        0x4c, 0x49, 0x46, 0x4b, 0x43, 0x4f, 0x4c, 0x42, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0, 0,
        0, 0, 0, 0x67, 0x86, 0x1d, 0x2b, 0, 0, 0, 0,
    ];
    let mut page = [0xa5; HEADER_PAGE_SIZE];
    encode_block_header(2 * 1024 * 1024, &mut page).unwrap();
    assert_eq!(&page[..32], &expected);
    assert!(page[32..].iter().all(|byte| *byte == 0));
    validate_block_header(2 * 1024 * 1024, &page).unwrap();
}

#[test]
fn invalid_header_and_checksum_remain_distinct() {
    let mut page = [0; HEADER_PAGE_SIZE];
    encode_block_header(BLOCK, &mut page).unwrap();
    for offset in [0, 8, 16] {
        let mut corrupt = page;
        corrupt[offset] ^= 1;
        assert!(matches!(
            validate_block_header(BLOCK, &corrupt),
            Err(BlockError::InvalidHeader)
        ));
    }
    page[24] ^= 1;
    assert!(matches!(
        validate_block_header(BLOCK, &page),
        Err(BlockError::ChecksumMismatch)
    ));
    assert!(matches!(
        validate_block_header(BLOCK, &page[..31]),
        Err(BlockError::InvalidHeader)
    ));
}

#[test]
fn recovery_reads_before_marking_and_preserves_live_blocks() {
    let directory = Directory::new();
    let owner = directory.open(4, false);
    owner
        .region(index(2), 0..BLOCK)
        .unwrap()
        .write_at(b"recovered", 17)
        .result
        .unwrap();
    owner.sync().unwrap();
    drop(owner);
    let mut reopened = directory.open(99, true);
    let region = reopened.region(index(2), 17..26).unwrap();
    let mut bytes = [0; 9];
    assert_eq!(region.read_at(&mut bytes, 0).bytes, 9);
    assert_eq!(&bytes, b"recovered");
    reopened.mark(&[index(2), index(2)]).unwrap();
    let ready = reopened.finish_marking().unwrap();
    assert_eq!(ready.snapshot().capacity, BLOCK * 4);
    assert_eq!(ready.allocate_many(10).unwrap(), [index(1), index(3)]);
    assert!(matches!(ready.allocate(), Err(BlockError::Exhausted)));
    region.read_at(&mut bytes, 0).result.unwrap();
    assert_eq!(&bytes, b"recovered");
}

#[test]
#[cfg(any(target_os = "linux", target_os = "android"))]
fn direct_block_file_reopens_and_transfers_aligned_regions() {
    let directory = Directory::new();
    let config = BlockConfig {
        block_size: BLOCK,
        initial_size: BLOCK * 3,
        direct_io: true,
        punch_holes: false,
    };
    let owner = UnmarkedBlockFile::open(
        &directory.path(),
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap()
    .finish_marking()
    .unwrap();
    let block = owner.allocate().unwrap();
    let mut buffer = file_io::AlignedBuffer::zeroed(BLOCK as usize, HEADER_PAGE_SIZE).unwrap();
    buffer.as_mut_slice().fill(0x35);
    owner
        .region(block, 0..BLOCK)
        .unwrap()
        .write_at(buffer.as_slice(), 0)
        .result
        .unwrap();
    owner.sync().unwrap();
    drop(owner);
    let owner = UnmarkedBlockFile::open(
        &directory.path(),
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    buffer.as_mut_slice().fill(0);
    let read = owner
        .region(block, 0..BLOCK)
        .unwrap()
        .read_at(buffer.as_mut_slice(), 0);
    read.result.unwrap();
    assert_eq!(read.bytes, BLOCK as usize);
    assert!(buffer.as_slice().iter().all(|byte| *byte == 0x35));
}

#[test]
fn failed_marking_cannot_publish_partial_live_set() {
    let directory = Directory::new();
    let mut owner = directory.open(4, false);
    owner.mark(&[index(1)]).unwrap();
    assert!(matches!(
        owner.mark(&[index(2), index(4)]),
        Err(BlockError::InvalidBlock(4))
    ));
    assert!(matches!(
        owner.mark(&[index(3)]),
        Err(BlockError::MarkingAborted)
    ));
    assert!(matches!(
        owner.finish_marking(),
        Err(BlockError::MarkingAborted)
    ));
    let mut owner = directory.open(4, false);
    owner.mark(&[index(1)]).unwrap();
    owner.abort_marking();
    owner.restart_marking();
    owner.mark(&[index(2)]).unwrap();
    assert_eq!(
        owner.finish_marking().unwrap().allocate_many(4).unwrap(),
        [index(1), index(3)]
    );
}

#[test]
fn allocation_is_fifo_and_returns_short_batches() {
    let directory = Directory::new();
    let owner = directory.open(4, false).finish_marking().unwrap();
    assert_eq!(owner.allocate_many(0).unwrap(), []);
    assert_eq!(
        owner.allocate_many(20).unwrap(),
        [index(1), index(2), index(3)]
    );
    owner.release(index(2)).unwrap();
    owner.release(index(1)).unwrap();
    assert!(matches!(
        owner.release(index(1)),
        Err(BlockError::NotAllocated(1))
    ));
    assert_eq!(owner.allocate_many(20).unwrap(), [index(2), index(1)]);
    assert!(matches!(
        owner.release(index(u32::MAX as u64 + 2)),
        Err(BlockError::InvalidBlock(_))
    ));
    assert!(BlockIndex::new(0).is_err());
}

#[test]
fn recovery_lease_delays_reuse_when_marking_does_not_keep_block() {
    let directory = Directory::new();
    let owner = directory.open(3, true);
    let region = owner.region(index(1), 0..BLOCK).unwrap();
    let retained = region.subregion(4..8).unwrap().clone();
    let owner = owner.finish_marking().unwrap();
    assert_eq!(owner.snapshot().pending_release_blocks, 1);
    assert_eq!(owner.allocate().unwrap(), index(2));
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    drop(region);
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    drop(retained);
    assert_eq!(owner.allocate().unwrap(), index(1));
    assert_eq!(owner.snapshot().pending_release_blocks, 0);
}

#[test]
fn late_write_cannot_cross_release_and_reallocation() {
    let directory = Directory::new();
    let owner = directory.open(2, true).finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 0..BLOCK).unwrap();
    let second = owner.region(block, 0..BLOCK).unwrap();
    let (permit, start) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        start.recv().unwrap();
        region.write_at(b"late physical write", 0).result.unwrap();
    });
    assert!(matches!(
        owner.release(block).unwrap(),
        ReleaseOutcome::Pending
    ));
    assert!(owner.region(block, 0..BLOCK).is_err());
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    permit.send(()).unwrap();
    worker.join().unwrap();
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    drop(second);
    assert_eq!(owner.allocate().unwrap(), block);
    let next = owner.region(block, 0..BLOCK).unwrap();
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        let mut bytes = [0xff; 19];
        next.read_at(&mut bytes, 0).result.unwrap();
        assert_eq!(bytes, [0; 19]);
    }
    next.write_at(b"new allocation", 0).result.unwrap();
    owner.reclaim_released();
    let mut bytes = [0; 14];
    next.read_at(&mut bytes, 0).result.unwrap();
    assert_eq!(&bytes, b"new allocation");
}

#[test]
fn cloned_and_nested_subregions_preserve_offsets_and_io_bounds() {
    let directory = Directory::new();
    let owner = directory.open(2, false).finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 8..24).unwrap();
    region.write_at(b"abcdefghijklmnop", 0).result.unwrap();
    let child = region.subregion(4..12).unwrap();
    let nested = child.subregion(2..6).unwrap();
    let cloned = nested.clone();
    assert_eq!(child.len(), 8);
    assert_eq!(nested.len(), 4);
    assert!(!nested.is_empty());
    let mut bytes = [0; 4];
    assert_eq!(cloned.read_at(&mut bytes, 0).bytes, 4);
    assert_eq!(&bytes, b"ghij");
    nested.write_at(b"XY", 1).result.unwrap();
    cloned.read_at(&mut bytes, 0).result.unwrap();
    assert_eq!(&bytes, b"gXYj");
    for (offset, bytes) in [(3, &b"XX"[..]), (4, &b"X"[..]), (u64::MAX, &b"X"[..])] {
        let failed = cloned.write_at(bytes, offset);
        assert_eq!(failed.bytes, 0);
        assert_eq!(
            failed.result.unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
    }
    let failed = nested.read_at(&mut bytes, 1);
    assert_eq!(failed.bytes, 0);
    assert_eq!(
        failed.result.unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
    let mut whole = [0; 16];
    region.read_at(&mut whole, 0).result.unwrap();
    assert_eq!(&whole, b"abcdefgXYjklmnop");
    let mut prefix = [0; 8];
    owner
        .region(block, 0..8)
        .unwrap()
        .read_at(&mut prefix, 0)
        .result
        .unwrap();
    assert_eq!(prefix, [0; 8]);
    let mut suffix = [0; 8];
    owner
        .region(block, 24..32)
        .unwrap()
        .read_at(&mut suffix, 0)
        .result
        .unwrap();
    assert_eq!(suffix, [0; 8]);
}

#[test]
fn subregions_reject_invalid_ranges_and_allow_empty_intervals() {
    let directory = Directory::new();
    let owner = directory.open(2, false).finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 8..24).unwrap();
    for (start, end) in [
        (5, 4),
        (0, 17),
        (17, 17),
        (1, u64::MAX),
        (u64::MAX, u64::MAX),
    ] {
        assert!(matches!(
            region.subregion(start..end),
            Err(BlockError::InvalidInput(_))
        ));
    }
    for relative in [0..0, 8..8, 16..16] {
        let empty = region.subregion(relative).unwrap();
        assert_eq!(empty.len(), 0);
        assert!(empty.is_empty());
        assert!(empty.subregion(0..0).unwrap().is_empty());
        assert!(matches!(
            empty.subregion(0..1),
            Err(BlockError::InvalidInput(_))
        ));
        empty.read_at(&mut [], 0).result.unwrap();
        empty.write_at(&[], 0).result.unwrap();
        assert_eq!(
            empty.write_at(b"X", 0).result.unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
        assert_eq!(
            empty.write_at(&[], 1).result.unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
    }
    assert_eq!(region.subregion(0..16).unwrap().len(), 16);
    assert!(owner
        .region(block, BLOCK..BLOCK)
        .unwrap()
        .subregion(0..0)
        .unwrap()
        .is_empty());
}

#[test]
fn block_reuse_waits_for_last_clone_or_subregion() {
    let directory = Directory::new();
    let owner = directory.open(2, false).finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 8..24).unwrap();
    let cloned = region.clone();
    let child = region.subregion(4..12).unwrap();
    let last = child.subregion(8..8).unwrap();
    assert!(matches!(
        owner.release(block).unwrap(),
        ReleaseOutcome::Pending
    ));
    drop(region);
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    cloned.write_at(b"retained", 0).result.unwrap();
    drop(cloned);
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    let mut bytes = [0; 4];
    child.read_at(&mut bytes, 0).result.unwrap();
    assert_eq!(&bytes, b"ined");
    drop(child);
    owner.reclaim_released();
    assert_eq!(owner.snapshot().pending_release_blocks, 1);
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    drop(last);
    assert_eq!(owner.allocate().unwrap(), block);
    assert_eq!(owner.snapshot().pending_release_blocks, 0);
    owner.reclaim_released();
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    assert!(matches!(
        owner.release(block).unwrap(),
        ReleaseOutcome::Reusable {
            reclamation_error: None
        }
    ));
    assert_eq!(owner.allocate().unwrap(), block);
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
}

#[test]
fn region_outlives_owner_and_rejects_out_of_bounds_io() {
    let directory = Directory::new();
    let owner = directory.open(3, false).finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    assert!(owner.region(index(u32::MAX as u64 + 2), 0..1).is_err());
    assert!(owner.region(block, 1..BLOCK + 1).is_err());
    let region = owner.region(block, 8..12).unwrap();
    drop(owner);
    let failed = region.write_at(b"overflow", 0);
    assert_eq!(failed.bytes, 0);
    assert_eq!(
        failed.result.unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
    region.write_at(b"kept", 0).result.unwrap();
    let mut bytes = [0; 4];
    region.read_at(&mut bytes, 0).result.unwrap();
    assert_eq!(&bytes, b"kept");
}

#[test]
fn reopening_through_an_alias_waits_for_all_regions_to_retire() {
    let directory = Directory::new();
    let owner = directory.open(2, true).finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 0..BLOCK).unwrap();
    let cloned = region.clone();
    let child = region.subregion(0..10).unwrap();
    drop(region);
    let alias = directory.0.join("alias");
    #[cfg(not(target_os = "emscripten"))]
    fs::hard_link(directory.path(), &alias).unwrap();
    #[cfg(target_os = "emscripten")]
    std::os::unix::fs::symlink(directory.path(), &alias).unwrap();
    drop(owner);
    let config = BlockConfig {
        block_size: BLOCK,
        initial_size: BLOCK * 2,
        direct_io: false,
        punch_holes: true,
    };
    for path in [directory.path(), alias.clone()] {
        match UnmarkedBlockFile::open(
            &path,
            config,
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        ) {
            Err(BlockError::Io(error)) => assert_eq!(error.kind(), io::ErrorKind::WouldBlock),
            _ => panic!("reopen must not bypass a live region lease"),
        }
    }
    child.write_at(b"last write", 0).result.unwrap();
    drop(cloned);
    match UnmarkedBlockFile::open(
        &alias,
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    ) {
        Err(BlockError::Io(error)) => assert_eq!(error.kind(), io::ErrorKind::WouldBlock),
        _ => panic!("reopen must not bypass a live subregion lease"),
    }
    drop(child);
    let owner = UnmarkedBlockFile::open(
        &alias,
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    let mut data = [0; 10];
    owner
        .region(block, 0..BLOCK)
        .unwrap()
        .read_at(&mut data, 0)
        .result
        .unwrap();
    assert_eq!(&data, b"last write");
    assert_eq!(owner.finish_marking().unwrap().allocate().unwrap(), block);
}

#[test]
fn growth_preserves_free_order_and_data() {
    let directory = Directory::new();
    let owner = directory.open(4, false).finish_marking().unwrap();
    let first = owner.allocate().unwrap();
    owner
        .region(first, 0..BLOCK)
        .unwrap()
        .write_at(b"keep", 0)
        .result
        .unwrap();
    owner.release(first).unwrap();
    owner.grow(BLOCK * 6, SpaceBudget::default()).unwrap();
    assert_eq!(
        owner.allocate_many(9).unwrap(),
        [index(2), index(3), index(1), index(4), index(5)]
    );
    let mut bytes = [0; 4];
    owner
        .region(first, 0..BLOCK)
        .unwrap()
        .read_at(&mut bytes, 0)
        .result
        .unwrap();
    assert_eq!(&bytes, b"keep");
    assert!(owner.grow(BLOCK * 4, SpaceBudget::default()).is_err());
    owner
        .grow(
            BLOCK * 6,
            SpaceBudget {
                reserved_bytes: u64::MAX,
            },
        )
        .unwrap();
}

#[test]
fn failed_growth_does_not_publish_capacity_and_retry_preserves_tail() {
    let directory = Directory::new();
    let owner = directory.open(3, false).finish_marking().unwrap();
    let before = owner.snapshot();
    let error = owner.grow_with(BLOCK * 5, SpaceBudget::default(), |file, range| {
        file_io::extend_file(file, range.end)?;
        file_io::write_all_at(file, b"tail", BLOCK * 4)?;
        Err(io::Error::other("injected sync failure"))
    });
    assert!(error.is_err());
    assert_eq!(owner.snapshot(), before);
    assert_eq!(fs::metadata(directory.path()).unwrap().len(), BLOCK * 5);
    owner.grow(BLOCK * 5, SpaceBudget::default()).unwrap();
    assert_eq!(
        owner.allocate_many(9).unwrap(),
        [index(1), index(2), index(3), index(4)]
    );
    let mut bytes = [0; 4];
    owner
        .region(index(4), 0..BLOCK)
        .unwrap()
        .read_at(&mut bytes, 0)
        .result
        .unwrap();
    assert_eq!(&bytes, b"tail");
    drop(owner);
    assert_eq!(directory.open(3, false).snapshot().capacity, BLOCK * 5);
}

#[test]
fn reserved_space_refuses_growth_without_file_change() {
    let directory = Directory::new();
    let owner = directory.open(3, false).finish_marking().unwrap();
    let before = owner.snapshot();
    assert!(matches!(
        owner.grow(
            BLOCK * 5,
            SpaceBudget {
                reserved_bytes: u64::MAX
            }
        ),
        Err(BlockError::Exhausted)
    ));
    assert_eq!(owner.snapshot(), before);
    assert_eq!(fs::metadata(directory.path()).unwrap().len(), BLOCK * 3);
}

#[test]
fn partial_creation_extension_is_closed_then_removed() {
    let directory = Directory::new();
    let result = UnmarkedBlockFile::open_with(
        &directory.path(),
        BlockConfig {
            block_size: BLOCK,
            initial_size: BLOCK * 3,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
        |file, _| {
            file_io::write_all_at(file, b"partial tail", BLOCK + 17)?;
            Err(io::Error::other("injected partial extension failure"))
        },
        |parent, name, identity| {
            let path = directory.path();
            assert_eq!(fs::metadata(&path).unwrap().len(), BLOCK + 29);
            let contender = OpenOptions::new().read(true).write(true).open(&path)?;
            drop(
                file_io::ExclusiveFile::try_new(contender)
                    .expect("created handle must already be closed"),
            );
            parent.remove_created_file(name, identity)
        },
    );
    assert!(matches!(result, Err(BlockError::Io(ref error))
        if error.to_string() == "injected partial extension failure"));
    assert!(!directory.path().exists());
    drop(directory.open(3, false));
    assert_eq!(directory.open(99, false).snapshot().capacity, BLOCK * 3);
}

#[test]
fn failed_creation_cleanup_preserves_both_errors_and_partial_file() {
    for cleanup in [
        io::Error::from_raw_os_error(5),
        io::Error::new(io::ErrorKind::PermissionDenied, "injected removal failure"),
    ] {
        let directory = Directory::new();
        let cleanup_code = cleanup.raw_os_error();
        let cleanup_message = cleanup.to_string();
        let result = UnmarkedBlockFile::open_with(
            &directory.path(),
            BlockConfig {
                block_size: BLOCK,
                initial_size: BLOCK * 3,
                direct_io: false,
                punch_holes: false,
            },
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
            |file, _| {
                file_io::write_all_at(file, b"partial", BLOCK)?;
                Err(io::Error::from_raw_os_error(28))
            },
            |parent, name, identity| {
                let path = directory.path();
                let contender = OpenOptions::new().read(true).write(true).open(&path)?;
                drop(
                    file_io::ExclusiveFile::try_new(contender)
                        .expect("created handle must already be closed"),
                );
                let _ = (parent, name, identity);
                Err(cleanup)
            },
        );
        let error = result.err().expect("extension and cleanup must fail");
        let BlockError::CreationCleanup { primary, cleanup } = &error else {
            panic!("missing cleanup error: {error}");
        };
        assert!(
            matches!(primary.as_ref(), BlockError::Io(error) if error.raw_os_error() == Some(28))
        );
        assert_eq!(
            cleanup.remove.as_ref().unwrap().raw_os_error(),
            cleanup_code
        );
        assert_eq!(
            cleanup.remove.as_ref().unwrap().to_string(),
            cleanup_message
        );
        let source = std::error::Error::source(&error)
            .unwrap()
            .downcast_ref::<BlockError>()
            .unwrap();
        assert!(std::ptr::eq(source, primary.as_ref()));
        assert!(error.to_string().contains(&primary.to_string()));
        assert!(error.to_string().contains(&cleanup_message));
        let bytes = fs::read(directory.path()).unwrap();
        assert_eq!(bytes.len(), BLOCK as usize + 7);
        assert_eq!(&bytes[BLOCK as usize..], b"partial");
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(directory.path())
            .unwrap();
        drop(file_io::ExclusiveFile::try_new(file).unwrap());
    }
}

#[test]
fn opening_valid_existing_file_never_extends_or_removes_it() {
    let directory = Directory::new();
    let owner = directory.open(3, false);
    owner
        .region(index(1), 0..BLOCK)
        .unwrap()
        .write_at(b"live", 0)
        .result
        .unwrap();
    owner.sync().unwrap();
    drop(owner);
    let before = fs::read(directory.path()).unwrap();
    let reopened = UnmarkedBlockFile::open_with(
        &directory.path(),
        BlockConfig {
            block_size: BLOCK,
            initial_size: BLOCK * 99,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
        |_, _| panic!("existing file must not be extended during open"),
        |_, _, _| panic!("existing file must not be removed"),
    )
    .unwrap();
    assert_eq!(reopened.snapshot().capacity, BLOCK * 3);
    assert_eq!(fs::read(directory.path()).unwrap(), before);
}

#[test]
fn opening_invalid_existing_file_does_not_reinitialize_it() {
    let directory = Directory::new();
    let mut data = vec![0x5a; HEADER_PAGE_SIZE * 3];
    fs::write(directory.path(), &data).unwrap();
    let config = BlockConfig {
        block_size: BLOCK,
        initial_size: BLOCK * 3,
        direct_io: false,
        punch_holes: false,
    };
    assert!(matches!(
        UnmarkedBlockFile::open(
            &directory.path(),
            config,
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default()
        ),
        Err(BlockError::InvalidHeader)
    ));
    assert_eq!(fs::read(directory.path()).unwrap(), data);
    encode_block_header(BLOCK, &mut data[..HEADER_PAGE_SIZE]).unwrap();
    data[24] ^= 1;
    fs::write(directory.path(), &data).unwrap();
    assert!(matches!(
        UnmarkedBlockFile::open(
            &directory.path(),
            config,
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default()
        ),
        Err(BlockError::ChecksumMismatch)
    ));
    assert_eq!(fs::read(directory.path()).unwrap(), data);
    data[24] ^= 1;
    fs::write(directory.path(), &data).unwrap();
    let owner = UnmarkedBlockFile::open(
        &directory.path(),
        config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    assert_eq!(owner.snapshot().capacity, BLOCK * 3);
}

#[test]
fn reopening_rejects_partial_file_length() {
    let directory = Directory::new();
    drop(directory.open(3, false));
    OpenOptions::new()
        .write(true)
        .open(directory.path())
        .unwrap()
        .set_len(BLOCK * 3 + 1)
        .unwrap();
    assert!(UnmarkedBlockFile::open(
        &directory.path(),
        BlockConfig {
            block_size: BLOCK,
            initial_size: BLOCK * 3,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default()
    )
    .is_err());
}

#[test]
#[ignore = "requires an existing C++ database block file in BLOCK_FILE_FIXTURE"]
fn reopens_cpp_database_without_modifying_header_or_length() {
    let path = PathBuf::from(std::env::var_os("BLOCK_FILE_FIXTURE").expect("BLOCK_FILE_FIXTURE"));
    let file = std::fs::File::open(&path).unwrap();
    let mut before = [0; HEADER_PAGE_SIZE];
    file_io::read_exact_at(&file, &mut before, 0).unwrap();
    let length = file.metadata().unwrap().len();
    let owner = UnmarkedBlockFile::open(
        &path,
        BlockConfig {
            block_size: 2 * 1024 * 1024,
            initial_size: 0,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap();
    assert_eq!(owner.snapshot().capacity, length);
    owner.sync().unwrap();
    drop(owner);
    let mut after = [0; HEADER_PAGE_SIZE];
    file_io::read_exact_at(&file, &mut after, 0).unwrap();
    assert_eq!(before, after);
    assert_eq!(file.metadata().unwrap().len(), length);
}

#[cfg(unix)]
#[test]
fn production_maximum_inventory_reopens_a_sparse_file_without_reserving_capacity() {
    use std::io::Write;
    use std::os::unix::fs::MetadataExt;
    let directory = Directory::new();
    let block_size = 2 * 1024 * 1024;
    let logical = block_size * 51201;
    let mut file = OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(directory.path())
        .unwrap();
    let mut header = [0; HEADER_PAGE_SIZE];
    encode_block_header(block_size, &mut header).unwrap();
    file.write_all(&header).unwrap();
    file.set_len(logical).unwrap();
    let before = file.metadata().unwrap().blocks() * 512;
    assert!(before < 1024 * 1024);
    let owner = UnmarkedBlockFile::open(
        &directory.path(),
        BlockConfig {
            block_size,
            initial_size: 0,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap()
    .finish_marking()
    .unwrap();
    let slots = owner.allocate_many(10240).unwrap();
    assert_eq!(slots.len(), 10240);
    for slot in slots {
        owner.release(slot).unwrap();
    }
    assert_eq!(file.metadata().unwrap().blocks() * 512, before);
    eprintln!("maximum sparse fixture logical_bytes={logical} allocated_bytes={before}");
}

#[test]
fn every_post_create_failure_cleans_owned_entry_and_allows_retry() {
    use crate::block_file::{CreationFault, CreationStage, CREATION_FAULT};
    for stage in [
        CreationStage::Claim,
        CreationStage::FileSync,
        CreationStage::StateAllocation,
    ] {
        let directory = Directory::new();
        CREATION_FAULT.with(|slot| {
            *slot.borrow_mut() = CreationFault {
                stage: Some(stage),
                ..Default::default()
            }
        });
        let error = UnmarkedBlockFile::open(
            &directory.path(),
            BlockConfig {
                block_size: BLOCK,
                initial_size: BLOCK * 3,
                direct_io: false,
                punch_holes: false,
            },
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        )
        .err()
        .unwrap();
        if stage == CreationStage::StateAllocation {
            assert!(matches!(error, BlockError::Memory(_)));
        } else {
            assert!(matches!(error, BlockError::Io(e) if e.raw_os_error() == Some(28)));
        }
        assert!(!directory.path().exists());
        CREATION_FAULT.with(|slot| assert_eq!(slot.borrow().closes, 1));
        drop(directory.open(3, false));
    }
}

#[test]
fn cleanup_keeps_primary_and_fixed_secondary_errors() {
    use crate::block_file::{CreationFault, CreationStage, CREATION_FAULT};
    for removal_failure in [false, true] {
        let directory = Directory::new();
        CREATION_FAULT.with(|slot| {
            *slot.borrow_mut() = CreationFault {
                directory_sync: None,
                stage: Some(CreationStage::StateAllocation),
                close: Some(5),
                remove: removal_failure.then_some(13),
                sync: (!removal_failure).then_some(28),
                closes: 0,
            }
        });
        let error = UnmarkedBlockFile::open(
            &directory.path(),
            BlockConfig {
                block_size: BLOCK,
                initial_size: BLOCK * 3,
                direct_io: false,
                punch_holes: false,
            },
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        )
        .err()
        .unwrap();
        let BlockError::CreationCleanup { primary, cleanup } = error else {
            panic!("cleanup failures lost");
        };
        assert!(matches!(*primary, BlockError::Memory(_)));
        assert_eq!(cleanup.close.unwrap().raw_os_error(), Some(5));
        if removal_failure {
            assert_eq!(cleanup.remove.unwrap().raw_os_error(), Some(13));
            assert!(cleanup.sync.is_none());
        } else {
            assert!(cleanup.remove.is_none());
            assert_eq!(cleanup.sync.unwrap().primary().raw_os_error(), Some(28));
        }
        assert_eq!(directory.path().exists(), removal_failure);
        CREATION_FAULT.with(|slot| assert_eq!(slot.borrow().closes, 1));
    }
}

#[test]
fn substituted_entry_is_never_removed_after_creation_failure() {
    let directory = Directory::new();
    let path = directory.path();
    let moved = path.with_extension("created");
    let error = UnmarkedBlockFile::open_with(
        &path,
        BlockConfig {
            block_size: BLOCK,
            initial_size: BLOCK * 3,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
        |_, _| {
            fs::rename(&path, &moved)?;
            fs::write(&path, b"substituted entry")?;
            Err(io::Error::from_raw_os_error(28))
        },
        |parent, name, identity| parent.remove_created_file(name, identity),
    )
    .err()
    .unwrap();
    let BlockError::CreationCleanup { primary, cleanup } = error else {
        panic!("identity refusal lost");
    };
    assert!(matches!(*primary, BlockError::Io(e) if e.raw_os_error() == Some(28)));
    assert!(cleanup.remove.is_some());
    assert!(cleanup.sync.is_none());
    assert_eq!(fs::read(path).unwrap(), b"substituted entry");
    assert!(moved.exists());
}

#[test]
fn creation_parent_sync_failure_is_typed_and_removes_only_new_file() {
    use crate::block_file::{CreationFault, CREATION_FAULT};
    use file_io::DirectorySyncError;
    for stage in 0..4 {
        let directory = Directory::new();
        let source = io::Error::from_raw_os_error(13);
        let failure = match stage {
            0 => DirectorySyncError::Open(source),
            1 => DirectorySyncError::Flush {
                source,
                close_error: None,
            },
            2 => DirectorySyncError::Flush {
                source,
                close_error: Some(io::Error::from_raw_os_error(5)),
            },
            _ => DirectorySyncError::Close(source),
        };
        CREATION_FAULT.with(|slot| {
            *slot.borrow_mut() = CreationFault {
                directory_sync: Some(failure),
                ..Default::default()
            }
        });
        let error = UnmarkedBlockFile::open(
            &directory.path(),
            BlockConfig {
                block_size: BLOCK,
                initial_size: BLOCK * 3,
                direct_io: false,
                punch_holes: false,
            },
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        )
        .err()
        .unwrap();
        let BlockError::DirectorySync { path, source } = error else {
            panic!("primary sync lost");
        };
        assert_eq!(path, directory.0);
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
        assert!(!directory.path().exists());
        CREATION_FAULT.with(|slot| assert_eq!(slot.borrow().closes, 1));
    }
}

#[cfg(windows)]
#[test]
fn compatibility_summary_survives_later_creation_allocation_failure() {
    use crate::block_file::{CreationFault, CreationStage, CREATION_FAULT};
    let directory = Directory::new();
    CREATION_FAULT.with(|slot| {
        *slot.borrow_mut() = CreationFault {
            stage: Some(CreationStage::StateAllocation),
            ..Default::default()
        }
    });
    let mut summary = file_io::DirectorySyncSummary::default();
    let error = UnmarkedBlockFile::open(
        &directory.path(),
        BlockConfig {
            block_size: BLOCK,
            initial_size: BLOCK * 3,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::WindowsCompatible,
        &mut summary,
    )
    .err()
    .unwrap();
    let primary = match error {
        BlockError::CreationCleanup { primary, cleanup } => {
            eprintln!("Windows strict cleanup errors: {cleanup}");
            *primary
        }
        error => error,
    };
    assert!(matches!(primary, BlockError::Memory(_)));
    eprintln!("Windows failed-open directory observations: {summary:?}");
    assert_eq!(summary.first().is_none(), summary.count() == 0);
    assert!(!directory.path().exists());
}
