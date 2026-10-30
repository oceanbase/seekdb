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
use std::fs::{self, File, OpenOptions};
#[cfg(unix)]
use std::io::Read;
use std::io::{Seek, SeekFrom};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

pub(crate) struct Directory(pub(crate) PathBuf);

impl Directory {
    pub(crate) fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = std::env::temp_dir().join(format!(
            "file-io-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }

    fn file(&self) -> File {
        OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(self.0.join("data"))
            .unwrap()
    }
}

impl Drop for Directory {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}

#[test]
fn positioned_io_reports_short_eof() {
    let directory = Directory::new();
    let mut file = directory.file();
    file.write_all(b"abcdef").unwrap();
    write_all_at(&file, b"XY", 3).unwrap();
    let mut bytes = [0; 8];
    let transfer = read_up_to_at(&file, &mut bytes, 2);
    transfer.result.unwrap();
    assert_eq!(transfer.bytes, 4);
    assert_eq!(&bytes[..4], b"cXYf");
    assert_eq!(
        read_exact_at(&file, &mut bytes, 2).unwrap_err().kind(),
        io::ErrorKind::UnexpectedEof
    );
}

#[test]
#[cfg(unix)]
fn positioned_io_preserves_unix_cursor() {
    let directory = Directory::new();
    let mut file = directory.file();
    file.write_all(b"abcdef").unwrap();
    file.seek(SeekFrom::Start(1)).unwrap();
    write_all_at(&file, b"XY", 3).unwrap();
    read_exact_at(&file, &mut [0; 4], 2).unwrap();
    assert_eq!(file.stream_position().unwrap(), 1);
    let mut next = [0; 1];
    file.read_exact(&mut next).unwrap();
    assert_eq!(&next, b"b");
}

#[test]
#[cfg(windows)]
fn positioned_io_preserves_windows_cursor_behavior() {
    let directory = Directory::new();
    let mut file = directory.file();
    file.write_all(b"abcdef").unwrap();
    file.seek(SeekFrom::Start(1)).unwrap();
    write_all_at(&file, b"XY", 3).unwrap();
    assert_eq!(file.stream_position().unwrap(), 5);
    read_exact_at(&file, &mut [0; 4], 2).unwrap();
    assert_eq!(file.stream_position().unwrap(), 6);
}

#[test]
fn positioned_io_supports_offsets_past_two_gibibytes() {
    let directory = Directory::new();
    let file = directory.file();
    let offset = (1u64 << 31) + 17;
    write_all_at(&file, b"large", offset).unwrap();
    let mut bytes = [0; 5];
    read_exact_at(&file, &mut bytes, offset).unwrap();
    assert_eq!(&bytes, b"large");
    assert_eq!(file.metadata().unwrap().len(), offset + 5);
}

#[test]
fn invalid_range_does_not_write_a_prefix() {
    let directory = Directory::new();
    let file = directory.file();
    for offset in [u64::MAX, i64::MAX as u64] {
        let transfer = write_fully_at(&file, b"x", offset);
        assert_eq!(transfer.bytes, 0);
        assert_eq!(
            transfer.result.unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
    }
    assert_eq!(file.metadata().unwrap().len(), 0);
}

#[test]
fn interrupted_and_partial_writes_retain_exact_progress() {
    struct Partial {
        calls: usize,
        bytes: Vec<u8>,
    }
    impl Write for Partial {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            self.calls += 1;
            match self.calls {
                1 => Err(io::ErrorKind::Interrupted.into()),
                2 => {
                    self.bytes.extend_from_slice(&bytes[..3]);
                    Ok(3)
                }
                _ => Err(io::Error::other("injected failure")),
            }
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }
    let mut writer = Partial {
        calls: 0,
        bytes: Vec::new(),
    };
    let transfer = write_all(&mut writer, b"ab\ncd\n");
    assert_eq!(transfer.bytes, 3);
    assert_eq!(writer.bytes, b"ab\n");
    assert_eq!(transfer.result.unwrap_err().to_string(), "injected failure");
}

#[test]
fn zero_write_stops_without_retrying_or_losing_progress() {
    struct Zero {
        calls: usize,
    }
    impl Write for Zero {
        fn write(&mut self, _bytes: &[u8]) -> io::Result<usize> {
            self.calls += 1;
            Ok(if self.calls == 1 { 2 } else { 0 })
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }
    let mut writer = Zero { calls: 0 };
    let transfer = write_all(&mut writer, b"abcd");
    assert_eq!(transfer.bytes, 2);
    assert_eq!(
        transfer.result.unwrap_err().kind(),
        io::ErrorKind::WriteZero
    );
    assert_eq!(writer.calls, 2);
}

#[test]
fn published_file_can_be_reopened_after_directory_sync() {
    let directory = Directory::new();
    let source = directory.0.join("pending");
    let destination = directory.0.join("committed");
    let mut file = File::create(&source).unwrap();
    file.write_all(b"committed bytes").unwrap();
    file.sync_all().unwrap();
    drop(file);
    publish_file(&source, &destination).unwrap();
    sync_directory(&directory.0).unwrap();
    assert!(!source.exists());
    assert_eq!(fs::read(destination).unwrap(), b"committed bytes");
}

#[test]
fn identity_tracks_files_across_renames_and_path_replacement() {
    let directory = Directory::new();
    let file = directory.file();
    let original = FileIdentity::of(&file).unwrap();
    let path = directory.0.join("data");
    assert_eq!(original, FileIdentity::at(&path).unwrap());
    let archive = directory.0.join("archive");
    fs::rename(&path, &archive).unwrap();
    assert_eq!(original, FileIdentity::at(&archive).unwrap());
    let replacement = directory.file();
    assert_ne!(original, FileIdentity::at(&path).unwrap());
    assert_eq!(
        FileIdentity::of(&replacement).unwrap(),
        FileIdentity::at(&path).unwrap()
    );
    assert_eq!(original, FileIdentity::of(&file).unwrap());
}

#[test]
fn space_query_reports_bytes_and_rejects_missing_paths() {
    let directory = Directory::new();
    let value = space(&directory.0).unwrap();
    assert!(value.capacity > 0);
    assert!(value.available <= value.free);
    let file = directory.file();
    assert_eq!(
        space(&directory.0.join("data")).unwrap().capacity,
        value.capacity
    );
    drop(file);
    assert_eq!(
        space(&directory.0.join("missing")).unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
}

#[test]
#[cfg(target_os = "linux")]
fn space_query_follows_a_file_symlink_to_its_filesystem() {
    use std::os::unix::fs::{symlink, MetadataExt};
    let other = std::path::Path::new("/dev/shm");
    let Ok(metadata) = other.metadata() else {
        return;
    };
    let directory = Directory::new();
    if metadata.dev() == directory.0.metadata().unwrap().dev() {
        return;
    }
    let target = other.join(directory.0.file_name().unwrap());
    let file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&target)
        .unwrap();
    let link = directory.0.join("link");
    symlink(&target, &link).unwrap();
    let result = space(&link);
    drop(file);
    fs::remove_file(target).unwrap();
    assert_eq!(result.unwrap().capacity, space(other).unwrap().capacity);
}

#[test]
fn stdio_redirection_uses_the_open_file_after_rename() {
    const CHILD_PATH: &str = "FILE_IO_STDIO_CHILD";
    if let Some(path) = std::env::var_os(CHILD_PATH) {
        let path = PathBuf::from(path);
        let file = OpenOptions::new()
            .create_new(true)
            .append(true)
            .open(&path)
            .unwrap();
        fs::rename(&path, path.with_extension("renamed")).unwrap();
        fs::write(&path, b"replacement").unwrap();
        redirect_stdio(&file).unwrap();
        drop(file);
        io::stdout().write_all(b"stdout bytes\n").unwrap();
        io::stderr().write_all(b"stderr bytes\n").unwrap();
        return;
    }
    let directory = Directory::new();
    let path = directory.0.join("stdout");
    let output = std::process::Command::new(std::env::current_exe().unwrap())
        .args([
            "--exact",
            "tests::stdio_redirection_uses_the_open_file_after_rename",
            "--nocapture",
        ])
        .env(CHILD_PATH, &path)
        .output()
        .unwrap();
    assert!(output.status.success(), "{output:?}");
    assert_eq!(fs::read(&path).unwrap(), b"replacement");
    let redirected = fs::read_to_string(path.with_extension("renamed")).unwrap();
    assert!(redirected.contains("stdout bytes\n"));
    assert!(redirected.contains("stderr bytes\n"));
}

#[test]
fn aligned_buffer_owns_zeroed_mutable_bytes() {
    fn send_sync<T: Send + Sync>() {}
    send_sync::<AlignedBuffer>();
    for len in [0, 1, 4096, 8193] {
        for alignment in [1, 8, 512, 4096] {
            let mut buffer = AlignedBuffer::zeroed(len, alignment).unwrap();
            assert_eq!((buffer.as_slice().as_ptr() as usize) % alignment, 0);
            assert_eq!(buffer.as_slice(), vec![0; len]);
            buffer.as_mut_slice().fill(31);
            assert_eq!(buffer.as_slice(), vec![31; len]);
        }
    }
    for (len, alignment) in [(1, 0), (1, 3), (usize::MAX, 8)] {
        assert_eq!(
            AlignedBuffer::zeroed(len, alignment).unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
    }
}

#[test]
fn extension_preserves_data_and_never_shrinks() {
    let directory = Directory::new();
    let file = directory.file();
    write_all_at(&file, b"retained", 0).unwrap();
    extend_file(&file, 8193).unwrap();
    extend_file(&file, 4).unwrap();
    let bytes = fs::read(directory.0.join("data")).unwrap();
    assert_eq!(bytes.len(), 8193);
    assert_eq!(&bytes[..8], b"retained");
    assert!(bytes[8..].iter().all(|byte| *byte == 0));
    for length in [0, i64::MAX as u64 + 1, u64::MAX] {
        assert_eq!(
            extend_file(&file, length).unwrap_err().kind(),
            io::ErrorKind::InvalidInput
        );
    }
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), bytes);
}

#[test]
fn zero_tail_extension_preserves_prefix_and_repeated_calls() {
    let directory = Directory::new();
    let mut file = directory.file();
    let prefix = vec![0x5a; 193];
    write_all_at(&file, &prefix, 0).unwrap();
    file.seek(SeekFrom::Start(17)).unwrap();
    let target = 2 * 64 * 1024 + 31;
    allocation::extend_with_zeros(&file, target, |bytes, offset| {
        write_at(&file, bytes, offset)
    })
    .unwrap();
    #[cfg(unix)]
    assert_eq!(file.stream_position().unwrap(), 17);
    for length in [target, 1] {
        allocation::extend_with_zeros(&file, length, |_, _| {
            panic!("a completed extension must not write again")
        })
        .unwrap();
    }
    let bytes = fs::read(directory.0.join("data")).unwrap();
    assert_eq!(bytes.len(), target as usize);
    assert_eq!(&bytes[..prefix.len()], prefix);
    assert!(bytes[prefix.len()..].iter().all(|byte| *byte == 0));
}

#[test]
fn zero_tail_failure_retains_progress_and_retry_uses_actual_eof() {
    let directory = Directory::new();
    let file = directory.file();
    write_all_at(&file, b"retained", 0).unwrap();
    let mut writes = 0;
    let error = allocation::extend_with_zeros(&file, 140_000, |bytes, offset| {
        writes += 1;
        match writes {
            1 => Err(io::ErrorKind::Interrupted.into()),
            2 => write_at(&file, &bytes[..7], offset),
            _ => Err(io::ErrorKind::PermissionDenied.into()),
        }
    })
    .unwrap_err();
    assert_eq!(error.kind(), io::ErrorKind::PermissionDenied);
    assert_eq!(file.metadata().unwrap().len(), 15);
    write_all_at(&file, b"partial", 8).unwrap();
    allocation::extend_with_zeros(&file, 140_000, |bytes, offset| {
        assert!(offset >= 15);
        write_at(&file, &bytes[..bytes.len().min(997)], offset)
    })
    .unwrap();
    let bytes = fs::read(directory.0.join("data")).unwrap();
    assert_eq!(bytes.len(), 140_000);
    assert_eq!(&bytes[..15], b"retainedpartial");
    assert!(bytes[15..].iter().all(|byte| *byte == 0));
}

#[test]
fn zero_tail_rejects_zero_writes_and_preserves_permission_errors() {
    let directory = Directory::new();
    let file = directory.file();
    write_all_at(&file, b"retained", 0).unwrap();
    assert_eq!(
        allocation::extend_with_zeros(&file, 4096, |_, _| Ok(0))
            .unwrap_err()
            .kind(),
        io::ErrorKind::WriteZero
    );
    let read_only = File::open(directory.0.join("data")).unwrap();
    let error = allocation::extend_with_zeros(&read_only, 4096, |bytes, offset| {
        write_at(&read_only, bytes, offset)
    })
    .unwrap_err();
    assert_ne!(error.kind(), io::ErrorKind::Unsupported);
    assert!(error.raw_os_error().is_some());
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), b"retained");
}

#[cfg(unix)]
#[test]
fn extension_rejects_directories_before_writing() {
    let directory = Directory::new();
    let file = File::open(&directory.0).unwrap();
    assert_eq!(
        extend_file(&file, 4096).unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
    assert_eq!(
        allocation::extend_with_zeros(&file, 4096, |_, _| {
            panic!("a directory must not be written")
        })
        .unwrap_err()
        .kind(),
        io::ErrorKind::InvalidInput
    );
}

#[cfg(target_os = "emscripten")]
#[test]
fn wasm_extension_does_not_enable_generic_reservation() {
    let directory = Directory::new();
    let file = directory.file();
    for mode in [
        Allocation::Reserve,
        Allocation::ReserveKeepLength,
        Allocation::PunchHole,
    ] {
        assert_eq!(
            allocate(&file, 0..4096, mode).unwrap_err().kind(),
            io::ErrorKind::Unsupported
        );
        assert_eq!(file.metadata().unwrap().len(), 0);
    }
    extend_file(&file, 4096).unwrap();
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), vec![0; 4096]);
}

#[test]
fn allocation_rejects_invalid_ranges() {
    let directory = Directory::new();
    let file = directory.file();
    for (start, end) in [(1, 1), (9, 3), (0, u64::MAX)] {
        assert_eq!(
            allocate(&file, start..end, Allocation::Reserve)
                .unwrap_err()
                .kind(),
            io::ErrorKind::InvalidInput
        );
    }
    assert_eq!(file.metadata().unwrap().len(), 0);
}

#[cfg(any(target_os = "linux", target_os = "android"))]
#[test]
fn allocation_preserves_data_and_never_shrinks() {
    let directory = Directory::new();
    let mut file = directory.file();
    let data = vec![0x5a; 8192];
    file.write_all(&data).unwrap();
    allocate(&file, 512..1024, Allocation::Reserve).unwrap();
    assert_eq!(file.metadata().unwrap().len(), 8192);
    allocate(&file, 8192..16384, Allocation::ReserveKeepLength).unwrap();
    assert_eq!(file.metadata().unwrap().len(), 8192);
    allocate(&file, 8192..16384, Allocation::Reserve).unwrap();
    assert_eq!(file.metadata().unwrap().len(), 16384);
    let mut actual = vec![0; 16384];
    read_exact_at(&file, &mut actual, 0).unwrap();
    assert_eq!(&actual[..8192], data);
    assert!(actual[8192..].iter().all(|b| *b == 0));
    assert_eq!(file.stream_position().unwrap(), 8192);
}

#[cfg(any(target_os = "linux", target_os = "android"))]
#[test]
fn punching_zeroes_only_requested_bytes_without_growing() {
    let directory = Directory::new();
    let file = directory.file();
    write_all_at(&file, &vec![0x5a; 16384], 0).unwrap();
    allocate(&file, 1000..9000, Allocation::PunchHole).unwrap();
    let mut actual = vec![0; 16384];
    read_exact_at(&file, &mut actual, 0).unwrap();
    assert!(actual[..1000].iter().all(|b| *b == 0x5a));
    assert!(actual[1000..9000].iter().all(|b| *b == 0));
    assert!(actual[9000..].iter().all(|b| *b == 0x5a));
    allocate(&file, 12000..20000, Allocation::PunchHole).unwrap();
    assert_eq!(file.metadata().unwrap().len(), 16384);
}

#[cfg(any(target_os = "linux", target_os = "android"))]
#[test]
fn allocation_permission_errors_are_not_unsupported() {
    let directory = Directory::new();
    let file = directory.file();
    write_all_at(&file, b"retained", 0).unwrap();
    let read_only = File::open(directory.0.join("data")).unwrap();
    let error = allocate(&read_only, 0..4096, Allocation::Reserve).unwrap_err();
    assert_ne!(error.kind(), io::ErrorKind::Unsupported);
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), b"retained");
}

#[cfg(any(target_os = "linux", target_os = "android"))]
#[test]
fn direct_io_preserves_flags_and_transfers_aligned_bytes() {
    use std::os::fd::AsRawFd;
    let directory = Directory::new();
    let file = directory.file();
    let flags = unsafe { libc::fcntl(file.as_raw_fd(), libc::F_GETFL) };
    enable_direct_io(&file).unwrap();
    assert_eq!(
        unsafe { libc::fcntl(file.as_raw_fd(), libc::F_GETFL) },
        flags | libc::O_DIRECT
    );
    let mut buffer = AlignedBuffer::zeroed(4096, 4096).unwrap();
    buffer.as_mut_slice().fill(47);
    write_all_at(&file, buffer.as_slice(), 0).unwrap();
    buffer.as_mut_slice().fill(0);
    read_exact_at(&file, buffer.as_mut_slice(), 0).unwrap();
    assert!(buffer.as_slice().iter().all(|b| *b == 47));
}

#[cfg(any(
    target_os = "linux",
    target_os = "android",
    target_vendor = "apple",
    windows
))]
#[test]
fn reserve_tail_keeps_contents_and_retry_keeps_reservation() {
    let directory = Directory::new();
    let file = directory.file();
    allocate(&file, 0..8192, Allocation::Reserve).unwrap();
    write_all_at(&file, b"retained", 0).unwrap();
    allocate(&file, 8192..16384, Allocation::ReserveKeepLength).unwrap();
    assert_eq!(file.metadata().unwrap().len(), 8192);
    #[cfg(unix)]
    let reserved_blocks = {
        use std::os::unix::fs::MetadataExt;
        file.metadata().unwrap().blocks()
    };
    allocate(&file, 8192..16384, Allocation::ReserveKeepLength).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        assert_eq!(file.metadata().unwrap().blocks(), reserved_blocks);
        assert!(reserved_blocks * 512 >= 16384);
    }
    allocate(&file, 8192..16384, Allocation::Reserve).unwrap();
    assert_eq!(file.metadata().unwrap().len(), 16384);
    let mut prefix = [0; 8];
    read_exact_at(&file, &mut prefix, 0).unwrap();
    assert_eq!(&prefix, b"retained");
}

#[cfg(any(target_vendor = "apple", windows))]
#[test]
fn unsupported_punch_does_not_modify_file() {
    let directory = Directory::new();
    let file = directory.file();
    write_all_at(&file, b"retained", 0).unwrap();
    assert_eq!(
        allocate(&file, 0..8, Allocation::PunchHole)
            .unwrap_err()
            .kind(),
        io::ErrorKind::Unsupported
    );
    assert_eq!(fs::read(directory.0.join("data")).unwrap(), b"retained");
}
