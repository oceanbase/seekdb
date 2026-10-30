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

use super::{ffi, RequestSlot};
use crate::{NamedOpenedFile, NamedOwner, OwnerPhase, RequestLifetime, Target};
use file_io::bridge::{error_number, legacy_path, DirectoryHandle, FileRegistry};
use std::{collections::HashMap, io, sync::Arc};

pub struct NamedOwnerHandle(Option<NamedOwner>);
pub struct NamedBindings {
    owner: Option<NamedOwner>,
    files: HashMap<u64, Arc<NamedOpenedFile>>,
}
pub fn new_named_owner() -> Box<NamedOwnerHandle> {
    Box::new(NamedOwnerHandle(None))
}
pub fn new_named_bindings() -> Box<NamedBindings> {
    Box::new(NamedBindings {
        owner: None,
        files: HashMap::new(),
    })
}
fn code(result: io::Result<()>) -> i32 {
    result.err().map_or(0, error_number)
}
fn leaf(bytes: &[u8]) -> io::Result<file_io::LeafName> {
    file_io::LeafName::new(legacy_path(bytes)?.as_os_str())
}
fn transfer(value: file_io::Transfer) -> file_io::bridge::ffi::IoResult {
    file_io::bridge::ffi::IoResult {
        bytes: value.bytes,
        error: value.result.err().map_or(0, error_number),
    }
}
impl NamedOwnerHandle {
    fn get(&self) -> io::Result<&NamedOwner> {
        self.0
            .as_ref()
            .ok_or_else(|| io::ErrorKind::NotConnected.into())
    }
    pub fn begin_named_inventory(&mut self, capacity: u64) -> i32 {
        if !self.named_owner_replaceable() {
            return error_number(io::ErrorKind::WouldBlock.into());
        }
        self.0 = Some(NamedOwner::new(capacity));
        0
    }
    pub fn named_owner_replaceable(&self) -> bool {
        self.0.as_ref().is_none_or(NamedOwner::can_replace)
    }
    pub fn close_named_owner(&self) {
        if let Some(owner) = &self.0 {
            owner.close();
        }
    }
    pub fn select_named_candidate(
        &self,
        parent: &DirectoryHandle,
        name: &[u8],
        extent: u64,
        id: &mut u64,
        length: &mut u64,
    ) -> i32 {
        *id = 0;
        *length = 0;
        code((|| {
            let (selected, size) =
                self.get()?
                    .candidate(parent.directory()?, leaf(name)?, extent)?;
            *id = selected;
            *length = size;
            Ok(())
        })())
    }
    pub fn repair_named_candidate(&self, id: u64) -> i32 {
        code(self.get().and_then(|o| o.repair(id)))
    }
    pub fn admit_named_candidate(&self, id: u64) -> i32 {
        code(self.get().and_then(|o| o.admit(id)))
    }
    pub fn finish_named_inventory(&self) -> i32 {
        code(self.get().and_then(NamedOwner::finish_inventory))
    }
    pub fn create_and_publish_named_file(
        &self,
        parent: &DirectoryHandle,
        name: &[u8],
        destination: &[u8],
        extent: u64,
        accept_moved_identity: bool,
    ) -> i32 {
        code((|| {
            self.get()?.create_and_publish(
                parent.directory()?,
                leaf(name)?,
                leaf(destination)?,
                extent,
                accept_moved_identity,
            )
        })())
    }
    pub fn remove_named_file(&self, parent: &DirectoryHandle, name: &[u8], required: bool) -> i32 {
        code((|| {
            let owner = self.get()?;
            let parent = parent.directory()?;
            let name = leaf(name)?;
            if required {
                owner.remove_required(parent, name)
            } else {
                owner.remove(parent, name)
            }
        })())
    }
    pub fn create_named_directory(&self, parent: &DirectoryHandle, name: &[u8], mode: u32) -> i32 {
        code((|| {
            self.get()?
                .create_directory(parent.directory()?, leaf(name)?, mode)
        })())
    }
    pub fn rename_named_directory(
        &self,
        parent: &DirectoryHandle,
        old: &[u8],
        new: &[u8],
        accept_moved_identity: bool,
    ) -> i32 {
        code((|| {
            self.get()?.rename_directory(
                parent.directory()?,
                leaf(old)?,
                leaf(new)?,
                accept_moved_identity,
            )
        })())
    }
    pub fn rename_named_file(
        &self,
        parent: &DirectoryHandle,
        old: &[u8],
        new: &[u8],
        accept_moved_identity: bool,
    ) -> i32 {
        code((|| {
            self.get()?.rename(
                parent.directory()?,
                leaf(old)?,
                leaf(new)?,
                accept_moved_identity,
            )
        })())
    }
    pub fn remove_named_directory(&self, parent: &DirectoryHandle, name: &[u8]) -> i32 {
        code((|| {
            self.get()?
                .remove_directory(parent.directory()?, leaf(name)?)
        })())
    }
    pub fn set_named_capacity(&self, capacity: u64) -> i32 {
        code(self.get().and_then(|o| o.set_capacity(capacity)))
    }
    pub fn named_stats(&self) -> ffi::NamedStats {
        match &self.0 {
            None => ffi::NamedStats {
                phase: 3,
                ..Default::default()
            },
            Some(owner) => {
                let s = owner.snapshot();
                ffi::NamedStats {
                    phase: match s.phase {
                        OwnerPhase::Recovering => 0,
                        OwnerPhase::Ready => 1,
                        OwnerPhase::Failed => 2,
                        OwnerPhase::Closing => 3,
                    },
                    committed: s.committed,
                    charged: s.charged,
                    capacity: s.capacity,
                    leases: s.leases,
                    pending: s.pending,
                    active: s.active,
                    error: s.error,
                }
            }
        }
    }
}
impl NamedBindings {
    pub fn named_enabled(&self) -> bool {
        self.owner.is_some()
    }
    pub fn attach_named_owner(&mut self, owner: &NamedOwnerHandle) -> i32 {
        code((|| {
            if self.owner.is_some() || !self.files.is_empty() {
                return Err(io::ErrorKind::AlreadyExists.into());
            }
            let owner = owner.get()?;
            if owner.snapshot().phase != OwnerPhase::Ready {
                return Err(io::ErrorKind::NotConnected.into());
            }
            self.owner = Some(owner.clone());
            Ok(())
        })())
    }
    pub fn clear_named_bindings(&mut self) {
        self.files.clear();
        self.owner = None;
    }
    pub fn close_named_binding(&mut self, token: u64) {
        self.files.remove(&token);
    }
    pub fn open_named(
        &mut self,
        registry: &FileRegistry,
        path: &[u8],
        flags: i32,
        mode: u32,
        token: &mut u64,
    ) -> i32 {
        *token = 0;
        code((|| {
            // O_APPEND/O_CREAT/O_EXCL/O_TRUNC are rejected before OS open.
            if file_io::bridge::named_open_flags_invalid(flags) {
                return Err(io::ErrorKind::Unsupported.into());
            }
            let owner = self.owner.as_ref().ok_or(io::ErrorKind::NotConnected)?;
            self.files
                .try_reserve(1)
                .map_err(|_| io::ErrorKind::OutOfMemory)?;
            let mut id = 0;
            let error = registry.open_registered(path, flags, mode, &mut id);
            if error != 0 {
                return Err(io::Error::from_raw_os_error(error));
            }
            let binding = registry.file(id).and_then(|file| owner.bind(file));
            match binding {
                Ok(binding) => {
                    self.files.insert(id, binding);
                    *token = id;
                    Ok(())
                }
                Err(error) => {
                    registry.close_registered(id);
                    Err(error)
                }
            }
        })())
    }
    pub(super) fn get(&self, token: u64) -> io::Result<Arc<NamedOpenedFile>> {
        let file = self
            .files
            .get(&token)
            .cloned()
            .ok_or(io::ErrorKind::NotConnected)?;
        file.check_admission()?;
        Ok(file)
    }
    pub fn read_named(
        &self,
        token: u64,
        bytes: &mut [u8],
        offset: u64,
    ) -> file_io::bridge::ffi::IoResult {
        match self.get(token) {
            Ok(file) => transfer(file.read(bytes, offset)),
            Err(error) => transfer(file_io::Transfer {
                bytes: 0,
                result: Err(error),
            }),
        }
    }
    pub fn write_named(
        &self,
        token: u64,
        bytes: &[u8],
        offset: u64,
    ) -> file_io::bridge::ffi::IoResult {
        match self.get(token) {
            Ok(file) => transfer(file.write(bytes, offset)),
            Err(error) => transfer(file_io::Transfer {
                bytes: 0,
                result: Err(error),
            }),
        }
    }
    pub fn sync_named(&self, token: u64, data_only: bool) -> i32 {
        code(self.get(token).and_then(|file| file.sync(data_only)))
    }
    pub fn resize_named(&self, token: u64, length: u64) -> i32 {
        code(self.get(token).and_then(|file| file.resize(length)))
    }
    pub fn allocate_named(&self, token: u64, mode: i32, offset: u64, length: u64) -> i32 {
        code((|| {
            let mode = match mode {
                0 => file_io::Allocation::Reserve,
                1 => file_io::Allocation::ReserveKeepLength,
                3 => file_io::Allocation::PunchHole,
                _ => return Err(io::ErrorKind::InvalidInput.into()),
            };
            self.get(token)?.allocate(offset, length, mode)
        })())
    }
    pub fn stat_named(&self, token: u64, out: &mut file_io::bridge::ffi::FileStat) -> i32 {
        code((|| {
            *out = file_io::bridge::file_stat(self.get(token)?.file())?;
            Ok(())
        })())
    }
}
pub fn bind_named_target(
    bindings: &NamedBindings,
    token: u64,
    slot: &mut RequestSlot,
    lifetime: &mut RequestLifetime,
) -> i32 {
    let target = slot.geometry(lifetime).and_then(|(offset, length)| {
        let file = bindings.get(token)?;
        file.check_range(offset, length as u64)?;
        Ok(Target::Named { file, offset })
    });
    slot.bind_target(target, lifetime)
}

#[cfg(all(test, target_os = "linux"))]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(1);
    #[test]
    fn named_publication_rejects_mutating_flags_and_retains_exact_binding() {
        let path = std::env::temp_dir().join(format!(
            "named-binding-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir(&path).unwrap();
        let parent = Arc::new(file_io::Directory::open(&path).unwrap());
        let owner = NamedOwner::new(4096);
        owner.finish_inventory().unwrap();
        owner
            .create(
                parent.clone(),
                file_io::LeafName::new(std::ffi::OsStr::new("file")).unwrap(),
                4096,
            )
            .unwrap();
        let handle = NamedOwnerHandle(Some(owner.clone()));
        let registry = file_io::bridge::new_file_registry();
        let mut bindings = new_named_bindings();
        assert_eq!(bindings.attach_named_owner(&handle), 0);
        let file_path = path.join("file");
        let bytes = file_path.as_os_str().as_encoded_bytes();
        for flag in [libc::O_APPEND, libc::O_CREAT, libc::O_EXCL, libc::O_TRUNC] {
            let mut id = 99;
            assert_ne!(
                bindings.open_named(&registry, bytes, libc::O_RDWR | flag, 0o600, &mut id),
                0
            );
            assert_eq!(id, 0);
            assert_eq!(std::fs::metadata(&file_path).unwrap().len(), 4096);
        }
        let mut id = 0;
        assert_eq!(
            bindings.open_named(&registry, bytes, libc::O_RDWR, 0o600, &mut id),
            0
        );
        let target = bindings.get(id).unwrap();
        assert_eq!(
            file_io::FileIdentity::of(&registry.file(id).unwrap()).unwrap(),
            file_io::FileIdentity::of(target.file()).unwrap()
        );
        assert_ne!(bindings.resize_named(id, 4097), 0);
        assert_ne!(bindings.allocate_named(id, 0, u64::MAX, 2), 0);
        assert_eq!(bindings.write_named(id, b"retained", 0).bytes, 8);
        owner
            .remove(
                parent.clone(),
                file_io::LeafName::new(std::ffi::OsStr::new("file")).unwrap(),
            )
            .unwrap();
        registry.close_registered(id);
        bindings.close_named_binding(id);
        assert!(bindings.get(id).is_err());
        assert_eq!(owner.snapshot().charged, 4096);
        let mut data = [0; 8];
        target.read(&mut data, 0).result.unwrap();
        assert_eq!(&data, b"retained");
        drop(target);
        assert_eq!(owner.snapshot().charged, 0);
        owner.close();
        bindings.clear_named_bindings();
        drop(handle);
        assert!(owner.can_replace());
        drop(parent);
        std::fs::remove_dir(path).unwrap();
    }
}
