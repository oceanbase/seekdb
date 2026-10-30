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

use std::io;

mod directory;
mod durability;
pub use durability::*;
mod path_operations;
mod paths;
pub use path_operations::*;
mod registry;
mod stream;
pub use directory::*;
pub use paths::{
    exists_path, mkdir_path, rename_path, rmdir_path, stat_path, truncate_legacy_path, unlink_path,
};
pub use registry::{named_open_flags_invalid, new_file_registry, stat as file_stat, FileRegistry};
pub use stream::{new_appender, new_reader, AppenderFacade, ReaderFacade};

#[cxx::bridge(namespace = "oceanbase::file_io")]
pub mod ffi {
    enum ErrorDomain {
        None,
        Posix,
        Windows,
        Library,
    }
    struct FsError {
        domain: ErrorDomain,
        native_code: i32,
        error: i32,
        message: String,
    }
    enum SyncStage {
        Complete,
        OpenFailed,
        FlushFailed,
        CloseFailed,
    }
    struct DirectorySyncResult {
        stage: SyncStage,
        primary: FsError,
        close: FsError,
    }
    struct DirectorySyncSummary {
        count: u64,
        first_path: Vec<u8>,
        first_error: FsError,
    }

    enum DirectoryCapabilities {
        BufferedWorker,
        DirectWorker,
        DirectNativeAio,
    }

    struct CapabilityResult {
        capability: DirectoryCapabilities,
        error: i32,
    }

    enum DirectoryStepCode {
        Entry,
        End,
        OpenError,
        ReadError,
    }

    enum Presence {
        Missing,
        Present,
    }
    enum EntryKind {
        File,
        Directory,
        Link,
        Other,
    }
    struct ObservationResult {
        error: FsError,
        presence: Presence,
        kind: EntryKind,
        length: u64,
    }
    struct DirectoryStep {
        code: DirectoryStepCode,
        error: i32,
        detail: FsError,
        name: Vec<u8>,
        inode: u64,
        offset: i64,
        seek_offset: u64,
        record_length: u16,
        name_length: u64,
        raw_type: u32,
    }

    struct IoResult {
        bytes: usize,
        error: i32,
    }

    struct SpaceResult {
        capacity: u64,
        free: u64,
        available: u64,
        error: i32,
    }

    struct PathMetadata {
        directory: bool,
        symlink: bool,
        length: u64,
        error: i32,
    }

    struct ResolvedPath {
        path: Vec<u8>,
        error: i32,
    }

    struct UsableSpaceResult {
        total: u64,
        available: u64,
        error: i32,
    }

    #[derive(Default)]
    struct FileStat {
        rdev: u64,
        dev: u64,
        inode: u64,
        mask: u32,
        mode: u32,
        nlink: u32,
        uid: u64,
        gid: u64,
        size: u64,
        block_cnt: u64,
        block_size: u64,
        atime_s: i64,
        mtime_s: i64,
        ctime_s: i64,
        btime_s: i64,
    }

    extern "Rust" {
        fn sync_directory_path(path: &[u8]) -> DirectorySyncResult;
        type ReaderFacade;
        fn new_reader() -> Box<ReaderFacade>;
        fn open_reader(self: &mut ReaderFacade, path: &[u8], direct: bool, alignment: usize)
            -> i32;
        fn read(self: &mut ReaderFacade, bytes: &mut [u8], offset: u64) -> IoResult;
        fn reader_len(self: &ReaderFacade, length: &mut u64) -> i32;
        type AppenderFacade;
        fn new_appender() -> Box<AppenderFacade>;
        fn open_appender(
            self: &mut AppenderFacade,
            path: &[u8],
            create: u8,
            direct: bool,
            alignment: usize,
        ) -> i32;
        fn append(self: &mut AppenderFacade, bytes: &[u8]) -> IoResult;
        fn sync(self: &mut AppenderFacade) -> i32;
        fn finish(self: &mut AppenderFacade) -> i32;
        fn position(self: &AppenderFacade) -> u64;
        fn is_open(self: &AppenderFacade) -> bool;

        #[derive(ExternType)]
        type FileRegistry;
        fn new_file_registry() -> Box<FileRegistry>;
        fn open_registered(
            self: &FileRegistry,
            path: &[u8],
            flags: i32,
            mode: u32,
            id: &mut u64,
        ) -> i32;
        fn close_registered(self: &FileRegistry, id: u64) -> i32;
        fn clear_registry(self: &FileRegistry);
        fn read_registered(self: &FileRegistry, id: u64, bytes: &mut [u8], offset: u64)
            -> IoResult;
        fn write_registered(self: &FileRegistry, id: u64, bytes: &[u8], offset: u64) -> IoResult;
        fn read_next_registered(self: &FileRegistry, id: u64, bytes: &mut [u8]) -> IoResult;
        fn write_next_registered(self: &FileRegistry, id: u64, bytes: &[u8]) -> IoResult;
        fn sync_registered(self: &FileRegistry, id: u64, data_only: bool) -> i32;
        fn truncate_registered(self: &FileRegistry, id: u64, size: u64) -> i32;
        fn seek_registered(
            self: &FileRegistry,
            id: u64,
            offset: i64,
            whence: i32,
            out: &mut u64,
        ) -> i32;
        fn allocate_registered(
            self: &FileRegistry,
            id: u64,
            mode: i32,
            offset: u64,
            len: u64,
        ) -> i32;
        fn stat_registered(self: &FileRegistry, id: u64, out: &mut FileStat) -> i32;
        #[derive(ExternType)]
        type DirectoryHandle;
        fn new_directory_handle() -> Box<DirectoryHandle>;
        fn open_directory(self: &mut DirectoryHandle, path: &[u8]) -> FsError;
        fn open_child_directory(
            parent: &DirectoryHandle,
            name: &[u8],
            out: &mut DirectoryHandle,
        ) -> FsError;
        fn directory_entries(directory: &DirectoryHandle) -> Box<DirectoryCursor>;
        fn inspect_at(directory: &DirectoryHandle, name: &[u8], follow: bool) -> ObservationResult;
        fn sync_directory_handle(directory: &DirectoryHandle) -> FsError;
        fn remove_file_at(directory: &DirectoryHandle, name: &[u8]) -> FsError;
        fn remove_dir_at(directory: &DirectoryHandle, name: &[u8]) -> FsError;
        type DirectoryCursor;
        fn read_directory(path: &[u8]) -> Box<DirectoryCursor>;
        fn next_entry(self: &mut DirectoryCursor) -> DirectoryStep;
        fn inspect_path(path: &[u8], follow: bool) -> PathMetadata;
        fn create_directory_path(path: &[u8], mode: u32, recursive: bool) -> i32;
        fn remove_file_path(path: &[u8]) -> i32;
        fn remove_directory_path(path: &[u8]) -> i32;
        fn symlink_file_path(target: &[u8], link: &[u8]) -> i32;
        fn resolve_path(path: &[u8], lexical: bool) -> ResolvedPath;
        fn read_usable_space(path: &[u8]) -> UsableSpaceResult;
        fn mkdir_path(path: &[u8], mode: u32) -> i32;
        fn rmdir_path(path: &[u8]) -> i32;
        fn unlink_path(path: &[u8]) -> i32;
        fn rename_path(source: &[u8], destination: &[u8]) -> i32;
        fn truncate_legacy_path(path: &[u8], length: i64) -> i32;
        fn exists_path(path: &[u8], exists: &mut bool) -> i32;
        fn stat_path(path: &[u8], out: &mut FileStat) -> i32;
        /// Legacy byte paths use the process ANSI code page on Windows.
        fn read_space(path: &[u8]) -> SpaceResult;
        fn probe_directory(path: &[u8]) -> CapabilityResult;

    }
}

pub fn legacy_path(bytes: &[u8]) -> io::Result<std::borrow::Cow<'_, std::path::Path>> {
    if bytes.is_empty() || bytes.contains(&0) {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        Ok(std::borrow::Cow::Borrowed(std::path::Path::new(
            std::ffi::OsStr::from_bytes(bytes),
        )))
    }
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStringExt;
        use windows_sys::Win32::Globalization::{
            MultiByteToWideChar, CP_ACP, MB_ERR_INVALID_CHARS,
        };
        let len = i32::try_from(bytes.len()).map_err(|_| io::ErrorKind::InvalidInput)?;
        let size = unsafe {
            MultiByteToWideChar(
                CP_ACP,
                MB_ERR_INVALID_CHARS,
                bytes.as_ptr(),
                len,
                std::ptr::null_mut(),
                0,
            )
        };
        if size == 0 {
            return Err(io::Error::last_os_error());
        }
        let mut wide = vec![0; size as usize];
        if unsafe {
            MultiByteToWideChar(
                CP_ACP,
                MB_ERR_INVALID_CHARS,
                bytes.as_ptr(),
                len,
                wide.as_mut_ptr(),
                size,
            )
        } == 0
        {
            return Err(io::Error::last_os_error());
        }
        Ok(std::borrow::Cow::Owned(
            std::ffi::OsString::from_wide(&wide).into(),
        ))
    }
}

fn probe_directory(bytes: &[u8]) -> ffi::CapabilityResult {
    match legacy_path(bytes).and_then(|path| crate::probe_directory(&path)) {
        Ok(capability) => ffi::CapabilityResult {
            capability: match capability {
                crate::DirectoryCapabilities::BufferedWorker => {
                    ffi::DirectoryCapabilities::BufferedWorker
                }
                crate::DirectoryCapabilities::DirectWorker => {
                    ffi::DirectoryCapabilities::DirectWorker
                }
                crate::DirectoryCapabilities::DirectNativeAio => {
                    ffi::DirectoryCapabilities::DirectNativeAio
                }
            },
            error: 0,
        },
        Err(error) => ffi::CapabilityResult {
            capability: ffi::DirectoryCapabilities::BufferedWorker,
            error: error_number(error),
        },
    }
}

fn read_space(bytes: &[u8]) -> ffi::SpaceResult {
    match legacy_path(bytes).and_then(|path| crate::space(&path)) {
        Ok(value) => ffi::SpaceResult {
            capacity: value.capacity,
            free: value.free,
            available: value.available,
            error: 0,
        },
        Err(error) => ffi::SpaceResult {
            capacity: 0,
            free: 0,
            available: 0,
            error: error_number(error),
        },
    }
}

pub fn error_number(error: io::Error) -> i32 {
    #[cfg(unix)]
    if let Some(code) = error.raw_os_error() {
        return code;
    }
    #[cfg(windows)]
    if error.raw_os_error()
        == Some(windows_sys::Win32::Foundation::ERROR_TOO_MANY_OPEN_FILES as i32)
    {
        return libc::EMFILE;
    }
    match error.kind() {
        io::ErrorKind::PermissionDenied => libc::EACCES,
        io::ErrorKind::NotFound => libc::ENOENT,
        io::ErrorKind::AlreadyExists | io::ErrorKind::DirectoryNotEmpty => libc::EEXIST,
        io::ErrorKind::TimedOut => libc::ETIMEDOUT,
        io::ErrorKind::WouldBlock => libc::EAGAIN,
        io::ErrorKind::StorageFull | io::ErrorKind::QuotaExceeded => libc::ENOSPC,
        io::ErrorKind::InvalidInput => libc::EINVAL,
        io::ErrorKind::Unsupported => libc::ENOSYS,
        io::ErrorKind::OutOfMemory => libc::ENOMEM,
        io::ErrorKind::Interrupted => libc::EINTR,
        io::ErrorKind::BrokenPipe => libc::EPIPE,
        io::ErrorKind::NotConnected => libc::EBADF,
        _ => libc::EIO,
    }
}

fn result(transfer: crate::Transfer) -> ffi::IoResult {
    ffi::IoResult {
        bytes: transfer.bytes,
        error: transfer.result.err().map_or(0, error_number),
    }
}
