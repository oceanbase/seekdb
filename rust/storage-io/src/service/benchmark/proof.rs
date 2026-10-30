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
use file_io::BenchmarkHold;
use std::sync::OnceLock;

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum Scenario {
    Stop,
    PrefixCleanup,
    RejectedCommitCleanup,
}
impl Scenario {
    pub(crate) fn cleanup(self) -> bool {
        matches!(self, Self::PrefixCleanup | Self::RejectedCommitCleanup)
    }
}

#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub enum CleanupStage {
    #[default]
    NativeReleasing,
    UpperReleasing,
}

#[derive(Clone, Copy, Default)]
pub struct PreparationCleanupFacts {
    pub native_entered: bool,
    pub native_finished: bool,
    pub native_timed_out: bool,
    pub upper_entered: bool,
    pub upper_finished: bool,
    pub upper_timed_out: bool,
    pub commit_error: Option<BenchmarkError>,
    pub rejection_observed: bool,
    pub token_retained: bool,
    pub inventory_preserved: bool,
    pub captured_targets: usize,
    pub returned_targets: usize,
    pub cleanup_order_bad: bool,
    pub shutdown_decision_seen: bool,
    pub shutdown_exit_decided: bool,
    pub shutdown_stage: CleanupStage,
    pub shutdown_run: u64,
    pub terminal: bool,
}

#[derive(Debug, PartialEq, Eq)]
pub(crate) struct TargetIdentity {
    block: bool,
    owner: usize,
    offset: u64,
    available: u64,
    device: u64,
    worker_required: bool,
    length: usize,
    alignment: usize,
}

#[derive(Default)]
pub(crate) struct CleanupState {
    pub facts: PreparationCleanupFacts,
    pub stage: CleanupStage,
    captured: Vec<TargetIdentity>,
    captured_run: u64,
    returned: Vec<TargetIdentity>,
}

pub struct UpperCleanupCompletion {
    _completion: Option<crate::drop_witness::DropCompletion>,
}

#[derive(Clone, Copy, Default)]
pub(crate) struct Facts {
    pub wait_entered: bool,
    pub selected: Option<(usize, u64)>,
    pub actor: Option<(usize, u64, u8, bool, bool)>,
}
pub(crate) struct ProofState {
    pub scenario: Scenario,
    pub facts: Mutex<Facts>,
    pub hold: Arc<BenchmarkHold>,
    pub lease: OnceLock<crate::DropWitness>,
    upper_registration: crate::drop_witness::DropRegistration,
    upper: crate::DropWitness,
    pub cleanup: Mutex<CleanupState>,
}
impl ProofState {
    pub fn new(scenario: Scenario) -> Arc<Self> {
        let upper_registration = crate::drop_witness::DropRegistration::default();
        let upper = upper_registration
            .watch()
            .expect("fresh upper registration");
        Arc::new(Self {
            scenario,
            facts: Mutex::default(),
            hold: BenchmarkHold::new(),
            lease: OnceLock::new(),
            upper_registration,
            upper,
            cleanup: Mutex::default(),
        })
    }
    pub fn claim(&self, worker: usize, request: u64) -> Option<Arc<BenchmarkHold>> {
        if self.scenario != Scenario::Stop {
            return None;
        }
        let mut facts = self.facts.lock().unwrap();
        if facts.selected.is_some() {
            return None;
        }
        facts.selected = Some((worker, request));
        Some(Arc::clone(&self.hold))
    }
    pub fn release(&self) {
        self.hold.release();
        self.upper.release();
        if let Some(lease) = self.lease.get() {
            lease.release();
        }
    }
}
impl BenchmarkPreparation {
    pub(crate) fn attach_proof(&self, proof: Arc<ProofState>) -> Result<(), BenchmarkError> {
        self.cell
            .proof
            .set(proof)
            .map_err(|_| BenchmarkError::InvalidPlan)
    }
    pub(crate) fn proof_inventory(&self) -> Vec<TargetIdentity> {
        self.targets
            .iter()
            .map(|target| {
                let (block, owner, offset, available) = match &target.target {
                    Target::Block(region) => {
                        let (owner, start, length) = region.proof_identity();
                        (true, owner, start, length)
                    }
                    Target::Named { file, offset } => (
                        false,
                        Arc::as_ptr(file) as usize,
                        *offset,
                        file.extent() - offset,
                    ),
                    Target::File { file, offset } => (
                        false,
                        Arc::as_ptr(file) as usize,
                        *offset,
                        u64::MAX - offset,
                    ),
                };
                TargetIdentity {
                    block,
                    owner,
                    offset,
                    available,
                    device: target.device.0,
                    worker_required: matches!(target.execution, Execution::WorkerRequired),
                    length: target.length,
                    alignment: target.alignment,
                }
            })
            .collect()
    }
    pub(crate) fn proof_capture_inventory(&self) {
        if let Some(proof) = self.proof().filter(|p| p.scenario.cleanup()) {
            let mut cleanup = proof.cleanup.lock().unwrap();
            cleanup.captured = self.proof_inventory();
            cleanup.captured_run = self.cell.id;
            cleanup.facts.captured_targets = cleanup.captured.len();
        }
    }
    pub(crate) fn proof_rejected_inventory(&self, error: BenchmarkError) {
        if let Some(proof) = self.proof().filter(|p| p.scenario.cleanup()) {
            let mut cleanup = proof.cleanup.lock().unwrap();
            cleanup.returned = self.proof_inventory();
            cleanup.facts.commit_error = Some(error);
            cleanup.facts.rejection_observed = true;
            cleanup.facts.token_retained =
                self.cell.id == cleanup.captured_run && self.reservation.is_some();
            cleanup.facts.returned_targets = cleanup.returned.len();
            cleanup.facts.inventory_preserved = cleanup.captured == cleanup.returned;
        }
    }
    pub(crate) fn proof(&self) -> Option<&Arc<ProofState>> {
        self.cell.proof.get()
    }
}
pub struct BenchmarkProof {
    pub(crate) state: Arc<ProofState>,
    pub(crate) run: BenchmarkRun,
}
impl Drop for BenchmarkProof {
    fn drop(&mut self) {
        self.state.release();
    }
}

impl BenchmarkRun {
    pub(crate) fn proof_run_id(&self) -> u64 {
        self.cell.id
    }
}

impl BenchmarkProof {
    pub fn proof_enter_upper_cleanup(&self) -> Box<UpperCleanupCompletion> {
        {
            let mut cleanup = self.state.cleanup.lock().unwrap();
            cleanup.stage = CleanupStage::UpperReleasing;
            cleanup.facts.cleanup_order_bad |= self.state.lease.get().is_none_or(|l| !l.finished());
        }
        self.run.cell.wake.notify();
        Box::new(UpperCleanupCompletion {
            _completion: self.state.upper_registration.enter(),
        })
    }
    pub fn proof_release_upper_cleanup(&self) {
        self.state.upper.release();
    }
    pub fn cleanup_facts(&self) -> PreparationCleanupFacts {
        let cleanup = self.state.cleanup.lock().unwrap();
        let native = self.state.lease.get().map(|lease| lease.facts());
        let upper = self.state.upper.facts();
        let mut facts = cleanup.facts;
        facts.native_entered = native.as_ref().is_some_and(|lease| lease.entered);
        facts.native_finished = native.as_ref().is_some_and(|lease| lease.finished);
        facts.native_timed_out = native.as_ref().is_some_and(|lease| lease.timed_out);
        facts.upper_entered = upper.entered;
        facts.upper_finished = upper.finished;
        facts.upper_timed_out = upper.timed_out;
        facts.terminal = self.run.status().is_terminal();
        facts
    }
}
impl BenchmarkIngress {
    pub(crate) fn cleanup_proof(&self) -> Option<(u64, Arc<ProofState>)> {
        let inbox = self.inbox.lock().unwrap();
        let cell = inbox.current.as_ref()?;
        let proof = cell.proof.get()?.clone();
        proof.scenario.cleanup().then_some((cell.id, proof))
    }
}
impl ProofState {
    pub(crate) fn observe_shutdown(&self, run: u64, can_exit: bool) {
        let mut cleanup = self.cleanup.lock().unwrap();
        cleanup.facts.shutdown_decision_seen = true;
        cleanup.facts.shutdown_exit_decided |= can_exit;
        cleanup.facts.shutdown_stage = cleanup.stage;
        cleanup.facts.shutdown_run = run;
    }
}
