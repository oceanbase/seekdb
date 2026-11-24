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

use super::{
    ffi,
    requests::{self, ServiceHandle},
};
use crate::file_session::{Endpoint, FileSession};
use crate::{NamedOpenedFile, NamedOwner, OwnerPhase, Target};
use file_io::bridge::{error_number, legacy_path, DirectoryHandle};
use std::{io, sync::Arc};

pub struct NamedOwnerHandle {
    owner: Option<NamedOwner>,
    session: FileSession,
}
pub struct NamedFile(Option<(Arc<NamedOpenedFile>, Arc<Endpoint>)>);
pub fn new_named_owner() -> Box<NamedOwnerHandle> {
    Box::new(NamedOwnerHandle {
        owner: None,
        session: FileSession::default(),
    })
}
pub fn new_named_file() -> Box<NamedFile> {
    Box::new(NamedFile(None))
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
        self.owner
            .as_ref()
            .ok_or_else(|| io::ErrorKind::NotConnected.into())
    }
    pub fn begin_named_inventory(
        &mut self,
        service: &ServiceHandle,
        directory: &[u8],
        capacity: u64,
        execution: &ffi::ExecutionOptions,
    ) -> i32 {
        if !self.named_owner_replaceable() {
            return error_number(io::ErrorKind::WouldBlock.into());
        }
        let error = requests::prepare_session(&self.session, service, directory, execution);
        if error != 0 {
            return error;
        }
        self.owner = Some(NamedOwner::new(capacity));
        0
    }
    pub fn named_owner_replaceable(&self) -> bool {
        self.owner.as_ref().is_none_or(NamedOwner::can_replace)
    }
    pub fn close_named_owner(&self) -> i32 {
        requests::status(self.session.close(|| {
            if let Some(owner) = &self.owner {
                owner.close();
            }
        }))
    }
    pub fn validate_named_workers(&self, configured: usize) -> i32 {
        requests::status(self.session.validate_workers(configured))
    }
    pub fn open_named_file(&self, path: &[u8], read_only: bool, output: &mut NamedFile) -> i32 {
        code(self.session.capture(|endpoint| {
            if output.0.is_some() {
                return Err(io::ErrorKind::AlreadyExists.into());
            }
            let owner = self.get()?;
            let path = legacy_path(path)?;
            let file = if read_only {
                file_io::open_with_direct_io(
                    std::fs::OpenOptions::new().read(true),
                    &path,
                    endpoint.direct_io(),
                )?
            } else {
                file_io::open_synchronous_read_write(&path, endpoint.direct_io())?
            };
            output.0 = Some((owner.bind(Arc::new(file))?, Arc::clone(endpoint)));
            Ok(())
        }))
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
        match &self.owner {
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
impl NamedFile {
    fn get(&self) -> io::Result<&Arc<NamedOpenedFile>> {
        let (file, _) = self.0.as_ref().ok_or(io::ErrorKind::NotConnected)?;
        file.check_admission()?;
        Ok(file)
    }
    pub fn submit_named(
        &self,
        options: &ffi::RequestOptions,
        bytes: &[u8],
        callback: cxx::UniquePtr<ffi::CallbackAdapter>,
    ) -> Box<crate::Request> {
        self.submit_named_request(options, bytes, requests::take_callback(callback))
    }
    pub(super) fn submit_named_request(
        &self,
        options: &ffi::RequestOptions,
        bytes: &[u8],
        callback: Option<crate::service::CallbackOwner>,
    ) -> Box<crate::Request> {
        let captured = (|| {
            let (file, endpoint) = self.0.as_ref().ok_or(io::ErrorKind::NotConnected)?;
            file.check_admission()?;
            let plan = requests::logical_plan(options)?;
            file.check_range(plan.offset, plan.length as u64)?;
            Ok((endpoint, plan, Arc::clone(file)))
        })();
        match captured {
            Ok((endpoint, plan, file)) => requests::submit_owned(
                endpoint,
                Target::Named {
                    file,
                    offset: plan.offset,
                },
                &plan,
                options,
                bytes,
                callback,
            ),
            Err(error) => requests::reject(error, callback),
        }
    }
    pub fn read_named(&self, bytes: &mut [u8], offset: u64) -> ffi::IoResult {
        match self.get() {
            Ok(file) => transfer(file.read(bytes, offset)),
            Err(error) => transfer(file_io::Transfer {
                bytes: 0,
                result: Err(error),
            }),
        }
    }
    pub fn resize_named(&self, length: u64) -> i32 {
        code(self.get().and_then(|file| file.resize(length)))
    }
}

impl Drop for NamedOwnerHandle {
    fn drop(&mut self) {
        let _ = self.close_named_owner();
    }
}
