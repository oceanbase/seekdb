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

use std::io;
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::time::{Duration, Instant};

#[derive(Default)]
pub(crate) struct Facts {
    pub entered: bool,
    pub released: bool,
    pub finished: bool,
    pub timed_out: bool,
}

struct DropState {
    facts: Mutex<Facts>,
    changed: Condvar,
    timeout: Duration,
}

/// Controls one final destructor without retaining the observed resource.
pub struct DropWitness {
    state: Arc<DropState>,
}

impl DropWitness {
    #[cfg(feature = "ownership-test-support")]
    pub(crate) fn facts(&self) -> std::sync::MutexGuard<'_, Facts> {
        self.state.facts.lock().unwrap()
    }

    pub fn entered(&self) -> bool {
        self.state.facts.lock().unwrap().entered
    }

    pub fn released(&self) -> bool {
        self.state.facts.lock().unwrap().released
    }

    pub fn finished(&self) -> bool {
        self.state.facts.lock().unwrap().finished
    }

    pub fn timed_out(&self) -> bool {
        self.state.facts.lock().unwrap().timed_out
    }

    pub fn release(&self) {
        self.state.facts.lock().unwrap().released = true;
        self.state.changed.notify_all();
    }
}

impl Drop for DropWitness {
    fn drop(&mut self) {
        self.release();
    }
}

#[derive(Default)]
pub(crate) struct DropRegistration(OnceLock<Arc<DropState>>);

impl DropRegistration {
    pub(crate) fn watch(&self) -> io::Result<DropWitness> {
        self.watch_with_timeout(Duration::from_secs(10))
    }

    fn watch_with_timeout(&self, timeout: Duration) -> io::Result<DropWitness> {
        let state = Arc::new(DropState {
            facts: Mutex::default(),
            changed: Condvar::new(),
            timeout,
        });
        self.0
            .set(Arc::clone(&state))
            .map_err(|_| io::Error::new(io::ErrorKind::AlreadyExists, "owner already watched"))?;
        Ok(DropWitness { state })
    }

    pub(crate) fn enter(&self) -> Option<DropCompletion> {
        let state = Arc::clone(self.0.get()?);
        let deadline = Instant::now() + state.timeout;
        let mut facts = state.facts.lock().unwrap();
        facts.entered = true;
        state.changed.notify_all();
        while !facts.released {
            let remaining = deadline.saturating_duration_since(Instant::now());
            if remaining.is_zero() {
                facts.timed_out = true;
                break;
            }
            facts = state.changed.wait_timeout(facts, remaining).unwrap().0;
        }
        drop(facts);
        Some(DropCompletion(state))
    }
}

pub(crate) struct DropCompletion(Arc<DropState>);

impl Drop for DropCompletion {
    fn drop(&mut self) {
        self.0.facts.lock().unwrap().finished = true;
        self.0.changed.notify_all();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn release_before_entry_preserves_all_facts() {
        let registration = DropRegistration::default();
        let witness = registration.watch().unwrap();
        witness.release();
        witness.release();
        assert!(!witness.entered());
        assert!(witness.released());
        let completion = registration.enter();
        assert!(witness.entered());
        assert!(!witness.finished());
        drop(completion);
        assert!(witness.finished());
        assert!(!witness.timed_out());
    }

    #[test]
    fn dropping_controller_releases_a_waiting_destructor() {
        let registration = Arc::new(DropRegistration::default());
        let witness = registration.watch().unwrap();
        let owner = Arc::clone(&registration);
        let destructor = std::thread::spawn(move || drop(owner.enter()));
        let deadline = Instant::now() + Duration::from_secs(2);
        while !witness.entered() && Instant::now() < deadline {
            std::thread::yield_now();
        }
        let entered = witness.entered();
        let state = Arc::clone(&witness.state);
        drop(witness);
        destructor.join().unwrap();
        let facts = state.facts.lock().unwrap();
        assert!(entered);
        assert!(facts.released);
        assert!(facts.finished);
        assert!(!facts.timed_out);
    }

    #[test]
    fn deadline_is_a_sticky_failure_and_does_not_fabricate_release() {
        let registration = DropRegistration::default();
        let witness = registration.watch_with_timeout(Duration::ZERO).unwrap();
        drop(registration.enter());
        assert!(witness.entered());
        assert!(witness.finished());
        assert!(witness.timed_out());
        assert!(!witness.released());
        witness.release();
        assert!(witness.timed_out());
    }

    #[test]
    fn only_one_controller_can_watch_an_owner() {
        let registration = DropRegistration::default();
        let witness = registration.watch().unwrap();
        assert!(
            matches!(registration.watch(), Err(error) if error.kind() == io::ErrorKind::AlreadyExists)
        );
        drop(witness);
        assert!(registration.watch().is_err());
    }
}
