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

use super::{ffi, fs_error, no_error};
use std::{io, sync::Arc};

#[derive(Default)]
pub struct DirectoryHandle(Option<Arc<crate::Directory>>);
impl DirectoryHandle {
    pub fn get(&self) -> io::Result<&crate::Directory> {
        self.0.as_deref().ok_or_else(|| {
            io::Error::new(io::ErrorKind::NotConnected, "directory handle is closed")
        })
    }
    pub fn directory(&self) -> io::Result<Arc<crate::Directory>> {
        self.0
            .clone()
            .ok_or_else(|| io::ErrorKind::NotConnected.into())
    }
    fn install(&mut self, open: impl FnOnce() -> io::Result<crate::Directory>) -> ffi::FsError {
        let result = if self.0.is_some() {
            Err(io::ErrorKind::AlreadyExists.into())
        } else {
            open().map(|directory| self.0 = Some(Arc::new(directory)))
        };
        status(result)
    }
    pub fn open_directory(&mut self, path: &[u8]) -> ffi::FsError {
        self.install(|| {
            super::legacy_path(path).and_then(|path| {
                crate::Directory::open_with(path, crate::BindingRequirement::ResolvedPathAllowed)
            })
        })
    }
}
pub fn new_directory_handle() -> Box<DirectoryHandle> {
    Box::default()
}
fn leaf(name: &[u8]) -> io::Result<crate::LeafName> {
    crate::LeafName::new(super::legacy_path(name)?.as_os_str())
}
fn status(result: io::Result<()>) -> ffi::FsError {
    result.map_or_else(|error| fs_error(&error), |()| no_error())
}
pub fn open_child_directory(
    parent: &DirectoryHandle,
    name: &[u8],
    out: &mut DirectoryHandle,
) -> ffi::FsError {
    out.install(|| parent.get()?.open_child(&leaf(name)?))
}
pub fn directory_entries(directory: &DirectoryHandle) -> Box<DirectoryCursor> {
    cursor(directory.get().and_then(crate::Directory::entries))
}
pub fn inspect_at(
    directory: &DirectoryHandle,
    name: &[u8],
    follow: bool,
) -> ffi::ObservationResult {
    let mut result = ffi::ObservationResult {
        error: no_error(),
        presence: ffi::Presence::Missing,
        kind: ffi::EntryKind::Other,
        length: 0,
    };
    match directory.get().and_then(|directory| {
        directory.inspect(
            &leaf(name)?,
            if follow {
                crate::Follow::Yes
            } else {
                crate::Follow::No
            },
        )
    }) {
        Ok(crate::Observation::Missing) => {}
        Ok(crate::Observation::Present { kind, len, .. }) => {
            result.presence = ffi::Presence::Present;
            result.kind = match kind {
                crate::EntryKind::File => ffi::EntryKind::File,
                crate::EntryKind::Directory => ffi::EntryKind::Directory,
                crate::EntryKind::Link => ffi::EntryKind::Link,
                crate::EntryKind::Other => ffi::EntryKind::Other,
            };
            result.length = len;
        }
        Err(error) => result.error = fs_error(&error),
    }
    result
}
pub fn sync_directory_handle(directory: &DirectoryHandle) -> ffi::FsError {
    status(directory.get().and_then(crate::Directory::sync))
}
pub fn remove_file_at(directory: &DirectoryHandle, name: &[u8]) -> ffi::FsError {
    status(
        directory
            .get()
            .and_then(|directory| directory.remove_file(&leaf(name)?)),
    )
}
pub fn remove_dir_at(directory: &DirectoryHandle, name: &[u8]) -> ffi::FsError {
    status(
        directory
            .get()
            .and_then(|directory| directory.remove_dir(&leaf(name)?)),
    )
}
fn cursor(result: io::Result<crate::Entries>) -> Box<DirectoryCursor> {
    Box::new(DirectoryCursor {
        state: match result {
            Ok(stream) => State::Open(stream),
            Err(error) => State::OpenError(error),
        },
    })
}

enum State {
    Open(crate::Entries),
    OpenError(io::Error),
    End,
}

pub struct DirectoryCursor {
    state: State,
}

pub fn read_directory(path: &[u8]) -> Box<DirectoryCursor> {
    cursor(open_entries(path))
}
fn empty_step(code: ffi::DirectoryStepCode, detail: ffi::FsError) -> ffi::DirectoryStep {
    ffi::DirectoryStep {
        code,
        error: detail.error,
        detail,
        name: Vec::new(),
        inode: 0,
        offset: 0,
        seek_offset: 0,
        record_length: 0,
        name_length: 0,
        raw_type: 0,
    }
}

impl DirectoryCursor {
    pub fn next_entry(&mut self) -> ffi::DirectoryStep {
        match std::mem::replace(&mut self.state, State::End) {
            State::OpenError(error) => {
                empty_step(ffi::DirectoryStepCode::OpenError, fs_error(&error))
            }
            State::End => empty_step(ffi::DirectoryStepCode::End, no_error()),
            State::Open(mut entries) => match entries.next_entry() {
                Ok(Some(entry)) => match transport(entry) {
                    Ok(step) => {
                        self.state = State::Open(entries);
                        step
                    }
                    Err(error) => empty_step(ffi::DirectoryStepCode::ReadError, fs_error(&error)),
                },
                Ok(None) => empty_step(ffi::DirectoryStepCode::End, no_error()),
                Err(error) => empty_step(ffi::DirectoryStepCode::ReadError, fs_error(&error)),
            },
        }
    }
}

fn open_entries(path: &[u8]) -> io::Result<crate::Entries> {
    if path.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            "directory path is empty",
        ));
    }
    super::legacy_path(path).and_then(crate::read_dir)
}

fn transport(entry: crate::Entry) -> io::Result<ffi::DirectoryStep> {
    let name = super::path_operations::legacy_bytes(std::path::Path::new(&entry.file_name()))?;
    let record = entry.record;
    #[cfg(windows)]
    let (record_length, name_length) = {
        #[repr(C)]
        struct DirentLayout {
            inode: i32,
            offset: i32,
            record_length: u16,
            name_length: usize,
            kind: i32,
            name: [u8; 261],
        }
        if name.len() >= 261 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "legacy directory name exceeds record capacity",
            ));
        }
        (
            std::mem::size_of::<DirentLayout>() as u16,
            name.len() as u64,
        )
    };
    #[cfg(unix)]
    let (record_length, name_length) = (record.record_length, record.name_length);
    Ok(ffi::DirectoryStep {
        code: ffi::DirectoryStepCode::Entry,
        error: 0,
        detail: no_error(),
        name,
        inode: record.inode,
        offset: record.offset,
        seek_offset: record.seek_offset,
        record_length,
        name_length,
        raw_type: record.raw_type,
    })
}

#[cfg(test)]
mod tests;
