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

use file_io::{Directory, EntryKind, FileIdentity, Follow, LeafName, Observation};
use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::sync::{Arc, Mutex};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OwnerPhase {
    Recovering,
    Ready,
    Failed,
    Closing,
}
#[derive(Clone, Copy, Debug)]
pub struct NamedSnapshot {
    pub phase: OwnerPhase,
    pub committed: u64,
    pub charged: u64,
    pub capacity: u64,
    pub leases: usize,
    pub pending: usize,
    pub active: usize,
    pub error: i32,
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum Namespace {
    Present,
    Busy,
    Removed,
}
#[derive(Clone, Copy, Debug)]
enum Stage {
    Selected,
    Created,
    Prepared,
    Closed,
    Applied,
    Synced,
}
struct Pending {
    parent: Arc<Directory>,
    name: LeafName,
    destination: Option<LeafName>,
    file: Option<File>,
    directory: Option<Directory>,
    stage: Stage,
    syscall_error: Option<io::Error>,
    postcheck_error: Option<io::Error>,
    failure: Option<io::Error>,
}
enum RenameFailure {
    NotApplied(io::Error),
    Unresolved(io::Error),
}
impl Pending {
    fn rename_selected(
        &mut self,
        identity: FileIdentity,
        accept_moved_identity: bool,
    ) -> Result<(), RenameFailure> {
        barrier(BarrierStage::RenameSelected);
        let mut effect = if fault(Fault::Rename).is_err() {
            file_io::Mutation::not_applied(io::ErrorKind::StorageFull.into())
        } else if self.directory.is_some() {
            self.parent.rename_directory_selected(
                &self.name,
                self.destination.as_ref().unwrap(),
                identity,
            )
        } else {
            self.parent
                .rename_selected(&self.name, self.destination.as_ref().unwrap(), identity)
        };
        barrier(BarrierStage::RenameApplied);
        if effect.syscall.is_ok() && fault(Fault::RenameApplied).is_err() {
            effect.syscall = Err(io::ErrorKind::StorageFull.into());
        }
        if effect.syscall.is_ok() {
            self.stage = Stage::Applied;
        }
        self.postcheck_error = effect.postcheck.err();
        if let Err(error) = effect.syscall {
            self.syscall_error = Some(duplicate(&error));
            let reconciled = accept_moved_identity
                && self.postcheck_error.is_none()
                && matches!(
                    error.kind(),
                    io::ErrorKind::StorageFull | io::ErrorKind::NotFound
                )
                && matches!(
                    self.parent.inspect(&self.name, Follow::No),
                    Ok(Observation::Missing)
                )
                && self
                    .check_identity(self.destination.as_ref().unwrap(), identity)
                    .is_ok();
            if !reconciled {
                if self.postcheck_error.is_none()
                    && self.check_identity(&self.name, identity).is_ok()
                {
                    return Err(RenameFailure::NotApplied(error));
                }
                return Err(RenameFailure::Unresolved(error));
            }
            self.stage = Stage::Applied;
        }
        if let Some(error) = self.postcheck_error.as_ref().map(duplicate) {
            return Err(RenameFailure::Unresolved(error));
        }
        Ok(())
    }

    fn check_identity(&self, name: &LeafName, identity: FileIdentity) -> io::Result<()> {
        if self.directory.is_some() {
            self.parent.check_directory(name, identity)
        } else {
            self.parent.check_file(name, identity)
        }
    }
}
struct Candidate {
    parent: Arc<Directory>,
    name: LeafName,
    file: Option<File>,
    admitted: bool,
}
struct Selection {
    directory: bool,
    parent: FileIdentity,
    source: LeafName,
    destination: Option<LeafName>,
}
impl Selection {
    fn new(
        parent: &Directory,
        source: &LeafName,
        destination: Option<&LeafName>,
        directory: bool,
    ) -> io::Result<Self> {
        Ok(Self {
            directory,
            parent: parent.identity()?,
            source: LeafName::new(source.as_os_str())?,
            destination: destination
                .map(|n| LeafName::new(n.as_os_str()))
                .transpose()?,
        })
    }
    fn conflicts(&self, other: &Self) -> bool {
        self.parent == other.parent
            && (self.source == other.source
                || self
                    .destination
                    .as_ref()
                    .is_some_and(|n| n == &other.source || Some(n) == other.destination.as_ref())
                || other.destination.as_ref() == Some(&self.source))
    }
}
struct Record {
    selection: Option<Selection>,
    identity: Option<FileIdentity>,
    extent: u64,
    accepted: bool,
    namespace: Namespace,
    leases: usize,
    pending: Option<Pending>,
    candidate: Option<Candidate>,
}
struct State {
    phase: OwnerPhase,
    records: HashMap<u64, Record>,
    next: u64,
    capacity: u64,
    committed: u64,
    charged: u64,
    active: usize,
    failure: Option<(io::ErrorKind, Option<i32>)>,
}
struct Inner {
    state: Mutex<State>,
}
#[derive(Clone)]
pub struct NamedOwner {
    inner: Arc<Inner>,
}
pub struct NamedOpenedFile {
    // The file closes before the last physical charge is released.
    file: Arc<File>,
    lease: PhysicalLease,
}
struct PhysicalLease {
    owner: Arc<Inner>,
    key: u64,
    extent: u64,
}
fn invalid() -> io::Error {
    io::ErrorKind::InvalidInput.into()
}
fn duplicate(error: &io::Error) -> io::Error {
    saved_error(&(error.kind(), error.raw_os_error()))
}
fn memory() -> io::Error {
    io::ErrorKind::OutOfMemory.into()
}
fn saved_error(error: &(io::ErrorKind, Option<i32>)) -> io::Error {
    error
        .1
        .map(io::Error::from_raw_os_error)
        .unwrap_or_else(|| error.0.into())
}
impl State {
    fn check(&self, recovering: bool) -> io::Result<()> {
        if let Some(error) = &self.failure {
            return Err(saved_error(error));
        }
        if self.phase == OwnerPhase::Ready || recovering && self.phase == OwnerPhase::Recovering {
            Ok(())
        } else {
            Err(io::ErrorKind::NotConnected.into())
        }
    }
    fn fail(&mut self, error: &io::Error) {
        self.failure
            .get_or_insert((error.kind(), error.raw_os_error()));
        self.phase = OwnerPhase::Failed;
    }
    fn reserve_name(&self, selection: &Selection) -> io::Result<()> {
        // Directory moves can change the parents of any active file operation.
        if selection.directory && self.active != 0
            || self
                .records
                .values()
                .filter_map(|r| r.selection.as_ref())
                .any(|s| s.directory || s.conflicts(selection))
        {
            Err(io::ErrorKind::WouldBlock.into())
        } else {
            Ok(())
        }
    }
    fn key(&mut self) -> io::Result<u64> {
        fault(Fault::MetadataReserve)?;
        self.records.try_reserve(1).map_err(|_| memory())?;
        let key = self.next;
        self.next = key.checked_add(1).ok_or_else(memory)?;
        Ok(key)
    }
    fn find(&self, identity: FileIdentity) -> Option<u64> {
        self.records
            .iter()
            .find_map(|(&key, value)| (value.identity == Some(identity)).then_some(key))
    }
    fn retire(&mut self, key: u64) {
        if self.records.get(&key).is_some_and(|r| {
            r.namespace == Namespace::Removed && r.leases == 0 && r.pending.is_none()
        }) {
            let record = self.records.remove(&key).unwrap();
            self.charged -= record.extent;
        }
    }
}
impl NamedOwner {
    pub fn new(capacity: u64) -> Self {
        Self {
            inner: Arc::new(Inner {
                state: Mutex::new(State {
                    phase: OwnerPhase::Recovering,
                    records: HashMap::new(),
                    next: 1,
                    capacity,
                    committed: 0,
                    charged: 0,
                    active: 0,
                    failure: None,
                }),
            }),
        }
    }
    pub fn snapshot(&self) -> NamedSnapshot {
        let s = self.inner.state.lock().unwrap();
        NamedSnapshot {
            phase: s.phase,
            committed: s.committed,
            charged: s.charged,
            capacity: s.capacity,
            active: s.active,
            leases: s.records.values().map(|r| r.leases).sum(),
            pending: s
                .records
                .values()
                .filter(|r| r.pending.is_some() || r.candidate.is_some())
                .count(),
            error: s.failure.as_ref().and_then(|e| e.1).unwrap_or(0),
        }
    }
    pub fn close(&self) {
        let mut s = self.inner.state.lock().unwrap();
        if s.failure.is_none() {
            s.phase = OwnerPhase::Closing;
        }
        drop(s);
        self.drain_candidates();
    }
    fn drain_candidates(&self) {
        let mut records = {
            let mut s = self.inner.state.lock().unwrap();
            if s.phase != OwnerPhase::Closing
                || s.failure.is_some()
                || s.active != 0
                || !s.records.values().any(|r| r.candidate.is_some())
            {
                return;
            }
            s.active = 1;
            std::mem::take(&mut s.records)
        };
        let mut error = None;
        records.retain(|_, record| {
            let Some(candidate) = record.candidate.as_mut() else {
                return true;
            };
            if error.is_some() {
                return true;
            }
            if let Some(file) = candidate.file.take() {
                if let Err(failure) = file_io::close_file(file) {
                    error = Some(failure);
                    return true;
                }
            }
            false
        });
        let mut s = self.inner.state.lock().unwrap();
        s.records = records;
        s.active = 0;
        if let Some(error) = error {
            s.fail(&error);
        }
    }
    pub fn can_replace(&self) -> bool {
        let s = self.inner.state.lock().unwrap();
        s.phase == OwnerPhase::Closing
            && s.failure.is_none()
            && s.active == 0
            && s.records
                .values()
                .all(|r| r.leases == 0 && r.pending.is_none() && r.candidate.is_none())
            && Arc::strong_count(&self.inner) == 1
    }
    pub fn set_capacity(&self, capacity: u64) -> io::Result<()> {
        let mut s = self.inner.state.lock().unwrap();
        s.check(false)?;
        s.capacity = capacity;
        Ok(())
    }
    pub fn candidate(
        &self,
        parent: Arc<Directory>,
        name: LeafName,
        extent: u64,
    ) -> io::Result<(u64, u64)> {
        if extent == 0 || extent > i64::MAX as u64 {
            return Err(invalid());
        }
        let key = {
            let mut s = self.inner.state.lock().unwrap();
            if s.phase != OwnerPhase::Recovering || s.active != 0 {
                return Err(invalid());
            }
            s.check(true)?;
            let key = s.key()?;
            s.active += 1;
            key
        };
        let result = (|| {
            let file = parent.open_file(&name)?;
            barrier(BarrierStage::CandidateOpened);
            let metadata = file.metadata()?;
            if !metadata.is_file() {
                return Err(invalid());
            }
            let identity = FileIdentity::of(&file)?;
            parent.check_file(&name, identity)?;
            Ok((file, identity, metadata.len()))
        })();
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        let selected = result.and_then(|(file, identity, length)| {
            if s.find(identity).is_some() {
                return Err(invalid());
            }
            s.records.insert(
                key,
                Record {
                    selection: None,
                    identity: Some(identity),
                    extent,
                    accepted: false,
                    namespace: Namespace::Present,
                    leases: 0,
                    pending: None,
                    candidate: Some(Candidate {
                        parent,
                        name,
                        file: Some(file),
                        admitted: false,
                    }),
                },
            );
            Ok((key, length))
        });
        drop(s);
        self.drain_candidates();
        selected
    }
    pub fn repair(&self, key: u64) -> io::Result<()> {
        let (candidate, identity, extent) = {
            let mut s = self.inner.state.lock().unwrap();
            if s.phase != OwnerPhase::Recovering || s.active != 0 {
                return Err(invalid());
            }
            s.check(true)?;
            let r = s.records.get_mut(&key).ok_or_else(invalid)?;
            let c = r.candidate.take().ok_or_else(invalid)?;
            let result = (c, r.identity.unwrap(), r.extent);
            s.active += 1;
            result
        };
        let result = (|| {
            candidate.parent.check_file(&candidate.name, identity)?;
            let file = candidate.file.as_ref().ok_or_else(invalid)?;
            file.set_len(extent)?;
            fault(Fault::RepairSync)?;
            file.sync_all()?;
            barrier(BarrierStage::RepairSynced);
            candidate.parent.check_file(&candidate.name, identity)
        })();
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        s.records.get_mut(&key).unwrap().candidate = Some(candidate);
        if let Err(error) = &result {
            s.fail(error);
        }
        drop(s);
        self.drain_candidates();
        result
    }
    pub fn admit(&self, key: u64) -> io::Result<()> {
        let mut s = self.inner.state.lock().unwrap();
        if s.phase != OwnerPhase::Recovering || s.active != 0 {
            return Err(invalid());
        }
        s.check(true)?;
        s.records
            .get_mut(&key)
            .and_then(|r| r.candidate.as_mut())
            .ok_or_else(invalid)?
            .admitted = true;
        Ok(())
    }
    pub fn finish_inventory(&self) -> io::Result<()> {
        let mut records = {
            let mut s = self.inner.state.lock().unwrap();
            if s.phase != OwnerPhase::Recovering || s.active != 0 {
                return Err(invalid());
            }
            s.check(true)?;
            if s.records
                .values()
                .any(|r| r.candidate.as_ref().is_none_or(|c| !c.admitted))
            {
                return Err(invalid());
            }
            s.active = 1;
            std::mem::take(&mut s.records)
        };
        let result = (|| {
            let mut total = 0u64;
            for r in records.values() {
                let c = r.candidate.as_ref().unwrap();
                c.parent.check_file(&c.name, r.identity.unwrap())?;
                let file = c.file.as_ref().ok_or_else(invalid)?;
                if file.metadata()?.len() != r.extent {
                    return Err(invalid());
                }
                total = total
                    .checked_add(r.extent)
                    .filter(|n| *n <= i64::MAX as u64)
                    .ok_or_else(invalid)?;
            }
            for r in records.values_mut() {
                let c = r.candidate.as_mut().unwrap();
                file_io::close_file(c.file.take().unwrap())?;
                c.parent.check_file(&c.name, r.identity.unwrap())?;
            }
            Ok(total)
        })();
        let mut s = self.inner.state.lock().unwrap();
        s.active = 0;
        match result {
            Ok(total) => {
                for r in records.values_mut() {
                    r.candidate = None;
                    r.accepted = true;
                }
                s.records = records;
                s.committed = total;
                s.charged = total;
                if s.phase == OwnerPhase::Recovering {
                    s.phase = OwnerPhase::Ready;
                } else {
                    return Err(io::ErrorKind::NotConnected.into());
                }
                Ok(())
            }
            Err(error) => {
                s.records = records;
                s.fail(&error);
                Err(error)
            }
        }
    }
    pub fn bind(&self, file: Arc<File>) -> io::Result<Arc<NamedOpenedFile>> {
        let identity = FileIdentity::of(&file)?;
        let mut s = self.inner.state.lock().unwrap();
        s.check(false)?;
        let key = s
            .find(identity)
            .ok_or_else(|| io::Error::from(io::ErrorKind::NotFound))?;
        let r = s.records.get_mut(&key).unwrap();
        if !r.accepted || r.namespace != Namespace::Present {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        r.leases = r.leases.checked_add(1).ok_or_else(memory)?;
        Ok(Arc::new(NamedOpenedFile {
            file,
            lease: PhysicalLease {
                owner: self.inner.clone(),
                key,
                extent: r.extent,
            },
        }))
    }
    pub fn create(&self, parent: Arc<Directory>, name: LeafName, extent: u64) -> io::Result<()> {
        self.create_entry(parent, name, extent, None)
    }
    pub fn create_and_publish(
        &self,
        parent: Arc<Directory>,
        name: LeafName,
        destination: LeafName,
        extent: u64,
        accept_moved_identity: bool,
    ) -> io::Result<()> {
        if name == destination {
            return Err(invalid());
        }
        self.create_entry(
            parent,
            name,
            extent,
            Some((destination, accept_moved_identity)),
        )
    }
    fn create_entry(
        &self,
        parent: Arc<Directory>,
        name: LeafName,
        extent: u64,
        publication: Option<(LeafName, bool)>,
    ) -> io::Result<()> {
        if extent == 0 || extent > i64::MAX as u64 {
            return Err(invalid());
        }
        let selection = Selection::new(&parent, &name, publication.as_ref().map(|p| &p.0), false)?;
        let key = {
            let mut s = self.inner.state.lock().unwrap();
            s.check(false)?;
            s.reserve_name(&selection)?;
            let charged = s
                .charged
                .checked_add(extent)
                .filter(|n| *n <= i64::MAX as u64)
                .ok_or_else(invalid)?;
            if charged > s.capacity {
                return Err(io::ErrorKind::StorageFull.into());
            }
            let key = s.key()?;
            s.records.insert(
                key,
                Record {
                    selection: Some(selection),
                    identity: None,
                    extent,
                    accepted: false,
                    namespace: Namespace::Busy,
                    leases: 0,
                    pending: None,
                    candidate: None,
                },
            );
            s.charged = charged;
            s.active += 1;
            key
        };
        barrier(BarrierStage::CreateReserved);
        let accept_moved_identity = publication.as_ref().is_some_and(|p| p.1);
        let mut pending = Pending {
            parent,
            name,
            destination: publication.map(|p| p.0),
            file: None,
            directory: None,
            stage: Stage::Selected,
            syscall_error: None,
            postcheck_error: None,
            failure: None,
        };
        let effect = pending.parent.create_file(&pending.name, 0o644);
        let file = match effect.syscall {
            Ok(file) => file,
            Err(error) => {
                if let Err(post) = effect.postcheck {
                    pending.postcheck_error = Some(post);
                    pending.syscall_error = Some(duplicate(&error));
                    return self.unresolved(key, pending, error);
                }
                self.forget_reservation(key);
                return Err(error);
            }
        };
        pending.file = Some(file);
        pending.stage = Stage::Created;
        if let Err(error) = effect.postcheck {
            let returned = duplicate(&error);
            pending.postcheck_error = Some(error);
            return self.unresolved(key, pending, returned);
        }
        let identity = match fault(Fault::CreateIdentity)
            .and_then(|()| FileIdentity::of(pending.file.as_ref().unwrap()))
        {
            Ok(identity) => identity,
            Err(error) => return self.unresolved(key, pending, error),
        };
        self.inner
            .state
            .lock()
            .unwrap()
            .records
            .get_mut(&key)
            .unwrap()
            .identity = Some(identity);
        if let Err(error) = pending.parent.check_file(&pending.name, identity) {
            return self.unresolved(key, pending, error);
        }
        let prepared = (|| {
            fault(Fault::Prepare)?;
            file_io::extend_file(pending.file.as_ref().unwrap(), extent)?;
            pending.file.as_ref().unwrap().sync_all()
        })();
        if let Err(error) = prepared {
            return self.rollback_create(key, pending, identity, error);
        }
        pending.stage = Stage::Prepared;
        let closed = file_io::close_file(pending.file.take().unwrap());
        pending.stage = Stage::Closed;
        if let Err(error) = closed {
            return self.unresolved(key, pending, error);
        }
        if pending.destination.is_some() {
            match pending.rename_selected(identity, accept_moved_identity) {
                Ok(()) => (),
                Err(RenameFailure::NotApplied(error)) => {
                    return self.rollback_create(key, pending, identity, error);
                }
                Err(RenameFailure::Unresolved(error)) => {
                    return self.unresolved(key, pending, error);
                }
            }
        }
        if let Err(error) = fault(Fault::ParentSync).and_then(|()| pending.parent.sync()) {
            return self.unresolved(key, pending, error);
        }
        pending.stage = Stage::Synced;
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        let r = s.records.get_mut(&key).unwrap();
        r.namespace = Namespace::Present;
        r.accepted = true;
        r.selection = None;
        s.committed += extent;
        drop(s);
        self.drain_candidates();
        Ok(())
    }
    pub fn create_directory(
        &self,
        parent: Arc<Directory>,
        name: LeafName,
        mode: u32,
    ) -> io::Result<()> {
        let selection = Selection::new(&parent, &name, None, true)?;
        let key = {
            let mut s = self.inner.state.lock().unwrap();
            s.check(true)?;
            s.reserve_name(&selection)?;
            let key = s.key()?;
            s.records.insert(
                key,
                Record {
                    selection: Some(selection),
                    identity: None,
                    extent: 0,
                    accepted: false,
                    namespace: Namespace::Busy,
                    leases: 0,
                    pending: None,
                    candidate: None,
                },
            );
            s.active += 1;
            key
        };
        barrier(BarrierStage::DirectoryReserved);
        let mut pending = Pending {
            parent,
            name,
            destination: None,
            file: None,
            directory: None,
            stage: Stage::Selected,
            syscall_error: None,
            postcheck_error: None,
            failure: None,
        };
        let effect = pending.parent.create_directory(&pending.name, mode);
        if effect.syscall.is_ok() {
            pending.stage = Stage::Created;
        }
        pending.postcheck_error = effect.postcheck.err();
        if let Err(error) = effect.syscall {
            if pending.postcheck_error.is_none() {
                return self.not_applied(key, pending, error);
            }
            pending.syscall_error = Some(duplicate(&error));
            return self.unresolved(key, pending, error);
        }
        if let Some(error) = pending.postcheck_error.as_ref().map(duplicate) {
            return self.unresolved(key, pending, error);
        }
        let selected = pending.parent.open_child(&pending.name).and_then(|child| {
            let identity = child.identity()?;
            pending.directory = Some(child);
            pending.parent.check_directory(&pending.name, identity)?;
            Ok(identity)
        });
        let identity = match selected {
            Ok(identity) => identity,
            Err(error) => return self.unresolved(key, pending, error),
        };
        self.inner
            .state
            .lock()
            .unwrap()
            .records
            .get_mut(&key)
            .unwrap()
            .identity = Some(identity);
        if let Err(error) = fault(Fault::ParentSync).and_then(|()| pending.parent.sync()) {
            return self.unresolved(key, pending, error);
        }
        pending.stage = Stage::Synced;
        if let Some(child) = pending.directory.take() {
            if let Err(error) = child.close() {
                return self.unresolved(key, pending, error);
            }
        }
        self.forget_reservation(key);
        Ok(())
    }
    fn rollback_create(
        &self,
        key: u64,
        mut pending: Pending,
        identity: FileIdentity,
        primary: io::Error,
    ) -> io::Result<()> {
        if let Some(file) = pending.file.take() {
            pending.stage = Stage::Closed;
            if let Err(error) = file_io::close_file(file) {
                pending.failure = Some(primary);
                return self.unresolved(key, pending, error);
            }
        }
        let effect = match fault(Fault::Remove) {
            Ok(()) => pending.parent.remove_selected(&pending.name, identity),
            Err(error) => file_io::Mutation::not_applied(error),
        };
        if effect.syscall.is_ok() {
            pending.stage = Stage::Applied;
        }
        pending.postcheck_error = effect.postcheck.err();
        if let Err(error) = effect.syscall {
            pending.failure = Some(primary);
            pending.syscall_error = Some(duplicate(&error));
            return self.unresolved(key, pending, error);
        }
        if let Some(error) = pending.postcheck_error.as_ref().map(duplicate) {
            pending.failure = Some(primary);
            return self.unresolved(key, pending, error);
        }
        if let Err(error) = fault(Fault::CleanupSync).and_then(|()| pending.parent.sync()) {
            pending.failure = Some(primary);
            return self.unresolved(key, pending, error);
        }
        self.forget_reservation(key);
        Err(primary)
    }
    fn forget_reservation(&self, key: u64) {
        let mut s = self.inner.state.lock().unwrap();
        let r = s.records.remove(&key).unwrap();
        s.charged -= r.extent;
        s.active -= 1;
        drop(s);
        self.drain_candidates();
    }
    fn unresolved(&self, key: u64, mut pending: Pending, error: io::Error) -> io::Result<()> {
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        s.fail(&error);
        pending.failure.get_or_insert_with(|| duplicate(&error));
        s.records.get_mut(&key).unwrap().pending = Some(pending);
        Err(error)
    }
    fn not_applied(&self, key: u64, mut pending: Pending, error: io::Error) -> io::Result<()> {
        if let Some(file) = pending.file.take() {
            pending.stage = Stage::Closed;
            if let Err(close_error) = file_io::close_file(file) {
                pending.syscall_error = Some(error);
                return self.unresolved(key, pending, close_error);
            }
        }
        if let Some(child) = pending.directory.take() {
            if let Err(close_error) = child.close() {
                pending.syscall_error = Some(error);
                return self.unresolved(key, pending, close_error);
            }
        }
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        let record = s.records.get_mut(&key).unwrap();
        if record.accepted {
            record.namespace = Namespace::Present;
            record.selection = None;
        } else {
            let record = s.records.remove(&key).unwrap();
            s.charged -= record.extent;
        }
        drop(s);
        self.drain_candidates();
        Err(error)
    }
    fn select_mutation(
        &self,
        parent: Arc<Directory>,
        name: LeafName,
        destination: Option<LeafName>,
        directory: bool,
    ) -> io::Result<(u64, FileIdentity, Pending)> {
        // The selected open pins physical identity through namespace execution.
        let selected_directory = if directory {
            Some(parent.open_child(&name)?)
        } else {
            None
        };
        let (identity, length, file) = if let Some(child) = &selected_directory {
            let identity = child.identity()?;
            parent.check_directory(&name, identity)?;
            (identity, 0, None)
        } else {
            let file = parent.open_file(&name)?;
            let metadata = file.metadata()?;
            if !metadata.is_file() {
                return Err(invalid());
            }
            (FileIdentity::of(&file)?, metadata.len(), Some(file))
        };
        let selection = Selection::new(&parent, &name, destination.as_ref(), directory)?;
        let mut s = self.inner.state.lock().unwrap();
        s.check(true)?;
        s.reserve_name(&selection)?;
        let key = if let Some(key) = s.find(identity) {
            let r = s.records.get_mut(&key).unwrap();
            if r.namespace != Namespace::Present || r.candidate.is_some() {
                return Err(io::ErrorKind::WouldBlock.into());
            }
            r.namespace = Namespace::Busy;
            r.selection = Some(selection);
            key
        } else {
            if !directory && s.phase != OwnerPhase::Recovering {
                return Err(io::ErrorKind::NotFound.into());
            }
            let charged = s.charged.checked_add(length).ok_or_else(invalid)?;
            let key = s.key()?;
            s.records.insert(
                key,
                Record {
                    selection: Some(selection),
                    identity: Some(identity),
                    extent: length,
                    accepted: false,
                    namespace: Namespace::Busy,
                    leases: 0,
                    pending: None,
                    candidate: None,
                },
            );
            s.charged = charged;
            key
        };
        s.active += 1;
        Ok((
            key,
            identity,
            Pending {
                parent,
                name,
                destination,
                file,
                directory: selected_directory,
                stage: Stage::Selected,
                syscall_error: None,
                postcheck_error: None,
                failure: None,
            },
        ))
    }
    pub fn remove(&self, parent: Arc<Directory>, name: LeafName) -> io::Result<()> {
        self.remove_entry(parent, name, false, false)
    }
    /// Completes a caller-selected namespace transition. Any failure blocks new admission.
    pub fn remove_required(&self, parent: Arc<Directory>, name: LeafName) -> io::Result<()> {
        self.remove_entry(parent, name, false, true)
    }
    pub fn remove_directory(&self, parent: Arc<Directory>, name: LeafName) -> io::Result<()> {
        self.remove_entry(parent, name, true, false)
    }
    fn remove_entry(
        &self,
        parent: Arc<Directory>,
        name: LeafName,
        directory: bool,
        required: bool,
    ) -> io::Result<()> {
        let selected = self.select_mutation(parent, name, None, directory);
        let (key, identity, mut pending) = match selected {
            Ok(selected) => selected,
            Err(error) => {
                if required {
                    self.inner.state.lock().unwrap().fail(&error);
                }
                return Err(error);
            }
        };
        barrier(BarrierStage::RemoveSelected);
        let effect = if fault(Fault::Remove).is_err() {
            file_io::Mutation::not_applied(io::ErrorKind::PermissionDenied.into())
        } else if directory {
            match pending.parent.inspect(&pending.name, Follow::No) {
                Ok(Observation::Present {
                    kind: EntryKind::Directory,
                    identity: found,
                    ..
                }) if identity == found => pending.parent.remove_directory(&pending.name),
                Ok(_) => file_io::Mutation::not_applied(invalid()),
                Err(error) => file_io::Mutation::not_applied(error),
            }
        } else {
            pending.parent.remove_selected(&pending.name, identity)
        };
        if effect.syscall.is_ok() {
            pending.stage = Stage::Applied;
        }
        pending.postcheck_error = effect.postcheck.err();
        if let Err(error) = effect.syscall {
            let unchanged = match pending.parent.inspect(&pending.name, Follow::No) {
                Ok(Observation::Present {
                    identity: found, ..
                }) => found == identity,
                _ => false,
            };
            if !required && pending.postcheck_error.is_none() && unchanged {
                return self.not_applied(key, pending, error);
            }
            pending.syscall_error = Some(duplicate(&error));
            return self.unresolved(key, pending, error);
        }
        if let Some(error) = pending.postcheck_error.as_ref().map(duplicate) {
            return self.unresolved(key, pending, error);
        }
        if let Err(error) = fault(Fault::ParentSync).and_then(|()| pending.parent.sync()) {
            return self.unresolved(key, pending, error);
        }
        pending.stage = Stage::Synced;
        // Drop the namespace operation's selected file before releasing charge.
        if let Some(file) = pending.file.take() {
            if let Err(error) = file_io::close_file(file) {
                return self.unresolved(key, pending, error);
            }
        }
        if let Some(child) = pending.directory.take() {
            if let Err(error) = child.close() {
                return self.unresolved(key, pending, error);
            }
        }
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        let r = s.records.get_mut(&key).unwrap();
        r.namespace = Namespace::Removed;
        r.selection = None;
        let committed = if r.accepted { r.extent } else { 0 };
        r.accepted = false;
        s.committed -= committed;
        s.retire(key);
        drop(s);
        self.drain_candidates();
        Ok(())
    }
    pub fn rename(
        &self,
        parent: Arc<Directory>,
        old: LeafName,
        new: LeafName,
        accept_moved_identity: bool,
    ) -> io::Result<()> {
        self.rename_entry(parent, old, new, accept_moved_identity, false)
    }
    pub fn rename_directory(
        &self,
        parent: Arc<Directory>,
        old: LeafName,
        new: LeafName,
        accept_moved_identity: bool,
    ) -> io::Result<()> {
        self.rename_entry(parent, old, new, accept_moved_identity, true)
    }
    fn rename_entry(
        &self,
        parent: Arc<Directory>,
        old: LeafName,
        new: LeafName,
        accept_moved_identity: bool,
        directory: bool,
    ) -> io::Result<()> {
        let (key, identity, mut pending) =
            self.select_mutation(parent, old, Some(new), directory)?;
        match pending.rename_selected(identity, accept_moved_identity) {
            Ok(()) => (),
            Err(RenameFailure::NotApplied(error)) => return self.not_applied(key, pending, error),
            Err(RenameFailure::Unresolved(error)) => return self.unresolved(key, pending, error),
        }
        if let Err(error) = fault(Fault::ParentSync).and_then(|()| pending.parent.sync()) {
            return self.unresolved(key, pending, error);
        }
        if let Some(file) = pending.file.take() {
            if let Err(error) = file_io::close_file(file) {
                return self.unresolved(key, pending, error);
            }
        }
        if let Some(child) = pending.directory.take() {
            if let Err(error) = child.close() {
                return self.unresolved(key, pending, error);
            }
        }
        let mut s = self.inner.state.lock().unwrap();
        s.active -= 1;
        let r = s.records.get_mut(&key).unwrap();
        r.namespace = if directory {
            Namespace::Removed
        } else {
            Namespace::Present
        };
        r.selection = None;
        s.retire(key);
        drop(s);
        self.drain_candidates();
        Ok(())
    }
}
impl Drop for PhysicalLease {
    fn drop(&mut self) {
        let mut s = self.owner.state.lock().unwrap();
        s.records.get_mut(&self.key).unwrap().leases -= 1;
        s.retire(self.key);
    }
}
impl NamedOpenedFile {
    #[cfg(feature = "cxx")]
    pub(crate) fn check_admission(&self) -> io::Result<()> {
        self.lease.owner.state.lock().unwrap().check(false)
    }
    pub fn extent(&self) -> u64 {
        self.lease.extent
    }
    pub fn check_range(&self, offset: u64, length: u64) -> io::Result<()> {
        match offset.checked_add(length) {
            Some(end) if end <= self.extent() => Ok(()),
            _ => Err(invalid()),
        }
    }
    pub fn read(&self, bytes: &mut [u8], offset: u64) -> file_io::Transfer {
        match self.check_range(offset, bytes.len() as u64) {
            Ok(()) => file_io::read_up_to_at(&self.file, bytes, offset),
            Err(error) => file_io::Transfer {
                bytes: 0,
                result: Err(error),
            },
        }
    }
    pub fn write(&self, bytes: &[u8], offset: u64) -> file_io::Transfer {
        match self.check_range(offset, bytes.len() as u64) {
            Ok(()) => file_io::write_fully_at(&self.file, bytes, offset),
            Err(error) => file_io::Transfer {
                bytes: 0,
                result: Err(error),
            },
        }
    }
    pub fn resize(&self, length: u64) -> io::Result<()> {
        self.check_range(0, length)?;
        self.file.set_len(length)
    }
    pub fn allocate(&self, offset: u64, length: u64, mode: file_io::Allocation) -> io::Result<()> {
        self.check_range(offset, length)?;
        file_io::allocate(&self.file, offset..offset + length, mode)
    }
    pub fn sync(&self, data_only: bool) -> io::Result<()> {
        if data_only {
            self.file.sync_data()
        } else {
            self.file.sync_all()
        }
    }
    #[cfg(any(test, feature = "cxx"))]
    pub(crate) fn file(&self) -> &File {
        &self.file
    }
    pub(crate) fn operation(
        self: &Arc<Self>,
        buffer: file_io::AlignedBuffer,
        offset: u64,
        direction: file_io::Direction,
    ) -> io::Result<file_io::Operation> {
        self.check_range(offset, buffer.as_slice().len() as u64)?;
        let operation = match direction {
            file_io::Direction::Read => {
                file_io::Operation::read(self.file.clone(), buffer, offset)?
            }
            file_io::Direction::Write => {
                file_io::Operation::write(self.file.clone(), buffer, offset)?
            }
        };
        Ok(operation.keep_alive(self.clone()))
    }
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum Fault {
    Remove,
    Rename,
    CreateIdentity,
    Prepare,
    ParentSync,
    CleanupSync,
    RepairSync,
    RenameApplied,
    MetadataReserve,
}
#[cfg(test)]
thread_local! { static FAULTS: std::cell::RefCell<Vec<Fault>> = const { std::cell::RefCell::new(Vec::new()) }; }
fn fault(stage: Fault) -> io::Result<()> {
    #[cfg(test)]
    if FAULTS.with(|faults| {
        let mut faults = faults.borrow_mut();
        if faults.first() == Some(&stage) {
            faults.remove(0);
            true
        } else {
            false
        }
    }) {
        return Err(io::Error::other("injected named ownership fault"));
    }
    let _ = stage;
    Ok(())
}
#[cfg(test)]
mod tests;

#[derive(Clone, Copy, PartialEq, Eq)]
enum BarrierStage {
    DirectoryReserved,
    CandidateOpened,
    RepairSynced,
    RenameSelected,
    RenameApplied,
    CreateReserved,
    RemoveSelected,
}
#[cfg(test)]
thread_local! { static BARRIER: std::cell::RefCell<Option<(BarrierStage, Arc<std::sync::Barrier>, Arc<std::sync::Barrier>)>> = const { std::cell::RefCell::new(None) }; }
fn barrier(stage: BarrierStage) {
    #[cfg(test)]
    BARRIER.with(|cell| {
        let value = cell.borrow_mut().take();
        if let Some((selected, arrived, resume)) = value {
            if selected == stage {
                arrived.wait();
                resume.wait();
            } else {
                *cell.borrow_mut() = Some((selected, arrived, resume));
            }
        }
    });
    let _ = stage;
}
