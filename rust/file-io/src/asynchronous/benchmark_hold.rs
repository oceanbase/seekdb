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

use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Default)]
pub struct HoldFacts {
    pub entered: bool,
    pub released: bool,
    pub executed: bool,
    pub timed_out: bool,
}
#[derive(Default)]
pub struct BenchmarkHold {
    facts: Mutex<HoldFacts>,
    changed: Condvar,
}
impl BenchmarkHold {
    pub fn new() -> Arc<Self> {
        Arc::new(Self::default())
    }
    pub fn facts(&self) -> HoldFacts {
        *self.facts.lock().unwrap()
    }
    pub fn release(&self) {
        self.facts.lock().unwrap().released = true;
        self.changed.notify_all();
    }
    pub(super) fn before_execute(&self) {
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut facts = self.facts.lock().unwrap();
        facts.entered = true;
        self.changed.notify_all();
        while !facts.released {
            let remaining = deadline.saturating_duration_since(Instant::now());
            if remaining.is_zero() {
                facts.timed_out = true;
                break;
            }
            facts = self.changed.wait_timeout(facts, remaining).unwrap().0;
        }
        facts.executed = true;
    }
}
