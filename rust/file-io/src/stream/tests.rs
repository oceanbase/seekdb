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
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

struct TestFile(PathBuf);
impl TestFile {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        Self(std::env::temp_dir().join(format!(
            "file-stream-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        )))
    }
    fn appender(&self, mode: IoMode) -> Appender {
        Appender::open(
            &self.0,
            CreateMode::CreateOrTruncate,
            mode,
            BufferLayout::new(8192, 4096).unwrap(),
        )
        .unwrap()
    }
}
impl Drop for TestFile {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.0);
    }
}
fn owner(appender: &mut Appender) -> &mut OpenAppender {
    match &mut appender.state {
        State::Open(o) => o,
        _ => panic!("owner must be open"),
    }
}
fn fake_direct(path: &TestFile) -> Appender {
    let mut appender = path.appender(IoMode::Buffered);
    owner(&mut appender).mode = EffectiveMode::Direct;
    appender
}

#[test]
fn layouts_reject_overflow_and_alignment_larger_than_capacity() {
    for (size, align) in [
        (0, 4096),
        (4096, 8192),
        (4096, 0),
        (4096, 3),
        (4097, 4096),
        (usize::MAX, 1),
    ] {
        assert!(BufferLayout::new(size, align).is_err());
    }
    assert!(BufferLayout::new(4096, 4096).is_ok());
}

#[test]
fn large_append_keeps_a_single_bounded_buffer_and_exact_bytes() {
    let path = TestFile::new();
    let mut appender = path.appender(IoMode::Buffered);
    let input: Vec<u8> = (0..1_000_037).map(|n| (n % 251) as u8).collect();
    let buffer = owner(&mut appender).buffer.as_slice().as_ptr();
    appender.append(&input).unwrap();
    assert_eq!(appender.position(), input.len() as u64);
    assert_eq!(owner(&mut appender).buffer.as_slice().as_ptr(), buffer);
    assert_eq!(owner(&mut appender).buffer.as_slice().len(), 8192);
    appender.finish().unwrap();
    appender.finish().unwrap();
    assert!(!appender.is_open());
    assert_eq!(std::fs::read(&path.0).unwrap(), input);
    assert!(matches!(
        appender.append(b"late"),
        Err(AppendError::Rejected(_))
    ));
}

#[test]
fn all_modes_preserve_tail_across_sync_and_reopen() {
    for mode in [
        IoMode::Buffered,
        IoMode::PreferDirect,
        IoMode::RequireDirect,
    ] {
        let path = TestFile::new();
        let layout = BufferLayout::new(8192, 4096).unwrap();
        let result = Appender::open(&path.0, CreateMode::Exclusive, mode, layout);
        let mut appender = match result {
            Ok(a) => a,
            Err(e)
                if mode == IoMode::RequireDirect
                    && (e.kind() == io::ErrorKind::Unsupported
                        || matches!(e.raw_os_error(), Some(libc::EINVAL | libc::EOPNOTSUPP))) =>
            {
                continue
            }
            Err(e) => panic!("open {mode:?}: {e}"),
        };
        eprintln!(
            "stream mode requested={mode:?} effective={:?}",
            appender.effective_mode()
        );
        if mode == IoMode::RequireDirect {
            assert_eq!(appender.effective_mode(), EffectiveMode::Direct);
        }
        let input = vec![73; 23001];
        appender.append(b"abc").unwrap();
        appender.sync().unwrap();
        assert_eq!(std::fs::read(&path.0).unwrap(), b"abc");
        appender.append(&input).unwrap();
        appender.sync().unwrap();
        appender.sync().unwrap();
        appender.finish().unwrap();
        let mut expected = b"abc".to_vec();
        expected.extend(input);
        let mut reopened = Appender::open(&path.0, CreateMode::Existing, mode, layout).unwrap();
        reopened.append(b"last").unwrap();
        reopened.finish().unwrap();
        expected.extend(b"last");
        assert_eq!(std::fs::read(&path.0).unwrap(), expected);
        let mut reader = Reader::open(&path.0, mode, layout).unwrap();
        let mut buffer = vec![0; expected.len() + 20];
        let transfer = reader.read_at(&mut buffer[1..], 1);
        transfer.result.unwrap();
        assert_eq!(transfer.bytes, expected.len() - 1);
        assert_eq!(&buffer[1..expected.len()], &expected[1..]);
        let transfer = reader.read_at(&mut buffer, expected.len() as u64);
        transfer.result.unwrap();
        assert_eq!(transfer.bytes, 0);
    }
}

#[test]
fn truncate_failure_retains_repair_with_and_without_later_append() {
    for later in [false, true] {
        let path = TestFile::new();
        let mut appender = fake_direct(&path);
        appender.append(b"abc").unwrap();
        owner(&mut appender).faults.truncate_failures = 1;
        assert!(appender.sync().is_err());
        assert_eq!(std::fs::metadata(&path.0).unwrap().len(), 4096);
        if later {
            appender.append(b"de").unwrap();
        }
        appender.finish().unwrap();
        assert_eq!(
            std::fs::read(&path.0).unwrap(),
            if later {
                b"abcde".as_slice()
            } else {
                b"abc".as_slice()
            }
        );
    }
}

#[test]
fn sync_failure_keeps_owner_and_finish_can_retry_without_replaying_input() {
    let path = TestFile::new();
    let mut appender = fake_direct(&path);
    appender.append(b"abc").unwrap();
    owner(&mut appender).faults.sync_failures = 1;
    assert!(appender.finish().is_err());
    assert!(appender.is_open());
    let writes = owner(&mut appender).faults.write_calls;
    appender.sync().unwrap();
    assert_eq!(owner(&mut appender).faults.write_calls, writes);
    appender.finish().unwrap();
    assert_eq!(std::fs::read(&path.0).unwrap(), b"abc");
}

#[test]
fn failed_write_reports_admission_and_never_truncates_or_resumes() {
    let path = TestFile::new();
    let mut appender = fake_direct(&path);
    owner(&mut appender).faults.fail_write_at = Some(2);
    let error = appender.append(&vec![57; 24001]).unwrap_err();
    assert!(matches!(
        error,
        AppendError::Failed {
            accepted: 16384,
            ..
        }
    ));
    assert_eq!(appender.position(), 16384);
    assert_eq!(std::fs::metadata(&path.0).unwrap().len(), 8192);
    assert!(matches!(
        appender.append(b"retry"),
        Err(AppendError::Failed { accepted: 0, .. })
    ));
    assert!(appender.sync().is_err());
    assert!(appender.finish().is_err());
    drop(appender);
    assert_eq!(std::fs::read(&path.0).unwrap(), vec![57; 8192]);
}

#[test]
fn flushing_prior_input_failure_accepts_no_current_bytes() {
    let path = TestFile::new();
    let mut appender = path.appender(IoMode::Buffered);
    appender.append(&vec![1; 8192]).unwrap();
    owner(&mut appender).faults.fail_write_at = Some(1);
    assert!(matches!(
        appender.append(b"new"),
        Err(AppendError::Failed { accepted: 0, .. })
    ));
    assert!(appender.finish().is_err());
}

#[test]
fn short_direct_write_never_submits_an_unaligned_remainder() {
    let path = TestFile::new();
    let mut appender = fake_direct(&path);
    owner(&mut appender).faults.write_limit = Some(3);
    appender.append(b"hello").unwrap();
    assert!(appender.sync().is_err());
    if let State::Failed { owner, .. } = &appender.state {
        assert_eq!(owner.faults.write_calls, 1);
        assert_eq!(owner.physical_extent, 3);
    } else {
        panic!("direct short write must fail terminally");
    }
    assert!(appender.finish().is_err());
    drop(appender);
    assert_eq!(std::fs::read(&path.0).unwrap(), b"hel");
}

#[test]
fn aligned_short_direct_and_short_buffered_writes_complete() {
    for (direct, count) in [(true, 4096), (false, 3)] {
        let path = TestFile::new();
        let mut appender = if direct {
            fake_direct(&path)
        } else {
            path.appender(IoMode::Buffered)
        };
        owner(&mut appender).faults.write_limit = Some(count);
        appender.append(&vec![7; 7003]).unwrap();
        appender.finish().unwrap();
        assert_eq!(std::fs::read(&path.0).unwrap(), vec![7; 7003]);
    }
}

#[test]
fn zero_write_and_failed_close_remain_errors() {
    let path = TestFile::new();
    let mut appender = path.appender(IoMode::Buffered);
    owner(&mut appender).faults.write_limit = Some(0);
    appender.append(b"a").unwrap();
    assert_eq!(
        appender.finish().unwrap_err().kind(),
        io::ErrorKind::WriteZero
    );
    assert_eq!(
        appender.finish().unwrap_err().kind(),
        io::ErrorKind::WriteZero
    );
    drop(appender);
    let mut appender = path.appender(IoMode::Buffered);
    owner(&mut appender).faults.close_failure = true;
    appender.append(b"b").unwrap();
    assert!(appender.finish().is_err());
    assert!(!appender.is_open());
    assert!(appender.finish().is_err());
    assert_eq!(std::fs::read(&path.0).unwrap(), b"b");
}

#[test]
fn create_modes_and_read_metadata_use_the_owned_file() {
    let path = TestFile::new();
    let layout = BufferLayout::new(8192, 4096).unwrap();
    assert!(Appender::open(&path.0, CreateMode::Existing, IoMode::Buffered, layout).is_err());
    assert!(Appender::open(
        &path.0,
        CreateMode::TruncateExisting,
        IoMode::Buffered,
        layout
    )
    .is_err());
    let mut a = Appender::open(&path.0, CreateMode::IfMissing, IoMode::Buffered, layout).unwrap();
    a.append(b"first").unwrap();
    a.finish().unwrap();
    assert!(Appender::open(&path.0, CreateMode::Exclusive, IoMode::Buffered, layout).is_err());
    let mut a = Appender::open(&path.0, CreateMode::Existing, IoMode::Buffered, layout).unwrap();
    a.append(b"second").unwrap();
    a.finish().unwrap();
    assert_eq!(std::fs::read(&path.0).unwrap(), b"firstsecond");
    let reader = Reader::open(&path.0, IoMode::Buffered, layout).unwrap();
    assert_eq!(reader.len().unwrap(), 11);
    #[cfg(unix)]
    {
        std::fs::remove_file(&path.0).unwrap();
        std::fs::write(&path.0, b"replacement-name").unwrap();
        assert_eq!(reader.len().unwrap(), 11);
    }
    let mut a = Appender::open(
        &path.0,
        CreateMode::TruncateExisting,
        IoMode::Buffered,
        layout,
    )
    .unwrap();
    a.finish().unwrap();
    assert_eq!(std::fs::metadata(&path.0).unwrap().len(), 0);
}

#[test]
fn invalid_read_initializes_progress_and_short_read_preserves_destination() {
    let path = TestFile::new();
    std::fs::write(&path.0, b"abc").unwrap();
    let mut reader = Reader::open(
        &path.0,
        IoMode::Buffered,
        BufferLayout::new(8192, 4096).unwrap(),
    )
    .unwrap();
    let mut bytes = [99; 7];
    let bad = reader.read_at(&mut bytes, u64::MAX);
    assert_eq!(bad.bytes, 0);
    assert!(bad.result.is_err());
    assert_eq!(bytes, [99; 7]);
    let good = reader.read_at(&mut bytes, 1);
    assert_eq!(good.bytes, 2);
    good.result.unwrap();
    assert_eq!(bytes, [b'b', b'c', 99, 99, 99, 99, 99]);
}

#[test]
fn unicode_native_path_and_sparse_metadata_above_two_gib() {
    let path = TestFile::new();
    let path = TestFile(path.0.with_extension("文件-日本語-🦀"));
    let file = File::create(&path.0).unwrap();
    file.set_len(3_u64 << 30).unwrap();
    drop(file);
    let mut reader = Reader::open(
        &path.0,
        IoMode::Buffered,
        BufferLayout::new(8192, 4096).unwrap(),
    )
    .unwrap();
    assert_eq!(reader.len().unwrap(), 3_u64 << 30);
    let mut bytes = [0; 1];
    assert_eq!(reader.read_at(&mut bytes, 3_u64 << 30).bytes, 0);
}

#[test]
fn direct_read_returns_initialized_prefix_with_later_error() {
    let path = TestFile::new();
    std::fs::write(&path.0, vec![42; 24000]).unwrap();
    let mut reader = Reader::open(
        &path.0,
        IoMode::Buffered,
        BufferLayout::new(8192, 4096).unwrap(),
    )
    .unwrap();
    reader.scratch = Some(AlignedBuffer::zeroed(8192, 4096).unwrap());
    reader.read_failure_at = Some(8191);
    let mut bytes = vec![99; 20000];
    let transfer = reader.read_at(&mut bytes, 1);
    assert!(transfer.result.is_err());
    assert_eq!(transfer.bytes, 8191);
    assert!(bytes[..8191].iter().all(|b| *b == 42));
    assert!(bytes[8191..].iter().all(|b| *b == 99));
}

#[test]
fn append_range_rejection_keeps_live_input_and_owner() {
    let path = TestFile::new();
    let mut appender = path.appender(IoMode::Buffered);
    appender.append(b"kept").unwrap();
    let old = owner(&mut appender).logical_end;
    owner(&mut appender).logical_end = i64::MAX as u64;
    assert!(matches!(
        appender.append(b"invalid"),
        Err(AppendError::Rejected(_))
    ));
    assert!(matches!(appender.state, State::Open(_)));
    owner(&mut appender).logical_end = old;
    appender.append(b"more").unwrap();
    appender.finish().unwrap();
    assert_eq!(std::fs::read(&path.0).unwrap(), b"keptmore");
}

#[cfg(unix)]
#[test]
fn direct_reader_opens_a_read_only_file() {
    use std::os::unix::fs::PermissionsExt;
    let path = TestFile::new();
    std::fs::write(&path.0, b"read only").unwrap();
    std::fs::set_permissions(&path.0, std::fs::Permissions::from_mode(0o400)).unwrap();
    let mut reader = Reader::open(
        &path.0,
        IoMode::PreferDirect,
        BufferLayout::new(8192, 4096).unwrap(),
    )
    .unwrap();
    let mut bytes = [0; 16];
    let result = reader.read_at(&mut bytes, 0);
    result.result.unwrap();
    assert_eq!(&bytes[..result.bytes], b"read only");
}
