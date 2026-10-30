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

use super::{error_number, ffi, legacy_path, result};
use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

static NEXT_ID: AtomicU64 = AtomicU64::new(1);

pub struct FileRegistry {
    files: Mutex<HashMap<u64, Arc<File>>>,
    publication: Mutex<()>,
}

pub fn new_file_registry() -> Box<FileRegistry> {
    Box::new(FileRegistry {
        files: Mutex::new(HashMap::new()),
        publication: Mutex::new(()),
    })
}

fn status(value: io::Result<()>) -> i32 {
    value.err().map_or(0, error_number)
}
fn transfer(value: io::Result<usize>) -> ffi::IoResult {
    match value {
        Ok(bytes) => ffi::IoResult { bytes, error: 0 },
        Err(error) => ffi::IoResult {
            bytes: 0,
            error: error_number(error),
        },
    }
}
fn retry<T>(mut operation: impl FnMut() -> io::Result<T>) -> io::Result<T> {
    loop {
        match operation() {
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            value => return value,
        }
    }
}

fn next_id(sequence: &AtomicU64) -> io::Result<u64> {
    sequence
        .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |next| {
            if next <= i64::MAX as u64 {
                Some(next + 1)
            } else {
                None
            }
        })
        .map_err(|_| io::ErrorKind::OutOfMemory.into())
}

impl FileRegistry {
    pub fn file(&self, id: u64) -> io::Result<Arc<File>> {
        self.files
            .lock()
            .unwrap()
            .get(&id)
            .cloned()
            .ok_or_else(|| io::ErrorKind::NotConnected.into())
    }

    pub fn open_registered(&self, path: &[u8], flags: i32, mode: u32, id: &mut u64) -> i32 {
        *id = 0;
        status((|| {
            let _publication = self.publication.lock().unwrap();
            let path = legacy_path(path)?;
            let open = options(flags, mode)?;
            // Reserve a publication slot before opening can create or truncate a file.
            let mut files = self.files.lock().unwrap();
            files
                .try_reserve(1)
                .map_err(|_| io::ErrorKind::OutOfMemory)?;
            let token = next_id(&NEXT_ID)?;
            drop(files);
            let file = Arc::new(open.open(path)?);
            let mut files = self.files.lock().unwrap();
            files.insert(token, file);
            *id = token;
            Ok(())
        })())
    }

    pub fn close_registered(&self, id: u64) -> i32 {
        let removed = self.files.lock().unwrap().remove(&id);
        if removed.is_some() {
            0
        } else {
            libc::EBADF
        }
    }
    pub fn clear_registry(&self) {
        let _publication = self.publication.lock().unwrap();
        let files = std::mem::take(&mut *self.files.lock().unwrap());
        drop(files);
    }
    pub fn read_registered(&self, id: u64, bytes: &mut [u8], offset: u64) -> ffi::IoResult {
        match self.file(id) {
            Ok(file) => result(crate::read_up_to_at(&file, bytes, offset)),
            Err(error) => transfer(Err(error)),
        }
    }
    pub fn write_registered(&self, id: u64, bytes: &[u8], offset: u64) -> ffi::IoResult {
        match self.file(id) {
            Ok(file) => result(crate::write_fully_at(&file, bytes, offset)),
            Err(error) => transfer(Err(error)),
        }
    }
    pub fn read_next_registered(&self, id: u64, bytes: &mut [u8]) -> ffi::IoResult {
        transfer(
            self.file(id)
                .and_then(|file| retry(|| (&*file).read(bytes))),
        )
    }
    pub fn write_next_registered(&self, id: u64, bytes: &[u8]) -> ffi::IoResult {
        transfer(
            self.file(id)
                .and_then(|file| retry(|| (&*file).write(bytes))),
        )
    }
    pub fn sync_registered(&self, id: u64, data_only: bool) -> i32 {
        status(self.file(id).and_then(|file| {
            if data_only {
                file.sync_data()
            } else {
                file.sync_all()
            }
        }))
    }
    pub fn truncate_registered(&self, id: u64, size: u64) -> i32 {
        status(self.file(id).and_then(|file| {
            if size > i64::MAX as u64 {
                return Err(io::ErrorKind::InvalidInput.into());
            }
            file.set_len(size)
        }))
    }
    pub fn seek_registered(&self, id: u64, offset: i64, whence: i32, out: &mut u64) -> i32 {
        status(self.file(id).and_then(|file| {
            let position = match whence {
                0 if offset >= 0 => SeekFrom::Start(offset as u64),
                1 => SeekFrom::Current(offset),
                2 => SeekFrom::End(offset),
                _ => return Err(io::ErrorKind::InvalidInput.into()),
            };
            *out = retry(|| (&*file).seek(position))?;
            Ok(())
        }))
    }
    pub fn allocate_registered(&self, id: u64, mode: i32, offset: u64, len: u64) -> i32 {
        status(self.file(id).and_then(|file| {
            let mode = match mode {
                0 => crate::Allocation::Reserve,
                1 => crate::Allocation::ReserveKeepLength,
                3 => crate::Allocation::PunchHole,
                _ => return Err(io::ErrorKind::InvalidInput.into()),
            };
            let end = offset.checked_add(len).ok_or(io::ErrorKind::InvalidInput)?;
            crate::allocate(&file, offset..end, mode)
        }))
    }
    pub fn stat_registered(&self, id: u64, out: &mut ffi::FileStat) -> i32 {
        status(self.file(id).and_then(|file| {
            *out = stat(&file)?;
            Ok(())
        }))
    }
}

pub fn named_open_flags_invalid(flags: i32) -> bool {
    flags & (libc::O_APPEND | libc::O_CREAT | libc::O_EXCL | libc::O_TRUNC) != 0
}

fn options(flags: i32, mode: u32) -> io::Result<OpenOptions> {
    if flags & libc::O_APPEND != 0 {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    let mut open = OpenOptions::new();
    let access = flags & 3;
    match access {
        libc::O_RDONLY => {
            open.read(true);
        }
        libc::O_WRONLY => {
            open.write(true);
        }
        libc::O_RDWR => {
            open.read(true).write(true);
        }
        _ => return Err(io::ErrorKind::InvalidInput.into()),
    }
    if flags & libc::O_TRUNC != 0 && access == libc::O_RDONLY {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    open.create(flags & libc::O_CREAT != 0)
        .create_new(flags & (libc::O_CREAT | libc::O_EXCL) == libc::O_CREAT | libc::O_EXCL)
        .truncate(flags & libc::O_TRUNC != 0);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        open.mode(mode).custom_flags(
            flags & !(libc::O_ACCMODE | libc::O_CREAT | libc::O_EXCL | libc::O_TRUNC),
        );
    }
    #[cfg(windows)]
    {
        let _ = mode;
        let known =
            3 | libc::O_CREAT | libc::O_EXCL | libc::O_TRUNC | libc::O_BINARY | libc::O_NOINHERIT;
        if flags & !known != 0 {
            return Err(io::ErrorKind::InvalidInput.into());
        }
    }
    Ok(open)
}

#[cfg(unix)]
pub fn stat(file: &File) -> io::Result<ffi::FileStat> {
    use std::os::unix::fs::MetadataExt;
    let m = file.metadata()?;
    Ok(ffi::FileStat {
        rdev: m.rdev(),
        dev: m.dev(),
        inode: m.ino(),
        mask: 0,
        mode: m.mode(),
        nlink: m.nlink() as u32,
        uid: u64::from(m.uid()),
        gid: u64::from(m.gid()),
        size: m.size(),
        block_cnt: m.blocks(),
        block_size: m.blksize(),
        atime_s: m.atime(),
        mtime_s: m.mtime(),
        ctime_s: m.ctime(),
        btime_s: m
            .created()
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map_or(0, |t| t.as_secs() as i64),
    })
}

#[cfg(windows)]
pub fn stat(file: &File) -> io::Result<ffi::FileStat> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Storage::FileSystem::{
        GetFileInformationByHandle, BY_HANDLE_FILE_INFORMATION,
    };
    let m = file.metadata()?;
    let mut info = std::mem::MaybeUninit::<BY_HANDLE_FILE_INFORMATION>::uninit();
    if unsafe { GetFileInformationByHandle(file.as_raw_handle(), info.as_mut_ptr()) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let info = unsafe { info.assume_init() };
    fn seconds(value: io::Result<std::time::SystemTime>) -> i64 {
        value
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map_or(0, |t| t.as_secs() as i64)
    }
    Ok(ffi::FileStat {
        dev: u64::from(info.dwVolumeSerialNumber),
        inode: (u64::from(info.nFileIndexHigh) << 32) | u64::from(info.nFileIndexLow),
        mode: (if m.is_dir() { 0o040000 } else { 0o100000 })
            | if m.permissions().readonly() {
                0o444
            } else {
                0o666
            },
        nlink: info.nNumberOfLinks,
        size: m.len(),
        atime_s: seconds(m.accessed()),
        mtime_s: seconds(m.modified()),
        ctime_s: seconds(m.created()),
        btime_s: seconds(m.created()),
        ..ffi::FileStat::default()
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    static PATH_ID: AtomicU64 = AtomicU64::new(1);
    struct TestFile(std::path::PathBuf);
    impl TestFile {
        fn new() -> Self {
            Self(std::env::temp_dir().join(format!(
                "file-registry-{}-{}",
                std::process::id(),
                PATH_ID.fetch_add(1, Ordering::Relaxed)
            )))
        }
        fn bytes(&self) -> Vec<u8> {
            self.0.to_str().unwrap().as_bytes().to_vec()
        }
        fn open(&self, registry: &FileRegistry) -> u64 {
            let mut id = 0;
            assert_eq!(
                registry.open_registered(
                    &self.bytes(),
                    libc::O_CREAT | libc::O_RDWR,
                    0o600,
                    &mut id
                ),
                0
            );
            assert!(id > 0 && id <= i64::MAX as u64);
            id
        }
    }
    impl Drop for TestFile {
        fn drop(&mut self) {
            let _ = std::fs::remove_file(&self.0);
        }
    }

    #[test]
    fn token_exhaustion_does_not_wrap() {
        let sequence = AtomicU64::new(i64::MAX as u64);
        assert_eq!(next_id(&sequence).unwrap(), i64::MAX as u64);
        assert!(next_id(&sequence).is_err());
        assert!(next_id(&sequence).is_err());
        assert_eq!(sequence.load(Ordering::Relaxed), i64::MAX as u64 + 1);
    }

    #[test]
    fn stale_foreign_tokens_and_acquired_owner() {
        let path = TestFile::new();
        let registry = new_file_registry();
        let other = new_file_registry();
        let id = path.open(&registry);
        let acquired = registry.file(id).unwrap();
        assert_eq!(other.close_registered(id), libc::EBADF);
        assert_eq!(registry.close_registered(id), 0);
        assert_eq!(registry.close_registered(id), libc::EBADF);
        assert_eq!(error_number(registry.file(id).unwrap_err()), libc::EBADF);
        assert_ne!(path.open(&registry), id);
        assert_ne!(path.open(&other), id);
        acquired.set_len(17).unwrap();
        assert_eq!(acquired.metadata().unwrap().len(), 17);
        registry.clear_registry();
        assert!(registry.files.lock().unwrap().is_empty());
        let replacement = new_file_registry();
        assert_ne!(path.open(&replacement), id);
    }

    #[test]
    fn invalid_flags_do_not_truncate() {
        let path = TestFile::new();
        std::fs::write(&path.0, b"retain").unwrap();
        let registry = new_file_registry();
        for flags in [
            libc::O_RDWR | libc::O_APPEND | libc::O_TRUNC,
            libc::O_RDONLY | libc::O_TRUNC,
            3 | libc::O_TRUNC,
        ] {
            let mut id = 99;
            assert_eq!(
                registry.open_registered(&path.bytes(), flags, 0o600, &mut id),
                libc::EINVAL
            );
            assert_eq!(id, 0);
            assert_eq!(std::fs::read(&path.0).unwrap(), b"retain");
        }
        let mut id = 99;
        assert_eq!(
            registry.open_registered(
                &path.bytes(),
                libc::O_RDWR | libc::O_CREAT | libc::O_EXCL,
                0o600,
                &mut id
            ),
            libc::EEXIST
        );
        assert_eq!(id, 0);
    }

    #[test]
    fn positioned_and_sequential_io_and_large_stat() {
        let path = TestFile::new();
        let registry = new_file_registry();
        let id = path.open(&registry);
        assert_eq!(registry.write_registered(id, b"abcdef", 0).bytes, 6);
        assert_eq!(registry.write_registered(id, b"XY", 1).bytes, 2);
        let mut bytes = [0; 6];
        assert_eq!(registry.read_registered(id, &mut bytes, 0).bytes, 6);
        assert_eq!(&bytes, b"aXYdef");
        let mut cursor = 99;
        assert_eq!(registry.seek_registered(id, 2, 0, &mut cursor), 0);
        assert_eq!(cursor, 2);
        assert_eq!(registry.read_next_registered(id, &mut bytes[..2]).bytes, 2);
        assert_eq!(&bytes[..2], b"Yd");
        assert_eq!(registry.write_next_registered(id, b"!!").bytes, 2);
        let mut stat = ffi::FileStat::default();
        assert_eq!(registry.stat_registered(id, &mut stat), 0);
        assert_eq!(stat.size, 6);
        assert_eq!(registry.truncate_registered(id, (1u64 << 32) + 17), 0);
        assert_eq!(registry.stat_registered(id, &mut stat), 0);
        assert_eq!(stat.size, (1u64 << 32) + 17);
        assert_eq!(registry.sync_registered(id, true), 0);
        assert_eq!(registry.stat_registered(0, &mut stat), libc::EBADF);
        assert_eq!(
            registry.read_next_registered(0, &mut bytes).error,
            libc::EBADF
        );
        assert_eq!(registry.truncate_registered(id, u64::MAX), libc::EINVAL);
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn sync_flags_and_allocation() {
        use std::os::fd::AsRawFd;
        let path = TestFile::new();
        let registry = new_file_registry();
        let mut id = 0;
        assert_eq!(
            registry.open_registered(
                &path.bytes(),
                libc::O_CREAT | libc::O_RDWR | libc::O_SYNC,
                0o600,
                &mut id
            ),
            0
        );
        let file = registry.file(id).unwrap();
        let flags = unsafe { libc::fcntl(file.as_raw_fd(), libc::F_GETFL) };
        assert_eq!(flags & libc::O_SYNC, libc::O_SYNC);
        assert_eq!(registry.allocate_registered(id, 0, 0, 8192), 0);
        assert_eq!(file.metadata().unwrap().len(), 8192);
        assert_eq!(registry.allocate_registered(id, 1, 8192, 8192), 0);
        assert_eq!(file.metadata().unwrap().len(), 8192);
        assert_eq!(registry.allocate_registered(id, 2, 0, 4096), libc::EINVAL);
        assert_eq!(
            registry.allocate_registered(id, 0, u64::MAX, 2),
            libc::EINVAL
        );
    }
}
