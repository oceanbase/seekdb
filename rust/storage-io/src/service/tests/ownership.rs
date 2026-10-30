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
use crate::{BlockError, BlockFile, BlockIndex};
use std::path::Path;

fn config() -> BlockConfig {
    BlockConfig {
        block_size: 4096,
        initial_size: 8192,
        direct_io: false,
        punch_holes: false,
    }
}

fn open(path: &Path) -> Result<UnmarkedBlockFile, BlockError> {
    UnmarkedBlockFile::open(
        path,
        config(),
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
}

fn assert_claimed(path: &Path) {
    match open(path) {
        Err(BlockError::Io(error)) => assert_eq!(error.kind(), io::ErrorKind::WouldBlock),
        _ => panic!("live request or completion must exclude another block owner"),
    }
}

fn write(owner: &BlockFile, block: BlockIndex) -> RequestSpec {
    RequestSpec::new(
        DEVICE,
        GROUP,
        Target::Block(owner.region(block, 0..4096).unwrap()),
        4096,
    )
}

#[test]
fn charge_witness_waits_for_the_final_real_owner() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let (options, target) = spec(&file, 4096).split();
    let admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target, false);
    let witness = lifetime.watch_final_charge().unwrap();
    engine.drain_gate();
    engine.grant_admission();
    drop(admission);
    assert!(!witness.entered());
    let destructor = thread::spawn(move || drop(lifetime));
    let until = deadline();
    while !witness.entered() && Instant::now() < until {
        thread::yield_now();
    }
    let entered = witness.entered();
    let before = engine.snapshot();
    witness.release();
    destructor.join().unwrap();
    assert!(entered);
    assert_eq!(before.admitted_requests, 1);
    assert_eq!(before.reserved_bytes, 4096);
    assert!(witness.finished());
    assert!(!witness.timed_out());
    assert_eq!(engine.snapshot().admitted_requests, 0);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
}

#[test]
fn lease_witness_observes_a_region_clone_after_lifetime_release() {
    let directory = Directory::new();
    let owner = open(&directory.0.join("blocks"))
        .unwrap()
        .finish_marking()
        .unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 0..4096).unwrap();
    let retained = region.subregion(0..4096).unwrap();
    let mut engine = engine();
    let (options, target) = RequestSpec::new(DEVICE, GROUP, Target::Block(region), 4096).split();
    let admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target, false);
    let witness = lifetime.watch_final_block_lease().unwrap();
    engine.drain_gate();
    engine.grant_admission();
    drop((admission, lifetime));
    owner.release(block).unwrap();
    assert!(!witness.entered());
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    let destructor = thread::spawn(move || drop(retained));
    let until = deadline();
    while !witness.entered() && Instant::now() < until {
        thread::yield_now();
    }
    let entered = witness.entered();
    let unavailable = matches!(owner.allocate(), Err(BlockError::Exhausted));
    witness.release();
    destructor.join().unwrap();
    assert!(entered);
    assert!(unavailable);
    assert!(witness.finished());
    assert!(!witness.timed_out());
    assert_eq!(owner.allocate().unwrap(), block);
}

#[test]
fn witnesses_reject_unbound_and_non_block_lifetimes() {
    let lifetime = RequestLifetime::default();
    assert!(
        matches!(lifetime.watch_final_charge(), Err(error) if error.kind() == io::ErrorKind::InvalidInput)
    );
    assert!(
        matches!(lifetime.watch_final_block_lease(), Err(error) if error.kind() == io::ErrorKind::InvalidInput)
    );
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let (options, target) = spec(&file, 4096).split();
    let admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target, false);
    assert!(
        matches!(lifetime.watch_final_block_lease(), Err(error) if error.kind() == io::ErrorKind::InvalidInput)
    );
    let witness = lifetime.watch_final_charge().unwrap();
    assert!(
        matches!(lifetime.watch_final_charge(), Err(error) if error.kind() == io::ErrorKind::AlreadyExists)
    );
    drop(witness);
    drop((lifetime, admission));
    engine.drain_gate();
    engine.grant_admission();
}

#[test]
fn prepared_request_keeps_file_claim_until_dropped() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let owner = open(&path).unwrap().finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let service = service();
    let request = prepare(
        &service,
        write(&owner, block),
        Direction::Write,
        &[0x57; 4096],
    );
    drop(owner);
    assert_claimed(&path);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    drop(request);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    drop(open(&path).unwrap());
    service.shutdown().unwrap();
}

#[test]
fn submitted_request_keeps_claim_after_public_owner_is_dropped() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let owner = open(&path).unwrap().finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let mut engine = engine();
    let (_request_id, results) = enqueue(
        &mut engine,
        write(&owner, block),
        Direction::Write,
        &[0x57; 4096],
    );
    drop(owner);
    assert_claimed(&path);
    dispatch(&mut engine);
    assert_claimed(&path);
    let (id, retired) = retired(&engine);
    assert_claimed(&path);
    engine.retired(id, retired);
    assert_claimed(&path);
    let completion = results.recv_timeout(Duration::from_secs(1)).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(completion.data().unwrap(), &[0x57; 4096]);
    assert_claimed(&path);
    drop(completion);
    let owner = open(&path).unwrap();
    let mut bytes = [0; 16];
    owner
        .region(block, 0..16)
        .unwrap()
        .read_at(&mut bytes, 0)
        .result
        .unwrap();
    assert_eq!(bytes, [0x57; 16]);
}

#[test]
fn retired_block_can_be_reused_while_completion_keeps_file_claim() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let owner = open(&path).unwrap().finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let mut engine = engine();
    let (_request_id, results) = enqueue(
        &mut engine,
        write(&owner, block),
        Direction::Write,
        &[0x57; 4096],
    );
    owner.release(block).unwrap();
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    dispatch(&mut engine);
    let (id, retired) = retired(&engine);
    assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
    engine.retired(id, retired);
    assert_eq!(owner.allocate().unwrap(), block);
    drop(owner);
    assert_claimed(&path);
    let completion = results.recv_timeout(Duration::from_secs(1)).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_claimed(&path);
    drop(completion);
    drop(open(&path).unwrap());
}

#[test]
fn early_cancellation_releases_file_only_after_physical_retirement() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let owner = open(&path).unwrap().finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let mut engine = engine();
    let (_request_id, results) = enqueue(
        &mut engine,
        write(&owner, block),
        Direction::Write,
        &[0x57; 4096],
    );
    drop(owner);
    dispatch(&mut engine);
    assert_eq!(
        engine.cancel(_request_id, CompletionStatus::Cancelled),
        CancelOutcome::Cancelled
    );
    let completion = results.recv_timeout(Duration::from_secs(1)).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Cancelled));
    assert!(completion.buffer().is_none());
    assert_claimed(&path);
    let (id, retired) = retired(&engine);
    assert_claimed(&path);
    engine.retired(id, retired);
    drop(open(&path).unwrap());
    assert_eq!(engine.snapshot().reserved_bytes, 4096);
    drop(completion);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    assert!(results.try_recv().is_err());
}

#[test]
fn lost_completion_destination_releases_claim_at_retirement() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let owner = open(&path).unwrap().finish_marking().unwrap();
    let block = owner.allocate().unwrap();
    let mut engine = engine();
    let (_request_id, results) = enqueue(
        &mut engine,
        write(&owner, block),
        Direction::Write,
        &[0x57; 4096],
    );
    drop(owner);
    drop(results);
    dispatch(&mut engine);
    assert_claimed(&path);
    let (id, retired) = retired(&engine);
    assert_claimed(&path);
    engine.retired(id, retired);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    drop(open(&path).unwrap());
}

#[test]
fn subregion_and_retained_clone_span_cancelled_physical_retirement() {
    for retain_past_retirement in [false, true] {
        let directory = Directory::new();
        let path = directory.0.join("blocks");
        let owner = open(&path).unwrap().finish_marking().unwrap();
        let block = owner.allocate().unwrap();
        let region = owner.region(block, 8..24).unwrap();
        let retained = region.clone();
        let target = region.subregion(4..8).unwrap();
        drop(region);
        let mut engine = engine();
        let (request_id, results) = enqueue(
            &mut engine,
            RequestSpec::new(DEVICE, GROUP, Target::Block(target), 4),
            Direction::Write,
            b"late",
        );
        assert!(matches!(
            owner.release(block).unwrap(),
            crate::ReleaseOutcome::Pending
        ));
        dispatch(&mut engine);
        assert_eq!(
            engine.cancel(request_id, CompletionStatus::Cancelled),
            CancelOutcome::Cancelled
        );
        let completion = results.recv_timeout(Duration::from_secs(1)).unwrap();
        assert!(matches!(completion.status, CompletionStatus::Cancelled));
        drop(completion);
        let retained = if retain_past_retirement {
            Some(retained)
        } else {
            drop(retained);
            None
        };
        assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
        let (id, retired) = retired(&engine);
        assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
        engine.retired(id, retired);
        assert_eq!(engine.snapshot().reserved_bytes, 0);
        if let Some(retained) = retained {
            assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
            let mut bytes = [0; 4];
            retained.read_at(&mut bytes, 4).result.unwrap();
            assert_eq!(&bytes, b"late");
            drop(retained);
        }
        assert_eq!(owner.allocate().unwrap(), block);
        assert!(matches!(owner.allocate(), Err(BlockError::Exhausted)));
        let mut bytes = [0; 4];
        owner
            .region(block, 12..16)
            .unwrap()
            .read_at(&mut bytes, 0)
            .result
            .unwrap();
        assert_eq!(&bytes, b"late");
        assert!(results.try_recv().is_err());
    }
}

#[test]
fn named_timeout_and_completion_lifetime_retain_unlinked_charge() {
    let directory = Directory::new();
    let parent = Arc::new(
        file_io::Directory::open_with(
            &directory.0,
            file_io::BindingRequirement::ResolvedPathAllowed,
        )
        .unwrap(),
    );
    let name = || file_io::LeafName::new(std::ffi::OsStr::new("named")).unwrap();
    let owner = crate::NamedOwner::new(4096);
    owner.finish_inventory().unwrap();
    owner.create(parent.clone(), name(), 4096).unwrap();
    let file = Arc::new(
        OpenOptions::new()
            .read(true)
            .write(true)
            .open(directory.0.join("named"))
            .unwrap(),
    );
    let binding = owner.bind(file).unwrap();
    let mut engine = engine();
    let at = Instant::now() + Duration::from_secs(1);
    let mut request = RequestSpec::new(
        DEVICE,
        GROUP,
        Target::Named {
            file: binding.clone(),
            offset: 0,
        },
        4096,
    );
    request.policy.deadline = Some(at);
    let (options, target) = request.split();
    let mut admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target.clone(), false);
    engine.drain_gate();
    engine.grant_admission();
    admission.wait_capacity().unwrap();
    admission
        .prepare(target, Direction::Write, &[0x57; 4096])
        .unwrap();
    let (destination, results) = mpsc::channel();
    admission.publish(destination).unwrap();
    drop((admission, binding));
    engine.drain_gate();
    owner.remove(parent, name()).unwrap();
    assert_eq!(owner.snapshot().committed, 0);
    assert_eq!(owner.snapshot().charged, 4096);
    dispatch(&mut engine);
    engine.controls(at);
    let completion = results.recv_timeout(Duration::from_secs(1)).unwrap();
    assert!(matches!(completion.status, CompletionStatus::TimedOut));
    drop(completion);
    assert_eq!(owner.snapshot().charged, 4096);
    retire(&mut engine);
    assert_eq!(owner.snapshot().charged, 4096);
    drop(lifetime);
    assert_eq!(owner.snapshot().charged, 0);
}

#[test]
fn named_health_probe_holds_the_exact_lease() {
    let directory = Directory::new();
    let parent = Arc::new(
        file_io::Directory::open_with(
            &directory.0,
            file_io::BindingRequirement::ResolvedPathAllowed,
        )
        .unwrap(),
    );
    let name = || file_io::LeafName::new(std::ffi::OsStr::new("named")).unwrap();
    let owner = crate::NamedOwner::new(4096);
    owner.finish_inventory().unwrap();
    owner.create(parent.clone(), name(), 4096).unwrap();
    let binding = owner
        .bind(Arc::new(
            OpenOptions::new()
                .read(true)
                .write(true)
                .open(directory.0.join("named"))
                .unwrap(),
        ))
        .unwrap();
    let mut engine = engine();
    let (options, target) = RequestSpec::new(
        DEVICE,
        GROUP,
        Target::Named {
            file: binding.clone(),
            offset: 0,
        },
        4096,
    )
    .split();
    let admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target, false);
    let probe = super::super::health::ProbePass {
        target: lifetime.target.as_ref().unwrap().clone(),
        offset: 512,
        length: 512,
        chunk: 512,
    };
    let operation = probe.operation().unwrap();
    owner.remove(parent, name()).unwrap();
    drop((binding, probe, lifetime, admission));
    engine.drain_gate();
    engine.grant_admission();
    assert_eq!(owner.snapshot().charged, 4096);
    drop(operation);
    assert_eq!(owner.snapshot().charged, 0);
}
