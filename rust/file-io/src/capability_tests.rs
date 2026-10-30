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
use crate::Direction;
use std::cell::RefCell;
use std::collections::VecDeque;
use std::sync::atomic::{AtomicU64, Ordering};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum Stage {
    #[cfg(unix)]
    Unlink,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    GetFlags,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    EnableDirect,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    ClearDirect,
    Write,
    Read,
    Sync,
    Close,
}

#[derive(Clone, Copy)]
enum Fault {
    #[cfg(any(target_os = "linux", target_os = "emscripten"))]
    Error(i32),
    Short,
    Corrupt,
}

#[derive(Default)]
struct Injection {
    faults: VecDeque<(Stage, Fault)>,
    calls: Vec<Stage>,
}

thread_local! {
    static INJECTION: RefCell<Injection> = RefCell::default();
}

fn take(stage: Stage) -> Option<Fault> {
    INJECTION.with(|injection| {
        let mut injection = injection.borrow_mut();
        injection.calls.push(stage);
        if injection
            .faults
            .front()
            .is_some_and(|(next, _)| *next == stage)
        {
            injection.faults.pop_front().map(|(_, fault)| fault)
        } else {
            None
        }
    })
}

pub(super) fn check(stage: Stage) -> io::Result<()> {
    match take(stage) {
        #[cfg(any(target_os = "linux", target_os = "emscripten"))]
        Some(Fault::Error(error)) => Err(io::Error::from_raw_os_error(error)),
        None => Ok(()),
        _ => panic!("transfer fault at non-transfer stage"),
    }
}

pub(super) fn transfer(direction: Direction, bytes: &mut [u8]) -> Option<io::Result<usize>> {
    let stage = match direction {
        Direction::Read => Stage::Read,
        Direction::Write => Stage::Write,
    };
    take(stage).map(|fault| match fault {
        #[cfg(any(target_os = "linux", target_os = "emscripten"))]
        Fault::Error(error) => Err(io::Error::from_raw_os_error(error)),
        Fault::Short => Ok(bytes.len() - 1),
        Fault::Corrupt => {
            bytes.fill(0x5a);
            bytes[0] = 0;
            Ok(bytes.len())
        }
    })
}

fn with_faults<T>(faults: &[(Stage, Fault)], run: impl FnOnce() -> T) -> (T, Vec<Stage>) {
    struct Reset;
    impl Drop for Reset {
        fn drop(&mut self) {
            INJECTION.with(|injection| *injection.borrow_mut() = Injection::default());
        }
    }
    INJECTION.with(|injection| {
        let mut injection = injection.borrow_mut();
        *injection = Injection::default();
        injection.faults.extend(faults);
    });
    let _reset = Reset;
    let result = run();
    let calls = INJECTION.with(|injection| {
        let injection = injection.borrow();
        assert!(injection.faults.is_empty());
        injection.calls.clone()
    });
    (result, calls)
}

struct Directory(PathBuf);
impl Directory {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = std::env::temp_dir().join(format!(
            "capability-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir(&path).unwrap();
        Self(path)
    }
    fn assert_empty(&self) {
        assert_eq!(std::fs::read_dir(&self.0).unwrap().count(), 0);
    }
}
impl Drop for Directory {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.0).unwrap();
    }
}

#[test]
fn actual_directory_probe_removes_temporary_name() {
    let directory = Directory::new();
    let capability = probe_directory(&directory.0).unwrap();
    #[cfg(not(any(target_os = "linux", target_os = "android")))]
    assert_eq!(capability, DirectoryCapabilities::BufferedWorker);
    #[cfg(any(target_os = "linux", target_os = "android"))]
    assert!(matches!(
        capability,
        DirectoryCapabilities::BufferedWorker
            | DirectoryCapabilities::DirectWorker
            | DirectoryCapabilities::DirectNativeAio
    ));
    directory.assert_empty();
}

#[test]
fn probe_rejects_empty_missing_and_non_directory_paths() {
    let directory = Directory::new();
    assert_eq!(
        probe_directory(Path::new("")).unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
    assert_eq!(
        probe_directory(&directory.0.join("missing"))
            .unwrap_err()
            .kind(),
        io::ErrorKind::NotFound
    );
    let path = directory.0.join("file");
    std::fs::write(&path, []).unwrap();
    assert!(probe_directory(&path).is_err());
    std::fs::remove_file(path).unwrap();
    directory.assert_empty();
}

#[test]
fn short_and_corrupt_synchronous_transfers_are_io_errors() {
    let directory = Directory::new();
    for fault in [
        (Stage::Write, Fault::Short),
        (Stage::Read, Fault::Short),
        (Stage::Read, Fault::Corrupt),
    ] {
        let (result, calls) = with_faults(&[fault], || probe_directory(&directory.0));
        let error = result.unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::InvalidData);
        #[cfg(feature = "cxx")]
        assert_eq!(crate::bridge::error_number(error), libc::EIO);
        assert!(!calls.contains(&Stage::Sync));
        #[cfg(not(target_os = "emscripten"))]
        assert_eq!(calls.last(), Some(&Stage::Close));
        #[cfg(target_os = "emscripten")]
        assert_eq!(&calls[calls.len() - 2..], &[Stage::Close, Stage::Unlink]);
        directory.assert_empty();
    }
}

#[cfg(target_os = "emscripten")]
#[test]
fn emscripten_probe_closes_before_removing_temporary_name() {
    let directory = Directory::new();
    let (result, calls) = with_faults(&[], || probe_directory(&directory.0));
    assert_eq!(result.unwrap(), DirectoryCapabilities::BufferedWorker);
    assert_eq!(
        calls,
        [
            Stage::Write,
            Stage::Read,
            Stage::Sync,
            Stage::Close,
            Stage::Unlink
        ]
    );
    directory.assert_empty();
}

#[cfg(target_os = "emscripten")]
#[test]
fn emscripten_probe_removes_temporary_name_after_io_and_close_errors() {
    let directory = Directory::new();
    for (stage, error) in [
        (Stage::Write, libc::ENOSPC),
        (Stage::Read, libc::EIO),
        (Stage::Sync, libc::EIO),
        (Stage::Close, libc::EBADF),
    ] {
        let (result, calls) = with_faults(&[(stage, Fault::Error(error))], || {
            probe_directory(&directory.0)
        });
        assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
        assert_eq!(&calls[calls.len() - 2..], &[Stage::Close, Stage::Unlink]);
        directory.assert_empty();
    }
}

#[cfg(target_os = "emscripten")]
#[test]
fn emscripten_cleanup_errors_never_publish_capabilities() {
    let directory = Directory::new();
    for error in [
        libc::EIO,
        libc::EACCES,
        libc::EINVAL,
        libc::EOPNOTSUPP,
        libc::ENOSYS,
    ] {
        let (result, calls) = with_faults(&[(Stage::Unlink, Fault::Error(error))], || {
            probe_directory(&directory.0)
        });
        assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
        assert_eq!(&calls[calls.len() - 2..], &[Stage::Close, Stage::Unlink]);
        let files: Vec<_> = std::fs::read_dir(&directory.0).unwrap().collect();
        assert_eq!(files.len(), 1);
        std::fs::remove_file(files[0].as_ref().unwrap().path()).unwrap();
        directory.assert_empty();
    }
}

#[cfg(target_os = "emscripten")]
#[test]
fn emscripten_cleanup_preserves_io_then_close_error_precedence() {
    let directory = Directory::new();
    for first in [
        None,
        Some(Stage::Write),
        Some(Stage::Read),
        Some(Stage::Sync),
    ] {
        let mut faults = Vec::new();
        if let Some(stage) = first {
            faults.push((stage, Fault::Error(libc::ENOSPC)));
        }
        faults.push((Stage::Close, Fault::Error(libc::EBADF)));
        faults.push((Stage::Unlink, Fault::Error(libc::EACCES)));
        let (result, calls) = with_faults(&faults, || probe_directory(&directory.0));
        let expected = if first.is_some() {
            libc::ENOSPC
        } else {
            libc::EBADF
        };
        assert_eq!(result.unwrap_err().raw_os_error(), Some(expected));
        assert_eq!(&calls[calls.len() - 2..], &[Stage::Close, Stage::Unlink]);
        let files: Vec<_> = std::fs::read_dir(&directory.0).unwrap().collect();
        assert_eq!(files.len(), 1);
        std::fs::remove_file(files[0].as_ref().unwrap().path()).unwrap();
        directory.assert_empty();
    }
}

#[cfg(target_os = "linux")]
#[test]
fn actual_probe_exercises_all_three_success_outcomes() {
    use crate::asynchronous::native::tests::{
        with_faults as native_faults, Call, Fault as NativeFault,
    };
    let directory = Directory::new();
    let (buffered, calls) = with_faults(
        &[(Stage::EnableDirect, Fault::Error(libc::EOPNOTSUPP))],
        || probe_directory(&directory.0),
    );
    assert_eq!(buffered.unwrap(), DirectoryCapabilities::BufferedWorker);
    assert!(
        calls.contains(&Stage::Write)
            && calls.contains(&Stage::Read)
            && calls.contains(&Stage::Sync)
    );
    let direct = native_faults(&[(Call::Setup, NativeFault::Error(libc::ENOSYS))], || {
        probe_directory(&directory.0)
    });
    assert_eq!(direct.unwrap(), DirectoryCapabilities::DirectWorker);
    assert_eq!(
        probe_directory(&directory.0).unwrap(),
        DirectoryCapabilities::DirectNativeAio
    );
    directory.assert_empty();
}

#[cfg(target_os = "linux")]
#[test]
fn direct_unsupported_errors_repeat_a_complete_buffered_round_trip() {
    let directory = Directory::new();
    for stage in [Stage::EnableDirect, Stage::Write, Stage::Read] {
        for error in [libc::EINVAL, libc::EOPNOTSUPP, libc::ENOSYS] {
            let (result, calls) = with_faults(&[(stage, Fault::Error(error))], || {
                probe_directory(&directory.0)
            });
            assert_eq!(result.unwrap(), DirectoryCapabilities::BufferedWorker);
            assert_eq!(
                calls.contains(&Stage::ClearDirect),
                stage != Stage::EnableDirect
            );
            let writes = calls.iter().filter(|&&stage| stage == Stage::Write).count();
            assert_eq!(writes, if stage == Stage::EnableDirect { 1 } else { 2 });
            assert_eq!(
                &calls[calls.len() - 4..],
                &[Stage::Write, Stage::Read, Stage::Sync, Stage::Close]
            );
            directory.assert_empty();
        }
    }
}

#[cfg(target_os = "linux")]
#[test]
fn direct_and_buffered_resource_permission_and_storage_errors_are_fatal() {
    let directory = Directory::new();
    for stage in [Stage::EnableDirect, Stage::Write, Stage::Read] {
        for error in [
            libc::EACCES,
            libc::EPERM,
            libc::ENOSPC,
            libc::EIO,
            libc::EAGAIN,
        ] {
            let (result, calls) = with_faults(&[(stage, Fault::Error(error))], || {
                probe_directory(&directory.0)
            });
            assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
            assert!(!calls.contains(&Stage::Sync));
            assert_eq!(calls.last(), Some(&Stage::Close));
        }
    }
    for error in [libc::EINVAL, libc::EOPNOTSUPP, libc::ENOSYS] {
        let (result, _) = with_faults(
            &[
                (Stage::EnableDirect, Fault::Error(libc::EOPNOTSUPP)),
                (Stage::Write, Fault::Error(error)),
            ],
            || probe_directory(&directory.0),
        );
        assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
    }
    directory.assert_empty();
}

#[cfg(target_os = "linux")]
#[test]
fn flag_read_clear_unlink_sync_and_close_errors_never_fallback() {
    let directory = Directory::new();
    for stage in [Stage::GetFlags, Stage::Unlink, Stage::Sync, Stage::Close] {
        for error in [libc::EINVAL, libc::EOPNOTSUPP, libc::ENOSYS, libc::EIO] {
            let (result, calls) = with_faults(&[(stage, Fault::Error(error))], || {
                probe_directory(&directory.0)
            });
            assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
            assert_eq!(calls.last(), Some(&Stage::Close));
            directory.assert_empty();
        }
    }
    let (result, calls) = with_faults(
        &[
            (Stage::Write, Fault::Error(libc::EINVAL)),
            (Stage::ClearDirect, Fault::Error(libc::EOPNOTSUPP)),
        ],
        || probe_directory(&directory.0),
    );
    assert_eq!(result.unwrap_err().raw_os_error(), Some(libc::EOPNOTSUPP));
    assert!(!calls.contains(&Stage::Sync));
}

#[cfg(target_os = "linux")]
#[test]
fn synchronous_interruption_retries_real_file_transfers() {
    let directory = Directory::new();
    let (result, calls) = with_faults(
        &[
            (Stage::Write, Fault::Error(libc::EINTR)),
            (Stage::Read, Fault::Error(libc::EINTR)),
        ],
        || probe_directory(&directory.0),
    );
    assert_eq!(result.unwrap(), DirectoryCapabilities::DirectNativeAio);
    assert_eq!(
        calls.iter().filter(|&&stage| stage == Stage::Write).count(),
        2
    );
    assert_eq!(
        calls.iter().filter(|&&stage| stage == Stage::Read).count(),
        2
    );
    directory.assert_empty();
}

#[cfg(target_os = "linux")]
#[test]
fn cleanup_preserves_primary_errors_and_defeats_tentative_fallback() {
    use crate::asynchronous::native::tests::{
        with_faults as native_faults, Call, Fault as NativeFault,
    };
    let directory = Directory::new();
    for (first, error) in [
        (Stage::Write, libc::ENOSPC),
        (Stage::Sync, libc::EIO),
        (Stage::Unlink, libc::EACCES),
    ] {
        let (result, calls) = with_faults(
            &[
                (first, Fault::Error(error)),
                (Stage::Close, Fault::Error(libc::EBADF)),
            ],
            || probe_directory(&directory.0),
        );
        assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
        assert_eq!(calls.last(), Some(&Stage::Close));
        directory.assert_empty();
    }
    let result = native_faults(&[(Call::Setup, NativeFault::Error(libc::ENOSYS))], || {
        with_faults(&[(Stage::Sync, Fault::Error(libc::EIO))], || {
            probe_directory(&directory.0)
        })
        .0
    });
    assert_eq!(result.unwrap_err().raw_os_error(), Some(libc::EIO));
}

#[cfg(unix)]
#[test]
fn native_path_keeps_non_utf8_directory_bytes() {
    use std::os::unix::ffi::OsStrExt;
    let directory = Directory::new();
    let path = directory.0.join(std::ffi::OsStr::from_bytes(b"probe-\xff"));
    std::fs::create_dir(&path).unwrap();
    probe_directory(&path).unwrap();
    assert_eq!(std::fs::read_dir(&path).unwrap().count(), 0);
}

#[cfg(windows)]
#[test]
fn native_wide_path_and_delete_on_close_survive_probe_failure() {
    let directory = Directory::new();
    let path = directory.0.join("目录-Δ");
    std::fs::create_dir(&path).unwrap();
    assert_eq!(
        probe_directory(&path).unwrap(),
        DirectoryCapabilities::BufferedWorker
    );
    assert_eq!(std::fs::read_dir(&path).unwrap().count(), 0);
    let (result, _) = with_faults(&[(Stage::Read, Fault::Short)], || probe_directory(&path));
    assert_eq!(result.unwrap_err().kind(), io::ErrorKind::InvalidData);
    assert_eq!(std::fs::read_dir(&path).unwrap().count(), 0);
}
