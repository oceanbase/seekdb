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

use std::ffi::{OsStr, OsString};
use std::io;
use std::path::Path;
use std::sync::Arc;

#[cfg(unix)]
#[path = "directory/unix.rs"]
mod platform;
#[cfg(windows)]
#[path = "directory/windows.rs"]
mod platform;

/// The identity guarantee required by a directory client.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BindingRequirement {
    OpenedIdentity,
    /// Allows checked path resolution from a held Windows handle. This is not atomic
    /// against namespace replacement between identity checks.
    ResolvedPathAllowed,
}

/// A single literal child name, validated once at the native boundary.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LeafName(OsString);
impl LeafName {
    pub fn new(name: &OsStr) -> io::Result<Self> {
        if name.is_empty()
            || name.as_encoded_bytes().contains(&0)
            || name.as_encoded_bytes().contains(&b'/')
            || Path::new(name).components().count() != 1
            || !matches!(
                Path::new(name).components().next(),
                Some(std::path::Component::Normal(_))
            )
        {
            return Err(io::ErrorKind::InvalidInput.into());
        }
        #[cfg(windows)]
        {
            use std::os::windows::ffi::OsStrExt;
            let units: Vec<_> = name.encode_wide().collect();
            if units
                .iter()
                .any(|u| matches!(*u, 0x2f | 0x5c | 0x3a | 0x2a | 0x3f))
                || matches!(units.last(), Some(0x2e | 0x20))
            {
                return Err(io::ErrorKind::InvalidInput.into());
            }
        }
        let mut owned = OsString::new();
        owned
            .try_reserve(name.len())
            .map_err(|_| io::ErrorKind::OutOfMemory)?;
        owned.push(name);
        Ok(Self(owned))
    }
    pub fn as_os_str(&self) -> &OsStr {
        &self.0
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Follow {
    Yes,
    No,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EntryKind {
    File,
    Directory,
    Link,
    Other,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Observation {
    Missing,
    Present {
        kind: EntryKind,
        len: u64,
        identity: crate::FileIdentity,
    },
}

/// The OS result and subsequent parent verification are independent evidence.
pub struct Mutation<T> {
    pub syscall: io::Result<T>,
    pub postcheck: io::Result<()>,
}
impl<T> Mutation<T> {
    pub fn not_applied(error: io::Error) -> Self {
        Self {
            syscall: Err(error),
            postcheck: Ok(()),
        }
    }
    pub fn into_result(self) -> io::Result<T> {
        let value = self.syscall?;
        self.postcheck?;
        Ok(value)
    }
}

/// An opened directory retained independently of its original pathname.
pub struct Directory {
    owner: Arc<platform::Owner>,
}

impl Directory {
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        Self::open_with(path, BindingRequirement::OpenedIdentity)
    }

    pub fn open_with(path: impl AsRef<Path>, binding: BindingRequirement) -> io::Result<Self> {
        Ok(Self {
            owner: Arc::new(platform::Owner::open(path.as_ref(), binding)?),
        })
    }

    pub fn identity(&self) -> io::Result<crate::FileIdentity> {
        self.owner.identity()
    }
    pub fn open_file(&self, name: &LeafName) -> io::Result<std::fs::File> {
        self.owner.open_file(name)
    }
    pub fn create_file(&self, name: &LeafName, mode: u32) -> Mutation<std::fs::File> {
        postchecked(self.owner.create_file(name, mode))
    }
    pub fn create_directory(&self, name: &LeafName, mode: u32) -> Mutation<()> {
        postchecked(self.owner.create_directory(name, mode))
    }
    pub fn rename_directory_selected(
        &self,
        old: &LeafName,
        new: &LeafName,
        identity: crate::FileIdentity,
    ) -> Mutation<()> {
        self.rename_entry(old, new, identity, EntryKind::Directory)
    }
    pub fn check_directory(
        &self,
        name: &LeafName,
        identity: crate::FileIdentity,
    ) -> io::Result<()> {
        self.check_entry(name, identity, EntryKind::Directory)
    }
    pub fn remove_selected(&self, name: &LeafName, identity: crate::FileIdentity) -> Mutation<()> {
        if let Err(error) = self.check_file(name, identity) {
            return Mutation::not_applied(error);
        }
        postchecked(self.owner.remove_effect(name, false))
    }
    pub fn rename_selected(
        &self,
        old: &LeafName,
        new: &LeafName,
        identity: crate::FileIdentity,
    ) -> Mutation<()> {
        self.rename_entry(old, new, identity, EntryKind::File)
    }
    fn rename_entry(
        &self,
        old: &LeafName,
        new: &LeafName,
        identity: crate::FileIdentity,
        kind: EntryKind,
    ) -> Mutation<()> {
        if let Err(error) = self.check_entry(old, identity, kind) {
            return Mutation::not_applied(error);
        }
        match self.inspect(new, Follow::No) {
            Ok(Observation::Missing) => postchecked(self.owner.rename_effect(old, new)),
            Ok(_) => Mutation::not_applied(io::ErrorKind::AlreadyExists.into()),
            Err(error) => Mutation::not_applied(error),
        }
    }
    pub fn remove_directory(&self, name: &LeafName) -> Mutation<()> {
        postchecked(self.owner.remove_effect(name, true))
    }
    pub fn check_file(&self, name: &LeafName, identity: crate::FileIdentity) -> io::Result<()> {
        self.check_entry(name, identity, EntryKind::File)
    }
    fn check_entry(
        &self,
        name: &LeafName,
        identity: crate::FileIdentity,
        kind: EntryKind,
    ) -> io::Result<()> {
        match self.inspect(name, Follow::No)? {
            Observation::Present {
                kind: found_kind,
                identity: found,
                ..
            } if found == identity && kind == found_kind => Ok(()),
            _ => Err(io::Error::other("selected entry identity changed")),
        }
    }
    pub fn inspect(&self, name: &LeafName, follow: Follow) -> io::Result<Observation> {
        self.owner.inspect(name, follow)
    }
    /// Opens a child directory without following a final link or reparse point.
    pub fn open_child(&self, name: &LeafName) -> io::Result<Self> {
        Ok(Self {
            owner: Arc::new(self.owner.open_child(name)?),
        })
    }
    pub fn remove_file(&self, name: &LeafName) -> io::Result<()> {
        self.owner.remove(name, false)
    }
    pub fn remove_dir(&self, name: &LeafName) -> io::Result<()> {
        self.owner.remove(name, true)
    }

    /// Flushes this held directory without changing its ownership or pathname binding.
    pub fn sync(&self) -> io::Result<()> {
        self.owner.sync()
    }

    /// Removes only an observed matching ordinary entry. The caller must control
    /// the namespace across the identity check and removal.
    pub fn remove_created_file(
        &self,
        name: &std::ffi::OsStr,
        identity: crate::FileIdentity,
    ) -> io::Result<()> {
        let name = LeafName::new(name)?;
        self.owner.remove_created_file(&name, identity)
    }

    pub fn close(self) -> io::Result<()> {
        match Arc::try_unwrap(self.owner) {
            Ok(owner) => owner.close(),
            Err(_) => Ok(()),
        }
    }

    /// Starts a fresh enumeration with an independent position.
    pub fn entries(&self) -> io::Result<Entries> {
        Ok(Entries {
            state: Some((platform::Stream::open(&self.owner)?, self.owner.clone())),
        })
    }
}

/// A copied name. Enumeration observations do not establish current membership or metadata.
#[derive(Debug)]
pub struct Entry {
    name: OsString,
    #[cfg_attr(
        not(feature = "cxx"),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub(crate) record: Record,
}

impl Entry {
    pub fn file_name(&self) -> OsString {
        self.name.clone()
    }
}

#[derive(Debug, Default)]
pub(crate) struct Record {
    #[cfg_attr(
        not(feature = "cxx"),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub inode: u64,
    #[cfg_attr(
        not(feature = "cxx"),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub offset: i64,
    #[cfg_attr(
        not(feature = "cxx"),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub seek_offset: u64,
    #[cfg_attr(
        not(all(unix, feature = "cxx")),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub record_length: u16,
    #[cfg_attr(
        not(all(unix, feature = "cxx")),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub name_length: u64,
    #[cfg_attr(
        not(feature = "cxx"),
        allow(
            dead_code,
            reason = "raw observations are consumed by the optional CXX adapter"
        )
    )]
    pub raw_type: u32,
}

/// A pull iterator. EOF and errors are terminal, and EOF reports a failed stream close.
pub struct Entries {
    state: Option<(platform::Stream, Arc<platform::Owner>)>,
}

impl Entries {
    pub fn next_entry(&mut self) -> io::Result<Option<Entry>> {
        let Some((mut stream, owner)) = self.state.take() else {
            return Ok(None);
        };
        loop {
            match stream.next(&owner) {
                Ok(Some(entry)) if entry.name == "." || entry.name == ".." => continue,
                Ok(Some(entry)) => {
                    self.state = Some((stream, owner));
                    return Ok(Some(entry));
                }
                Ok(None) => return stream.close().map(|()| None),
                Err(error) => return Err(error),
            }
        }
    }
}

impl Iterator for Entries {
    type Item = io::Result<Entry>;
    fn next(&mut self) -> Option<Self::Item> {
        self.next_entry().transpose()
    }
}
impl std::iter::FusedIterator for Entries {}

/// Enumerates a path using the same owner and stream as Directory::entries.
/// Windows explicitly accepts the checked resolved-path binding described above.
pub fn read_dir(path: impl AsRef<Path>) -> io::Result<Entries> {
    Directory::open_with(path, BindingRequirement::ResolvedPathAllowed)?.entries()
}

#[cfg(test)]
mod tests;

pub(crate) fn sync_path(path: &Path) -> Result<(), crate::DirectorySyncError> {
    use crate::DirectorySyncError;
    #[cfg(test)]
    if let Some(code) = SYNC_FAULT.with(|fault| fault.borrow_mut().open.take()) {
        return Err(DirectorySyncError::Open(io::Error::from_raw_os_error(code)));
    }
    let owner = platform::Owner::open(path, BindingRequirement::ResolvedPathAllowed)
        .map_err(DirectorySyncError::Open)?;
    let flush = owner.sync();
    #[cfg(test)]
    let flush = SYNC_FAULT
        .with(|fault| fault.borrow_mut().flush.take())
        .map_or(flush, |code| Err(io::Error::from_raw_os_error(code)));
    let close = owner.close();
    #[cfg(test)]
    let close = SYNC_FAULT.with(|fault| {
        let mut fault = fault.borrow_mut();
        fault.closes += 1;
        fault
            .close
            .take()
            .map_or(close, |code| Err(io::Error::from_raw_os_error(code)))
    });
    match (flush, close) {
        (Ok(()), Ok(())) => Ok(()),
        (Err(source), close) => Err(DirectorySyncError::Flush {
            source,
            close_error: close.err(),
        }),
        (Ok(()), Err(error)) => Err(DirectorySyncError::Close(error)),
    }
}

#[cfg(test)]
#[derive(Default)]
pub(crate) struct SyncFault {
    pub open: Option<i32>,
    pub flush: Option<i32>,
    pub close: Option<i32>,
    pub closes: usize,
}
#[cfg(test)]
thread_local! { pub(crate) static SYNC_FAULT: std::cell::RefCell<SyncFault> = std::cell::RefCell::default(); }

#[cfg(all(test, target_os = "linux"))]
pub(crate) fn fail_next_stream_close(code: i32) {
    platform::CLOSE_ERROR.with(|error| error.set(Some(code)));
}

fn postchecked<T>(effect: Mutation<T>) -> Mutation<T> {
    #[cfg(test)]
    if FAIL_POSTCHECK.with(|fail| fail.replace(false)) {
        return Mutation {
            syscall: effect.syscall,
            postcheck: Err(io::Error::other("injected parent postcheck failure")),
        };
    }
    effect
}
#[cfg(test)]
thread_local! { static FAIL_POSTCHECK: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }
