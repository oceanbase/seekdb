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

use super::{BindingRequirement, Entry, EntryKind, Follow, LeafName, Observation, Record};
use std::ffi::{CStr, OsString};
use std::fs::{File, OpenOptions};
use std::io;
use std::os::fd::{AsRawFd, FromRawFd, IntoRawFd, OwnedFd};
use std::os::unix::ffi::{OsStrExt, OsStringExt};
#[cfg(not(target_os = "emscripten"))]
use std::os::unix::fs::MetadataExt;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::ptr::NonNull;

pub(super) struct Owner(File);
impl Owner {
    pub(super) fn identity(&self) -> io::Result<crate::FileIdentity> {
        crate::FileIdentity::of(&self.0)
    }
    #[cfg(not(target_os = "emscripten"))]
    fn verify_linked(&self) -> io::Result<()> {
        if self.0.metadata()?.nlink() == 0 {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                "opened directory was removed",
            ));
        }
        Ok(())
    }
    #[cfg(target_os = "emscripten")]
    fn verify_linked(&self) -> io::Result<()> {
        // WasmFS fixes st_nlink at 1, but removes "." from unlinked directories.
        let mut stat = std::mem::MaybeUninit::<libc::stat>::uninit();
        if unsafe {
            libc::fstatat(
                self.0.as_raw_fd(),
                c".".as_ptr(),
                stat.as_mut_ptr(),
                libc::AT_SYMLINK_NOFOLLOW,
            )
        } != 0
        {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
    pub(super) fn sync(&self) -> io::Result<()> {
        if unsafe { libc::fsync(self.0.as_raw_fd()) } != 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
    pub(super) fn close(self) -> io::Result<()> {
        crate::close_file(self.0)
    }
    pub(super) fn remove_created_file(
        &self,
        name: &LeafName,
        identity: crate::FileIdentity,
    ) -> io::Result<()> {
        match self.inspect(name, Follow::No)? {
            Observation::Present {
                kind: EntryKind::File,
                identity: found,
                ..
            } if found == identity => self.remove(name, false),
            _ => Err(io::Error::other(
                "created entry identity changed; refusing removal",
            )),
        }
    }
    pub(super) fn inspect(&self, name: &LeafName, follow: Follow) -> io::Result<Observation> {
        let name = std::ffi::CString::new(name.as_os_str().as_bytes()).unwrap();
        let mut stat = std::mem::MaybeUninit::<libc::stat>::uninit();
        let flags = if follow == Follow::No {
            libc::AT_SYMLINK_NOFOLLOW
        } else {
            0
        };
        if unsafe { libc::fstatat(self.0.as_raw_fd(), name.as_ptr(), stat.as_mut_ptr(), flags) }
            != 0
        {
            let error = io::Error::last_os_error();
            return if error.raw_os_error() == Some(libc::ENOENT) {
                self.verify_linked()?;
                Ok(Observation::Missing)
            } else {
                Err(error)
            };
        }
        let stat = unsafe { stat.assume_init() };
        let kind = match stat.st_mode & libc::S_IFMT {
            libc::S_IFREG => EntryKind::File,
            libc::S_IFDIR => EntryKind::Directory,
            libc::S_IFLNK => EntryKind::Link,
            _ => EntryKind::Other,
        };
        #[allow(
            clippy::unnecessary_cast,
            reason = "stat fields vary with the target ABI"
        )]
        let identity = crate::FileIdentity::from_parts(stat.st_dev as u64, stat.st_ino as u64);
        let len = u64::try_from(stat.st_size).map_err(|_| io::ErrorKind::InvalidData)?;
        Ok(Observation::Present {
            kind,
            len,
            identity,
        })
    }
    pub(super) fn open_child(&self, name: &LeafName) -> io::Result<Self> {
        let name = std::ffi::CString::new(name.as_os_str().as_bytes()).unwrap();
        let fd = unsafe {
            libc::openat(
                self.0.as_raw_fd(),
                name.as_ptr(),
                libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
            )
        };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(Self(unsafe { File::from_raw_fd(fd) }))
    }
    pub(super) fn open_file(&self, name: &LeafName) -> io::Result<File> {
        self.open_file_flags(name, libc::O_RDWR, 0)
    }
    fn open_file_flags(&self, name: &LeafName, flags: i32, mode: u32) -> io::Result<File> {
        let name = std::ffi::CString::new(name.as_os_str().as_bytes()).unwrap();
        let fd = unsafe {
            libc::openat(
                self.0.as_raw_fd(),
                name.as_ptr(),
                flags | libc::O_CLOEXEC | libc::O_NOFOLLOW,
                mode as libc::mode_t,
            )
        };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        let file = unsafe { File::from_raw_fd(fd) };
        Ok(file)
    }
    pub(super) fn create_file(&self, name: &LeafName, mode: u32) -> super::Mutation<File> {
        let syscall = self.open_file_flags(name, libc::O_RDWR | libc::O_CREAT | libc::O_EXCL, mode);
        super::Mutation {
            syscall,
            postcheck: self.verify_linked(),
        }
    }
    pub(super) fn create_directory(&self, name: &LeafName, mode: u32) -> super::Mutation<()> {
        let name = std::ffi::CString::new(name.as_os_str().as_bytes()).unwrap();
        let result =
            unsafe { libc::mkdirat(self.0.as_raw_fd(), name.as_ptr(), mode as libc::mode_t) };
        let syscall = if result == 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        };
        super::Mutation {
            syscall,
            postcheck: self.verify_linked(),
        }
    }
    pub(super) fn remove(&self, name: &LeafName, directory: bool) -> io::Result<()> {
        self.remove_effect(name, directory).into_result()
    }
    pub(super) fn remove_effect(&self, name: &LeafName, directory: bool) -> super::Mutation<()> {
        let name = std::ffi::CString::new(name.as_os_str().as_bytes()).unwrap();
        let result = unsafe {
            libc::unlinkat(
                self.0.as_raw_fd(),
                name.as_ptr(),
                if directory { libc::AT_REMOVEDIR } else { 0 },
            )
        };
        let syscall = if result == 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        };
        super::Mutation {
            syscall,
            postcheck: self.verify_linked(),
        }
    }
    pub(super) fn rename_effect(&self, old: &LeafName, new: &LeafName) -> super::Mutation<()> {
        let old = std::ffi::CString::new(old.as_os_str().as_bytes()).unwrap();
        let new = std::ffi::CString::new(new.as_os_str().as_bytes()).unwrap();
        let result = unsafe {
            libc::renameat(
                self.0.as_raw_fd(),
                old.as_ptr(),
                self.0.as_raw_fd(),
                new.as_ptr(),
            )
        };
        let syscall = if result == 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        };
        super::Mutation {
            syscall,
            postcheck: self.verify_linked(),
        }
    }
    pub(super) fn open(path: &Path, _binding: BindingRequirement) -> io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_DIRECTORY | libc::O_CLOEXEC)
            .open(path)?;
        Ok(Self(file))
    }
}

pub(super) struct Stream(NonNull<libc::DIR>);

impl Stream {
    pub(super) fn open(owner: &Owner) -> io::Result<Self> {
        // dup would share the enumeration position with other cursors.
        let fd = unsafe {
            libc::openat(
                owner.0.as_raw_fd(),
                c".".as_ptr(),
                libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC,
            )
        };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        let fd = unsafe { OwnedFd::from_raw_fd(fd) };
        let pointer = unsafe { libc::fdopendir(fd.as_raw_fd()) };
        let pointer = NonNull::new(pointer).ok_or_else(io::Error::last_os_error)?;
        let _ = fd.into_raw_fd();
        Ok(Self(pointer))
    }

    pub(super) fn close(self) -> io::Result<()> {
        let pointer = self.0;
        std::mem::forget(self);
        close_stream(pointer)
    }

    #[cfg(all(test, target_os = "linux"))]
    pub(super) fn descriptor(&self) -> i32 {
        unsafe { libc::dirfd(self.0.as_ptr()) }
    }

    pub(super) fn next(&mut self, owner: &Owner) -> io::Result<Option<Entry>> {
        // POSIX distinguishes EOF from read failure only through errno.
        unsafe {
            *errno_address() = 0;
        }
        let entry = unsafe { libc::readdir(self.0.as_ptr()) };
        if entry.is_null() {
            let error = io::Error::last_os_error();
            return if error.raw_os_error() == Some(0) {
                owner.verify_linked()?;
                Ok(None)
            } else {
                Err(error)
            };
        }
        // readdir guarantees a terminated name valid until the next call on this DIR.
        Ok(Some(unsafe { copy_entry(entry) }))
    }
}

/// The record header and its terminated name must be readable until this call returns.
unsafe fn copy_entry(entry: *const libc::dirent) -> Entry {
    let mut result = Entry {
        name: OsString::new(),
        record: Record::default(),
    };
    // A variable-length readdir record can be shorter than libc::dirent.
    // Read its fields without forming a reference to the entire struct.
    unsafe {
        result.name = OsString::from_vec(
            CStr::from_ptr(std::ptr::addr_of!((*entry).d_name).cast())
                .to_bytes()
                .to_vec(),
        );
        #[allow(
            clippy::unnecessary_cast,
            reason = "ino_t and off_t vary with the target ABI"
        )]
        let inode = (*entry).d_ino as u64;
        result.record.inode = inode;
        result.record.record_length = (*entry).d_reclen;
        result.record.raw_type = u32::from((*entry).d_type);
        #[cfg(target_vendor = "apple")]
        {
            result.record.seek_offset = (*entry).d_seekoff;
            result.record.name_length = u64::from((*entry).d_namlen);
        }
        #[cfg(not(target_vendor = "apple"))]
        {
            #[allow(
                clippy::unnecessary_cast,
                reason = "ino_t and off_t vary with the target ABI"
            )]
            let offset = (*entry).d_off as i64;
            result.record.offset = offset;
            result.record.name_length = result.name.len() as u64;
        }
    }
    result
}

fn errno_address() -> *mut libc::c_int {
    #[cfg(target_vendor = "apple")]
    unsafe {
        libc::__error()
    }
    #[cfg(target_os = "android")]
    unsafe {
        libc::__errno()
    }
    #[cfg(not(any(target_vendor = "apple", target_os = "android")))]
    unsafe {
        libc::__errno_location()
    }
}

fn close_stream(pointer: NonNull<libc::DIR>) -> io::Result<()> {
    let result = unsafe { libc::closedir(pointer.as_ptr()) };
    let error = (result != 0).then(io::Error::last_os_error);
    #[cfg(test)]
    if let Some(error) = CLOSE_ERROR.with(|slot| slot.take()) {
        return Err(io::Error::from_raw_os_error(error));
    }
    error.map_or(Ok(()), Err)
}

impl Drop for Stream {
    fn drop(&mut self) {
        let _ = close_stream(self.0);
    }
}

#[cfg(test)]
thread_local! { pub(super) static CLOSE_ERROR: std::cell::Cell<Option<i32>> = const { std::cell::Cell::new(None) }; }

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_unknown_record_preserves_its_fields() {
        const WORDS: usize = (std::mem::offset_of!(libc::dirent, d_name) + 2).div_ceil(8);
        let mut storage = [0_u64; WORDS];
        let entry = storage.as_mut_ptr().cast::<libc::dirent>();
        // A short POSIX record contains the header and name, not the full d_name array.
        unsafe {
            (*entry).d_ino = 123;
            (*entry).d_reclen = std::mem::size_of_val(&storage) as u16;
            // Some filesystems report DT_UNKNOWN even for ordinary files.
            (*entry).d_type = libc::DT_UNKNOWN;
            std::ptr::addr_of_mut!((*entry).d_name)
                .cast::<u8>()
                .write(b'x');
            #[cfg(target_vendor = "apple")]
            {
                (*entry).d_namlen = 1;
                (*entry).d_seekoff = 456;
            }
            #[cfg(not(target_vendor = "apple"))]
            {
                (*entry).d_off = -7;
            }
        }
        let value = unsafe { copy_entry(entry) };
        assert_eq!(value.name, "x");
        assert_eq!(value.record.name_length, 1);
        assert_eq!(value.record.raw_type, u32::from(libc::DT_UNKNOWN));
        assert_eq!(value.record.inode, 123);
        assert_eq!(
            value.record.record_length as usize,
            std::mem::size_of_val(&storage)
        );
        #[cfg(target_vendor = "apple")]
        assert_eq!(value.record.seek_offset, 456);
        #[cfg(not(target_vendor = "apple"))]
        assert_eq!(value.record.offset, -7);
    }
}
