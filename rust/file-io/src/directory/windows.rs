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
use crate::FileIdentity;
use std::ffi::OsString;
use std::fs::{File, OpenOptions};
use std::io;
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
use std::os::windows::io::AsRawHandle;
use std::path::{Path, PathBuf};
use windows_sys::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_NO_MORE_FILES, HANDLE, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Storage::FileSystem::{
    FindClose, FindExInfoStandard, FindExSearchNameMatch, FindFirstFileExW, FindNextFileW,
    GetFinalPathNameByHandleW, FILE_ATTRIBUTE_DEVICE, FILE_ATTRIBUTE_DIRECTORY,
    FILE_ATTRIBUTE_REPARSE_POINT, FILE_FLAG_BACKUP_SEMANTICS, FILE_FLAG_OPEN_REPARSE_POINT,
    FILE_SHARE_DELETE, FILE_SHARE_READ, FILE_SHARE_WRITE, WIN32_FIND_DATAW,
};

pub(super) struct Owner {
    file: File,
    identity: FileIdentity,
}

impl Owner {
    pub(super) fn identity(&self) -> io::Result<crate::FileIdentity> {
        crate::FileIdentity::of(&self.file)
    }
    pub(super) fn sync(&self) -> io::Result<()> {
        if unsafe {
            windows_sys::Win32::Storage::FileSystem::FlushFileBuffers(self.file.as_raw_handle())
        } == 0
        {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
    pub(super) fn close(self) -> io::Result<()> {
        crate::close_file(self.file)
    }
    pub(super) fn remove_created_file(
        &self,
        name: &LeafName,
        identity: FileIdentity,
    ) -> io::Result<()> {
        match self.inspect(name, Follow::No)? {
            Observation::Present {
                kind: EntryKind::File,
                identity: found,
                ..
            } if identity == found => self.remove(name, false),
            _ => Err(io::Error::other(
                "created entry identity changed; refusing removal",
            )),
        }
    }
    pub(super) fn inspect(&self, name: &LeafName, follow: Follow) -> io::Result<Observation> {
        let parent = self.resolved_path()?;
        let path = parent.join(name.as_os_str());
        let result = (|| {
            let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
            let mut data = std::mem::MaybeUninit::<WIN32_FIND_DATAW>::uninit();
            let handle = unsafe {
                FindFirstFileExW(
                    wide.as_ptr(),
                    FindExInfoStandard,
                    data.as_mut_ptr().cast(),
                    FindExSearchNameMatch,
                    std::ptr::null(),
                    0,
                )
            };
            if handle == INVALID_HANDLE_VALUE {
                let error = io::Error::last_os_error();
                return if error.raw_os_error() == Some(ERROR_FILE_NOT_FOUND as i32) {
                    Ok(Observation::Missing)
                } else {
                    Err(error)
                };
            }
            let data = unsafe { data.assume_init() };
            let closed = unsafe { FindClose(handle) };
            if closed == 0 {
                return Err(io::Error::last_os_error());
            }
            let length = data
                .cFileName
                .iter()
                .position(|&unit| unit == 0)
                .ok_or(io::ErrorKind::InvalidData)?;
            if OsString::from_wide(&data.cFileName[..length]) != name.as_os_str() {
                return Err(io::Error::other(
                    "lookup did not return the selected literal name",
                ));
            }
            let file = OpenOptions::new()
                .access_mode(0)
                .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
                .custom_flags(
                    FILE_FLAG_BACKUP_SEMANTICS
                        | if follow == Follow::No {
                            FILE_FLAG_OPEN_REPARSE_POINT
                        } else {
                            0
                        },
                )
                .open(&path)?;
            let metadata = file.metadata()?;
            let attrs = metadata.file_attributes();
            let kind = if attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
                EntryKind::Link
            } else if attrs & FILE_ATTRIBUTE_DIRECTORY != 0 {
                EntryKind::Directory
            } else if attrs & FILE_ATTRIBUTE_DEVICE != 0 {
                EntryKind::Other
            } else {
                EntryKind::File
            };
            Ok(Observation::Present {
                kind,
                len: metadata.len(),
                identity: FileIdentity::of(&file)?,
            })
        })();
        self.verify(&parent)?;
        result
    }
    pub(super) fn open_child(&self, name: &LeafName) -> io::Result<Self> {
        let parent = self.resolved_path()?;
        let result = Self::open(
            &parent.join(name.as_os_str()),
            BindingRequirement::ResolvedPathAllowed,
        );
        self.verify(&parent)?;
        result
    }
    pub(super) fn open_file(&self, name: &LeafName) -> io::Result<File> {
        let parent = self.resolved_path()?;
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT)
            .open(parent.join(name.as_os_str()))?;
        self.verify(&parent)?;
        if file.metadata()?.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            return Err(io::ErrorKind::InvalidInput.into());
        }
        Ok(file)
    }
    pub(super) fn create_file(&self, name: &LeafName, _mode: u32) -> super::Mutation<File> {
        let parent = match self.resolved_path() {
            Ok(parent) => parent,
            Err(error) => return super::Mutation::not_applied(error),
        };
        let syscall = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
            .open(parent.join(name.as_os_str()));
        super::Mutation {
            syscall,
            postcheck: self.verify(&parent),
        }
    }
    pub(super) fn create_directory(&self, name: &LeafName, mode: u32) -> super::Mutation<()> {
        let _ = mode;
        let parent = match self.resolved_path() {
            Ok(parent) => parent,
            Err(error) => return super::Mutation::not_applied(error),
        };
        let syscall = std::fs::create_dir(parent.join(name.as_os_str()));
        super::Mutation {
            syscall,
            postcheck: self.verify(&parent),
        }
    }
    pub(super) fn remove(&self, name: &LeafName, directory: bool) -> io::Result<()> {
        self.remove_effect(name, directory).into_result()
    }
    pub(super) fn remove_effect(&self, name: &LeafName, directory: bool) -> super::Mutation<()> {
        let parent = match self.resolved_path() {
            Ok(parent) => parent,
            Err(error) => return super::Mutation::not_applied(error),
        };
        let path = parent.join(name.as_os_str());
        let syscall = if directory {
            std::fs::remove_dir(path)
        } else {
            std::fs::remove_file(path)
        };
        super::Mutation {
            syscall,
            postcheck: self.verify(&parent),
        }
    }
    pub(super) fn rename_effect(&self, old: &LeafName, new: &LeafName) -> super::Mutation<()> {
        let parent = match self.resolved_path() {
            Ok(parent) => parent,
            Err(error) => return super::Mutation::not_applied(error),
        };
        let syscall = std::fs::rename(parent.join(old.as_os_str()), parent.join(new.as_os_str()));
        super::Mutation {
            syscall,
            postcheck: self.verify(&parent),
        }
    }
    pub(super) fn open(path: &Path, binding: BindingRequirement) -> io::Result<Self> {
        if binding == BindingRequirement::OpenedIdentity {
            return Err(io::ErrorKind::Unsupported.into());
        }
        let open = |write| {
            OpenOptions::new()
                .read(true)
                .write(write)
                .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
                .custom_flags(FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT)
                .open(path)
        };
        let file = open(true).or_else(|_| open(false))?;
        normal_directory(&file.metadata()?)?;
        let identity = FileIdentity::of(&file)?;
        Ok(Self { file, identity })
    }

    fn resolved_path(&self) -> io::Result<PathBuf> {
        let mut buffer = vec![0; 256];
        loop {
            let length = unsafe {
                GetFinalPathNameByHandleW(
                    self.file.as_raw_handle(),
                    buffer.as_mut_ptr(),
                    buffer.len() as u32,
                    0,
                )
            };
            if length == 0 {
                return Err(io::Error::last_os_error());
            }
            if length as usize >= buffer.len() {
                buffer.resize(length as usize + 1, 0);
                continue;
            }
            buffer.truncate(length as usize);
            let path = PathBuf::from(OsString::from_wide(&buffer));
            self.verify(&path)?;
            return Ok(path);
        }
    }

    fn verify(&self, path: &Path) -> io::Result<()> {
        normal_directory(&std::fs::symlink_metadata(path)?)?;
        if FileIdentity::at(path)? != self.identity {
            return Err(io::Error::other(
                "directory identity changed during enumeration",
            ));
        }
        Ok(())
    }
}

fn normal_directory(metadata: &std::fs::Metadata) -> io::Result<()> {
    let attrs = metadata.file_attributes();
    if attrs & FILE_ATTRIBUTE_DIRECTORY == 0 {
        return Err(io::ErrorKind::NotADirectory.into());
    }
    if attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
        return Err(io::Error::other("resolved directory is a reparse point"));
    }
    Ok(())
}

enum Next {
    Cached,
    Unread,
    End,
}

pub(super) struct Stream {
    handle: Option<HANDLE>,
    next: Next,
    data: Box<WIN32_FIND_DATAW>,
    path: PathBuf,
}

impl Stream {
    pub(super) fn open(owner: &Owner) -> io::Result<Self> {
        let path = owner.resolved_path()?;
        let mut pattern: Vec<u16> = path.as_os_str().encode_wide().collect();
        if !matches!(pattern.last(), Some(0x5c | 0x2f)) {
            pattern.push(0x5c);
        }
        pattern.extend([u16::from(b'*'), 0]);
        let mut data = std::mem::MaybeUninit::<WIN32_FIND_DATAW>::uninit();
        let handle = unsafe {
            FindFirstFileExW(
                pattern.as_ptr(),
                FindExInfoStandard,
                data.as_mut_ptr().cast(),
                FindExSearchNameMatch,
                std::ptr::null(),
                0,
            )
        };
        let stream = if handle == INVALID_HANDLE_VALUE {
            let error = io::Error::last_os_error();
            if error.raw_os_error() != Some(ERROR_FILE_NOT_FOUND as i32) {
                return Err(error);
            }
            Self {
                handle: None,
                next: Next::End,
                data: Box::new(unsafe { std::mem::zeroed() }),
                path,
            }
        } else {
            Self {
                handle: Some(handle),
                next: Next::Cached,
                data: Box::new(unsafe { data.assume_init() }),
                path,
            }
        };
        owner.verify(&stream.path)?;
        Ok(stream)
    }

    fn take_data(&mut self) -> io::Result<Option<WIN32_FIND_DATAW>> {
        match std::mem::replace(&mut self.next, Next::Unread) {
            Next::Cached => Ok(Some(*self.data)),
            Next::End => {
                self.next = Next::End;
                Ok(None)
            }
            Next::Unread => {
                if unsafe { FindNextFileW(self.handle.unwrap(), &mut *self.data) } == 0 {
                    let error = io::Error::last_os_error();
                    self.next = Next::End;
                    if error.raw_os_error() == Some(ERROR_NO_MORE_FILES as i32) {
                        Ok(None)
                    } else {
                        Err(error)
                    }
                } else {
                    Ok(Some(*self.data))
                }
            }
        }
    }

    pub(super) fn next(&mut self, owner: &Owner) -> io::Result<Option<Entry>> {
        owner.verify(&self.path)?;
        let Some(data) = self.take_data()? else {
            owner.verify(&self.path)?;
            return Ok(None);
        };
        let length = data
            .cFileName
            .iter()
            .position(|&unit| unit == 0)
            .ok_or(io::ErrorKind::InvalidData)?;
        let name = OsString::from_wide(&data.cFileName[..length]);
        // The legacy record observes the next name's hash. It is not an object identity.
        let offset = if let Some(next) = self.take_data()? {
            let hash = next
                .cFileName
                .iter()
                .take(260)
                .take_while(|&&unit| unit != 0)
                .fold(5381_u32, |hash, &unit| {
                    hash.wrapping_mul(33).wrapping_add(u32::from(unit))
                });
            self.next = Next::Cached;
            i64::from(hash & 0x7fff_ffff)
        } else {
            i64::from(i32::MAX)
        };
        owner.verify(&self.path)?;
        Ok(Some(Entry {
            name,
            record: Record {
                offset,
                raw_type: match data.dwFileAttributes {
                    attr if attr & FILE_ATTRIBUTE_DEVICE != 0 => 0x2000,
                    attr if attr & FILE_ATTRIBUTE_REPARSE_POINT != 0 => 0xa000,
                    attr if attr & FILE_ATTRIBUTE_DIRECTORY != 0 => 0x4000,
                    _ => 0x8000,
                },
                ..Record::default()
            },
        }))
    }

    pub(super) fn close(mut self) -> io::Result<()> {
        if let Some(handle) = self.handle.take() {
            if unsafe { FindClose(handle) } == 0 {
                return Err(io::Error::last_os_error());
            }
        }
        Ok(())
    }
}

impl Drop for Stream {
    fn drop(&mut self) {
        if let Some(handle) = self.handle.take() {
            unsafe {
                FindClose(handle);
            }
        }
    }
}
