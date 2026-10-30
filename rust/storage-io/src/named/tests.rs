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
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
static NEXT: AtomicU64 = AtomicU64::new(1);
struct Fixture {
    path: PathBuf,
    directory: Arc<Directory>,
}
impl Fixture {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!(
            "named-owner-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir(&path).unwrap();
        let directory = Arc::new(
            Directory::open_with(&path, file_io::BindingRequirement::ResolvedPathAllowed).unwrap(),
        );
        Self { path, directory }
    }
    fn name(name: &str) -> LeafName {
        LeafName::new(std::ffi::OsStr::new(name)).unwrap()
    }
    fn file(&self, name: &str) -> Arc<File> {
        Arc::new(
            std::fs::OpenOptions::new()
                .read(true)
                .write(true)
                .open(self.path.join(name))
                .unwrap(),
        )
    }
    fn ready(&self) -> NamedOwner {
        let owner = NamedOwner::new(16384);
        owner.finish_inventory().unwrap();
        owner
    }
    fn create(&self, owner: &NamedOwner, name: &str) {
        owner
            .create(self.directory.clone(), Self::name(name), 4096)
            .unwrap();
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}
#[test]
fn removal_charge_follows_real_file_and_operation_holders() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    let binding = owner.bind(f.file("one")).unwrap();
    let operation = binding
        .operation(
            file_io::AlignedBuffer::zeroed(512, 512).unwrap(),
            0,
            file_io::Direction::Read,
        )
        .unwrap();
    owner
        .remove(f.directory.clone(), Fixture::name("one"))
        .unwrap();
    assert_eq!(owner.snapshot().committed, 0);
    assert_eq!(owner.snapshot().charged, 4096);
    drop(binding);
    assert_eq!(owner.snapshot().charged, 4096);
    drop(operation);
    assert_eq!(owner.snapshot().charged, 0);
}
#[test]
fn bounded_direct_operations_preserve_extent_after_shrink() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    let binding = owner.bind(f.file("one")).unwrap();
    binding.resize(7).unwrap();
    assert!(binding.resize(4097).is_err());
    assert!(binding.write(&[9; 2], 4095).result.is_err());
    assert!(binding.write(&[9; 2], u64::MAX).result.is_err());
    assert!(binding
        .allocate(4095, 2, file_io::Allocation::Reserve)
        .is_err());
    assert!(binding
        .allocate(u64::MAX, 2, file_io::Allocation::Reserve)
        .is_err());
    assert_eq!(binding.file().metadata().unwrap().len(), 7);
    owner
        .remove(f.directory.clone(), Fixture::name("one"))
        .unwrap();
    assert_eq!(owner.snapshot().charged, 4096);
    drop(binding);
    assert_eq!(owner.snapshot().charged, 0);
}
#[test]
fn conclusive_create_rollback_preserves_availability() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::Prepare));
    assert!(owner
        .create(f.directory.clone(), Fixture::name("one"), 4096)
        .is_err());
    assert!(!f.path.join("one").exists());
    assert_eq!(owner.snapshot().charged, 0);
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    f.create(&owner, "one");
}
#[test]
fn unresolved_create_keeps_handle_charge_and_restart_barrier() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::CreateIdentity));
    assert!(owner
        .create(f.directory.clone(), Fixture::name("one"), 4096)
        .is_err());
    assert!(f.path.join("one").exists());
    assert_eq!(owner.snapshot().charged, 4096);
    owner.close();
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert!(!owner.can_replace());
    assert!(owner
        .create(f.directory.clone(), Fixture::name("two"), 4096)
        .is_err());
    assert!(owner.bind(f.file("one")).is_err());
    let s = owner.inner.state.lock().unwrap();
    assert!(s
        .records
        .values()
        .next()
        .unwrap()
        .pending
        .as_ref()
        .unwrap()
        .file
        .is_some());
}
#[test]
fn rollback_sync_failure_preserves_applied_stage() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| {
        faults
            .borrow_mut()
            .extend([Fault::Prepare, Fault::CleanupSync])
    });
    assert!(owner
        .create(f.directory.clone(), Fixture::name("one"), 4096)
        .is_err());
    assert!(!f.path.join("one").exists());
    assert_eq!(owner.snapshot().charged, 4096);
    std::fs::write(f.path.join("one"), b"replacement").unwrap();
    assert!(owner
        .remove(f.directory.clone(), Fixture::name("one"))
        .is_err());
    assert_eq!(std::fs::read(f.path.join("one")).unwrap(), b"replacement");
    owner.close();
    assert!(!owner.can_replace());
}
#[test]
fn unlink_sync_failure_never_unlinks_a_replacement() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::ParentSync));
    assert!(owner
        .remove(f.directory.clone(), Fixture::name("one"))
        .is_err());
    assert!(!f.path.join("one").exists());
    assert_eq!(owner.snapshot().charged, 4096);
    std::fs::write(f.path.join("one"), b"replacement").unwrap();
    assert!(owner
        .remove(f.directory.clone(), Fixture::name("one"))
        .is_err());
    assert_eq!(std::fs::read(f.path.join("one")).unwrap(), b"replacement");
}
#[test]
fn recovery_repairs_only_the_original_open_identity() {
    let f = Fixture::new();
    std::fs::write(f.path.join("one"), b"short").unwrap();
    let owner = NamedOwner::new(1);
    let (id, length) = owner
        .candidate(f.directory.clone(), Fixture::name("one"), 4096)
        .unwrap();
    assert_eq!(length, 5);
    assert_eq!(owner.snapshot().committed, 0);
    owner.repair(id).unwrap();
    owner.admit(id).unwrap();
    owner.finish_inventory().unwrap();
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(owner.snapshot().capacity, 1);
    assert!(owner
        .create(f.directory.clone(), Fixture::name("two"), 4096)
        .is_err());
}
#[test]
fn recovery_replacement_and_duplicate_identity_are_rejected() {
    let f = Fixture::new();
    std::fs::write(f.path.join("one"), vec![0; 4096]).unwrap();
    let owner = NamedOwner::new(16384);
    let (id, _) = owner
        .candidate(f.directory.clone(), Fixture::name("one"), 4096)
        .unwrap();
    std::fs::hard_link(f.path.join("one"), f.path.join("alias")).unwrap();
    assert!(owner
        .candidate(f.directory.clone(), Fixture::name("alias"), 4096)
        .is_err());
    owner.admit(id).unwrap();
    std::fs::rename(f.path.join("one"), f.path.join("old")).unwrap();
    std::fs::write(f.path.join("one"), vec![0; 4096]).unwrap();
    assert!(owner.finish_inventory().is_err());
    owner.close();
    assert!(!owner.can_replace());
}
#[test]
fn incomplete_inventory_and_failed_repair_cannot_publish() {
    let f = Fixture::new();
    std::fs::write(f.path.join("one"), b"short").unwrap();
    let owner = NamedOwner::new(16384);
    let (id, _) = owner
        .candidate(f.directory.clone(), Fixture::name("one"), 4096)
        .unwrap();
    assert!(owner.finish_inventory().is_err());
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::RepairSync));
    assert!(owner.repair(id).is_err());
    assert!(owner.admit(id).is_err());
    owner.close();
    assert!(!owner.can_replace());
    assert_eq!(owner.snapshot().committed, 0);
}
#[test]
fn closing_waits_for_bindings_and_attached_frontends() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    let attached = owner.clone();
    let binding = owner.bind(f.file("one")).unwrap();
    owner.close();
    assert!(!owner.can_replace());
    assert!(owner.bind(f.file("one")).is_err());
    drop(binding);
    assert!(!owner.can_replace());
    drop(attached);
    assert!(owner.can_replace());
}
#[test]
fn rename_keeps_identity_and_charge() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    let binding = owner.bind(f.file("one")).unwrap();
    owner
        .rename(
            f.directory.clone(),
            Fixture::name("one"),
            Fixture::name("two"),
            true,
        )
        .unwrap();
    assert!(!f.path.join("one").exists());
    assert!(owner.bind(f.file("two")).is_ok());
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(binding.extent(), 4096);
}

fn pause_at(stage: BarrierStage) -> (Arc<std::sync::Barrier>, Arc<std::sync::Barrier>) {
    let arrived = Arc::new(std::sync::Barrier::new(2));
    let resume = Arc::new(std::sync::Barrier::new(2));
    BARRIER.with(|cell| *cell.borrow_mut() = Some((stage, arrived.clone(), resume.clone())));
    (arrived, resume)
}
#[test]
fn concurrent_renames_reserve_destination_across_reopened_parents() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    f.create(&owner, "two");
    let reopened = Arc::new(
        Directory::open_with(&f.path, file_io::BindingRequirement::ResolvedPathAllowed).unwrap(),
    );
    let worker_owner = owner.clone();
    let parent = f.directory.clone();
    let (send, receive) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        let barriers = pause_at(BarrierStage::RenameSelected);
        send.send(barriers).unwrap();
        worker_owner.rename(parent, Fixture::name("one"), Fixture::name("dest"), true)
    });
    let (arrived, resume) = receive.recv().unwrap();
    arrived.wait();
    assert_eq!(
        owner
            .rename(reopened, Fixture::name("two"), Fixture::name("dest"), true)
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    resume.wait();
    worker.join().unwrap().unwrap();
    assert!(f.path.join("dest").exists());
    assert!(f.path.join("two").exists());
    assert_eq!(owner.snapshot().committed, 8192);
    assert_eq!(owner.snapshot().charged, 8192);
}
#[test]
fn clean_recovery_abandonment_and_close_races_drain() {
    for stage in [BarrierStage::CandidateOpened, BarrierStage::RepairSynced] {
        let f = Fixture::new();
        std::fs::write(f.path.join("one"), b"short").unwrap();
        let owner = NamedOwner::new(16384);
        let worker_owner = owner.clone();
        let parent = f.directory.clone();
        let candidate = if stage == BarrierStage::RepairSynced {
            Some(
                owner
                    .candidate(parent.clone(), Fixture::name("one"), 4096)
                    .unwrap()
                    .0,
            )
        } else {
            None
        };
        let (send, receive) = std::sync::mpsc::channel();
        let worker = std::thread::spawn(move || {
            let barriers = pause_at(stage);
            send.send(barriers).unwrap();
            if let Some(id) = candidate {
                worker_owner.repair(id)
            } else {
                worker_owner
                    .candidate(parent, Fixture::name("one"), 4096)
                    .map(|_| ())
            }
        });
        let (arrived, resume) = receive.recv().unwrap();
        arrived.wait();
        owner.close();
        assert!(!owner.can_replace());
        resume.wait();
        worker.join().unwrap().unwrap();
        assert!(owner.can_replace());
        assert_eq!(owner.snapshot().pending, 0);
    }
    let f = Fixture::new();
    std::fs::write(f.path.join("one"), b"short").unwrap();
    let owner = NamedOwner::new(16384);
    owner
        .candidate(f.directory.clone(), Fixture::name("one"), 4096)
        .unwrap();
    owner.close();
    assert!(owner.can_replace());
}
#[test]
fn applied_rename_error_reconciles_only_selected_identity() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::RenameApplied));
    owner
        .rename(
            f.directory.clone(),
            Fixture::name("one"),
            Fixture::name("dest"),
            true,
        )
        .unwrap();
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().charged, 4096);
    assert!(owner.bind(f.file("dest")).is_ok());
}
#[test]
fn rename_reconciliation_rejects_an_unrelated_destination() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    let worker_owner = owner.clone();
    let parent = f.directory.clone();
    let (send, receive) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        let barriers = pause_at(BarrierStage::RenameApplied);
        send.send(barriers).unwrap();
        FAULTS.with(|faults| faults.borrow_mut().push(Fault::RenameApplied));
        worker_owner.rename(parent, Fixture::name("one"), Fixture::name("dest"), true)
    });
    let (arrived, resume) = receive.recv().unwrap();
    arrived.wait();
    std::fs::remove_file(f.path.join("dest")).unwrap();
    std::fs::write(f.path.join("dest"), b"replacement").unwrap();
    resume.wait();
    assert!(worker.join().unwrap().is_err());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(std::fs::read(f.path.join("dest")).unwrap(), b"replacement");
}

#[test]
fn metadata_reservation_failure_has_no_filesystem_effect() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::MetadataReserve));
    assert!(owner
        .create(f.directory.clone(), Fixture::name("one"), 4096)
        .is_err());
    assert!(!f.path.join("one").exists());
    assert_eq!(owner.snapshot().charged, 0);
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
}
#[test]
fn capacity_shrink_linearizes_with_create_reservation() {
    let f = Fixture::new();
    let owner = f.ready();
    let worker_owner = owner.clone();
    let parent = f.directory.clone();
    let (send, receive) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        let barriers = pause_at(BarrierStage::CreateReserved);
        send.send(barriers).unwrap();
        worker_owner.create(parent, Fixture::name("one"), 4096)
    });
    let (arrived, resume) = receive.recv().unwrap();
    arrived.wait();
    owner.set_capacity(0).unwrap();
    assert_eq!(owner.snapshot().charged, 4096);
    resume.wait();
    worker.join().unwrap().unwrap();
    assert!(owner
        .create(f.directory.clone(), Fixture::name("two"), 4096)
        .is_err());
    assert_eq!(owner.snapshot().charged, 4096);
}
#[test]
fn directory_removal_sync_failure_retains_a_restart_barrier() {
    let f = Fixture::new();
    std::fs::create_dir(f.path.join("child")).unwrap();
    let owner = NamedOwner::new(16384);
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::ParentSync));
    assert!(owner
        .remove_directory(f.directory.clone(), Fixture::name("child"))
        .is_err());
    assert!(!f.path.join("child").exists());
    owner.close();
    assert!(!owner.can_replace());
    std::fs::create_dir(f.path.join("child")).unwrap();
    assert!(owner
        .remove_directory(f.directory.clone(), Fixture::name("child"))
        .is_err());
    assert!(f.path.join("child").exists());
}

#[test]
fn rejected_rename_without_effect_keeps_generation_usable() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    f.create(&owner, "two");
    assert!(owner
        .rename(
            f.directory.clone(),
            Fixture::name("one"),
            Fixture::name("two"),
            true
        )
        .is_err());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().charged, 8192);
    assert!(owner.bind(f.file("one")).is_ok());
    assert!(owner.bind(f.file("two")).is_ok());
}

#[test]
fn directory_lifecycle_preserves_file_identity_and_has_no_physical_charge() {
    let f = Fixture::new();
    let owner = NamedOwner::new(0);
    owner.finish_inventory().unwrap();
    owner
        .create_directory(f.directory.clone(), Fixture::name("temporary"), 0o775)
        .unwrap();
    let child = Arc::new(f.directory.open_child(&Fixture::name("temporary")).unwrap());
    let identity = child.identity().unwrap();
    owner.set_capacity(4096).unwrap();
    owner
        .create(child.clone(), Fixture::name("file"), 4096)
        .unwrap();
    let binding = owner
        .bind(Arc::new(child.open_file(&Fixture::name("file")).unwrap()))
        .unwrap();
    owner
        .rename_directory(
            f.directory.clone(),
            Fixture::name("temporary"),
            Fixture::name("active"),
            true,
        )
        .unwrap();
    f.directory
        .check_directory(&Fixture::name("active"), identity)
        .unwrap();
    owner.remove_required(child, Fixture::name("file")).unwrap();
    owner
        .remove_directory(f.directory.clone(), Fixture::name("active"))
        .unwrap();
    assert_eq!(owner.snapshot().committed, 0);
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(owner.snapshot().pending, 0);
    binding.write(b"held", 0).result.unwrap();
    drop(binding);
    assert_eq!(owner.snapshot().charged, 0);
    assert!(owner.inner.state.lock().unwrap().records.is_empty());
    owner.close();
    assert!(owner.can_replace());
}

#[test]
fn directory_create_sync_failure_returns_with_retained_identity_and_blocks_reinit() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::ParentSync));
    assert!(owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .is_err());
    let identity = f
        .directory
        .open_child(&Fixture::name("child"))
        .unwrap()
        .identity()
        .unwrap();
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().pending, 1);
    assert_eq!(owner.snapshot().charged, 0);
    assert!(owner
        .remove_directory(f.directory.clone(), Fixture::name("child"))
        .is_err());
    f.directory
        .check_directory(&Fixture::name("child"), identity)
        .unwrap();
    owner.close();
    assert!(!owner.can_replace());
}

#[test]
fn directory_rename_sync_failure_never_repeats_the_rename() {
    let f = Fixture::new();
    let owner = f.ready();
    owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .unwrap();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::ParentSync));
    assert!(owner
        .rename_directory(
            f.directory.clone(),
            Fixture::name("child"),
            Fixture::name("moved"),
            true
        )
        .is_err());
    assert!(!f.path.join("child").exists());
    assert!(f.path.join("moved").exists());
    std::fs::create_dir(f.path.join("child")).unwrap();
    assert!(owner
        .rename_directory(
            f.directory.clone(),
            Fixture::name("child"),
            Fixture::name("other"),
            true
        )
        .is_err());
    assert!(f.path.join("child").exists());
    assert!(!f.path.join("other").exists());
    owner.close();
    assert!(!owner.can_replace());
}

#[test]
fn directory_rename_reconciles_only_the_selected_identity_after_applied_error() {
    let f = Fixture::new();
    let owner = f.ready();
    owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .unwrap();
    let child = f.directory.open_child(&Fixture::name("child")).unwrap();
    let identity = child.identity().unwrap();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::RenameApplied));
    owner
        .rename_directory(
            f.directory.clone(),
            Fixture::name("child"),
            Fixture::name("moved"),
            true,
        )
        .unwrap();
    f.directory
        .check_directory(&Fixture::name("moved"), identity)
        .unwrap();
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().active, 0);
    assert_eq!(owner.snapshot().pending, 0);
}

#[test]
fn directory_rename_rejects_replacement_identity_after_applied_error() {
    let f = Fixture::new();
    let owner = f.ready();
    owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .unwrap();
    let worker_owner = owner.clone();
    let parent = f.directory.clone();
    let (send, receive) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        FAULTS.with(|faults| faults.borrow_mut().push(Fault::RenameApplied));
        send.send(pause_at(BarrierStage::RenameApplied)).unwrap();
        worker_owner.rename_directory(parent, Fixture::name("child"), Fixture::name("moved"), true)
    });
    let (arrived, resume) = receive.recv().unwrap();
    arrived.wait();
    std::fs::rename(f.path.join("moved"), f.path.join("selected")).unwrap();
    std::fs::create_dir(f.path.join("moved")).unwrap();
    resume.wait();
    assert!(worker.join().unwrap().is_err());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert!(f.path.join("moved").is_dir());
    assert!(f.path.join("selected").is_dir());
}

#[test]
fn directory_mutation_reservation_excludes_file_mutations_through_reopened_children() {
    let f = Fixture::new();
    let owner = f.ready();
    owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .unwrap();
    let child = Arc::new(f.directory.open_child(&Fixture::name("child")).unwrap());
    owner.create(child, Fixture::name("file"), 4096).unwrap();
    let reopened = Arc::new(f.directory.open_child(&Fixture::name("child")).unwrap());
    let worker_owner = owner.clone();
    let parent = f.directory.clone();
    let (send, receive) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        send.send(pause_at(BarrierStage::RenameSelected)).unwrap();
        worker_owner.rename_directory(parent, Fixture::name("child"), Fixture::name("moved"), true)
    });
    let (arrived, resume) = receive.recv().unwrap();
    arrived.wait();
    assert_eq!(
        owner
            .create(reopened.clone(), Fixture::name("other"), 4096)
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    assert_eq!(
        owner
            .remove(reopened.clone(), Fixture::name("file"))
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    assert_eq!(
        owner
            .rename(
                reopened.clone(),
                Fixture::name("file"),
                Fixture::name("other"),
                true
            )
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    resume.wait();
    worker.join().unwrap().unwrap();
    owner.remove(reopened, Fixture::name("file")).unwrap();
    owner
        .remove_directory(f.directory.clone(), Fixture::name("moved"))
        .unwrap();
    assert_eq!(owner.snapshot().charged, 0);
}

#[test]
fn file_reservation_excludes_directory_create_rename_and_remove() {
    let f = Fixture::new();
    let owner = f.ready();
    owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .unwrap();
    let child = Arc::new(f.directory.open_child(&Fixture::name("child")).unwrap());
    let worker_owner = owner.clone();
    let (send, receive) = std::sync::mpsc::channel();
    let worker = std::thread::spawn(move || {
        send.send(pause_at(BarrierStage::CreateReserved)).unwrap();
        worker_owner.create(child, Fixture::name("file"), 4096)
    });
    let (arrived, resume) = receive.recv().unwrap();
    arrived.wait();
    assert_eq!(
        owner
            .create_directory(f.directory.clone(), Fixture::name("other"), 0o775)
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    assert_eq!(
        owner
            .rename_directory(
                f.directory.clone(),
                Fixture::name("child"),
                Fixture::name("moved"),
                true
            )
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    assert_eq!(
        owner
            .remove_directory(f.directory.clone(), Fixture::name("child"))
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    resume.wait();
    worker.join().unwrap().unwrap();
    assert!(f.path.join("child/file").exists());
    assert!(!f.path.join("other").exists());
    assert!(!f.path.join("moved").exists());
}

#[test]
fn namespace_rename_failure_keeps_original_file_selectable_for_retry() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    let binding = owner.bind(f.file("one")).unwrap();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::Rename));
    assert!(owner
        .rename(
            f.directory.clone(),
            Fixture::name("one"),
            Fixture::name("one.tmp"),
            true
        )
        .is_err());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().committed, 4096);
    assert!(f.path.join("one").exists());
    owner
        .rename(
            f.directory.clone(),
            Fixture::name("one"),
            Fixture::name("one.tmp"),
            true,
        )
        .unwrap();
    owner
        .remove_required(f.directory.clone(), Fixture::name("one.tmp"))
        .unwrap();
    assert_eq!(owner.snapshot().committed, 0);
    assert_eq!(owner.snapshot().charged, 4096);
    drop(binding);
    assert_eq!(owner.snapshot().charged, 0);
}

#[test]
fn incomplete_namespace_transition_fails_generation_even_when_unlink_did_not_apply() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "one");
    owner
        .rename(
            f.directory.clone(),
            Fixture::name("one"),
            Fixture::name("one.tmp"),
            true,
        )
        .unwrap();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::Remove));
    assert!(owner
        .remove_required(f.directory.clone(), Fixture::name("one.tmp"))
        .is_err());
    assert!(f.path.join("one.tmp").exists());
    assert!(!f.path.join("one").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().pending, 1);
    assert_eq!(owner.snapshot().charged, 4096);
    assert!(owner
        .remove_required(f.directory.clone(), Fixture::name("one.tmp"))
        .is_err());
    owner.close();
    assert!(!owner.can_replace());
}

#[test]
fn directory_metadata_reservation_fails_before_mkdir() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::MetadataReserve));
    assert!(owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .is_err());
    assert!(!f.path.join("child").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .unwrap();
    assert!(owner
        .create_directory(f.directory.clone(), Fixture::name("child"), 0o775)
        .is_err());
    assert!(f.path.join("child").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().pending, 0);
}

fn publish(owner: &NamedOwner, f: &Fixture) -> io::Result<()> {
    owner.create_and_publish(
        f.directory.clone(),
        Fixture::name("next.tmp"),
        Fixture::name("next"),
        4096,
        true,
    )
}

#[test]
fn unapplied_publication_rolls_back_and_same_names_retry() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::Rename));
    assert_eq!(
        publish(&owner, &f).unwrap_err().kind(),
        io::ErrorKind::StorageFull
    );
    assert!(!f.path.join("next.tmp").exists());
    assert!(!f.path.join("next").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().charged, 0);
    assert_eq!(owner.snapshot().committed, 0);
    publish(&owner, &f).unwrap();
    assert!(!f.path.join("next.tmp").exists());
    assert_eq!(std::fs::metadata(f.path.join("next")).unwrap().len(), 4096);
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(owner.snapshot().committed, 4096);
}

#[test]
fn failed_publication_does_not_remove_preexisting_names() {
    for existing in ["next.tmp", "next"] {
        let f = Fixture::new();
        let owner = f.ready();
        std::fs::write(f.path.join(existing), b"existing").unwrap();
        assert!(publish(&owner, &f).is_err());
        assert_eq!(std::fs::read(f.path.join(existing)).unwrap(), b"existing");
        assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
        assert_eq!(owner.snapshot().charged, 0);
        if existing == "next" {
            assert!(!f.path.join("next.tmp").exists());
        }
    }
}

#[test]
fn publication_cleanup_failure_retains_barrier_and_prior_leases() {
    let f = Fixture::new();
    let owner = f.ready();
    f.create(&owner, "prior");
    let binding = owner.bind(f.file("prior")).unwrap();
    let operation = binding
        .operation(
            file_io::AlignedBuffer::zeroed(512, 512).unwrap(),
            0,
            file_io::Direction::Read,
        )
        .unwrap();
    owner
        .remove(f.directory.clone(), Fixture::name("prior"))
        .unwrap();
    FAULTS.with(|faults| {
        faults
            .borrow_mut()
            .extend([Fault::Rename, Fault::CleanupSync])
    });
    assert!(publish(&owner, &f).is_err());
    assert!(!f.path.join("next.tmp").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().charged, 8192);
    assert_eq!(owner.snapshot().committed, 0);
    std::fs::write(f.path.join("next.tmp"), b"replacement").unwrap();
    assert!(publish(&owner, &f).is_err());
    assert_eq!(
        std::fs::read(f.path.join("next.tmp")).unwrap(),
        b"replacement"
    );
    drop(binding);
    assert_eq!(owner.snapshot().charged, 8192);
    drop(operation);
    assert_eq!(owner.snapshot().charged, 4096);
    owner.close();
    assert!(!owner.can_replace());
}

#[test]
fn publication_sync_failure_retains_published_file_and_charge() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::ParentSync));
    assert!(publish(&owner, &f).is_err());
    assert!(!f.path.join("next.tmp").exists());
    assert!(f.path.join("next").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(owner.snapshot().committed, 0);
    assert!(publish(&owner, &f).is_err());
}

#[test]
fn publication_reconciles_only_the_selected_moved_identity() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::RenameApplied));
    publish(&owner, &f).unwrap();
    assert!(!f.path.join("next.tmp").exists());
    assert!(f.path.join("next").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Ready);
    assert_eq!(owner.snapshot().committed, 4096);
    assert_eq!(owner.snapshot().charged, 4096);
}

#[test]
fn publication_without_reconciliation_retains_applied_error() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().push(Fault::RenameApplied));
    assert!(owner
        .create_and_publish(
            f.directory.clone(),
            Fixture::name("next.tmp"),
            Fixture::name("next"),
            4096,
            false,
        )
        .is_err());
    assert!(!f.path.join("next.tmp").exists());
    assert!(f.path.join("next").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().charged, 4096);
}

#[test]
fn publication_reserves_both_names_before_create() {
    let f = Fixture::new();
    let owner = f.ready();
    std::thread::scope(|scope| {
        let (send, receive) = std::sync::mpsc::channel();
        let owner_ref = &owner;
        let fixture = &f;
        let worker = scope.spawn(move || {
            send.send(pause_at(BarrierStage::CreateReserved)).unwrap();
            publish(owner_ref, fixture)
        });
        let (arrived, resume) = receive.recv().unwrap();
        arrived.wait();
        for name in ["next.tmp", "next"] {
            assert_eq!(
                owner
                    .create(f.directory.clone(), Fixture::name(name), 4096)
                    .unwrap_err()
                    .kind(),
                io::ErrorKind::WouldBlock
            );
        }
        assert_eq!(owner.snapshot().charged, 4096);
        resume.wait();
        worker.join().unwrap().unwrap();
    });
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(owner.snapshot().committed, 4096);
}

#[test]
fn publication_failed_cleanup_unlink_keeps_temporary_identity() {
    let f = Fixture::new();
    let owner = f.ready();
    FAULTS.with(|faults| faults.borrow_mut().extend([Fault::Rename, Fault::Remove]));
    assert!(publish(&owner, &f).is_err());
    assert!(f.path.join("next.tmp").exists());
    assert!(!f.path.join("next").exists());
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().charged, 4096);
    assert_eq!(owner.snapshot().committed, 0);
    assert!(publish(&owner, &f).is_err());
    owner.close();
    assert!(!owner.can_replace());
}

#[test]
fn publication_rejects_replaced_temporary_identity_without_cleanup() {
    let f = Fixture::new();
    let owner = f.ready();
    std::thread::scope(|scope| {
        let (send, receive) = std::sync::mpsc::channel();
        let owner_ref = &owner;
        let fixture = &f;
        let worker = scope.spawn(move || {
            FAULTS.with(|faults| faults.borrow_mut().push(Fault::Rename));
            send.send(pause_at(BarrierStage::RenameApplied)).unwrap();
            publish(owner_ref, fixture)
        });
        let (arrived, resume) = receive.recv().unwrap();
        arrived.wait();
        std::fs::rename(f.path.join("next.tmp"), f.path.join("selected")).unwrap();
        std::fs::write(f.path.join("next.tmp"), b"replacement").unwrap();
        resume.wait();
        assert!(worker.join().unwrap().is_err());
    });
    assert_eq!(
        std::fs::read(f.path.join("next.tmp")).unwrap(),
        b"replacement"
    );
    assert_eq!(
        std::fs::metadata(f.path.join("selected")).unwrap().len(),
        4096
    );
    assert_eq!(owner.snapshot().phase, OwnerPhase::Failed);
    assert_eq!(owner.snapshot().charged, 4096);
}
