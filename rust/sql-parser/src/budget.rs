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

use std::cell::Cell;
use crate::StackBounds;

// Keep room for grammar backtracking, partial-tree cleanup and native calls.
#[cfg(not(target_family = "wasm"))]
const CHECKPOINT_RESERVE: usize = 256 * 1024;

pub(crate) struct Budget {
    bounds: Option<StackBounds>,
    failure: Cell<Option<usize>>,
}

impl Budget {
    pub(crate) fn new(bounds: StackBounds) -> Self {
        Self { bounds: Some(bounds), failure: Cell::new(None) }
    }

    // The shared grammar context is also required by nonrecursive scanners.
    pub(crate) fn lexical_scan() -> Self {
        Self { bounds: None, failure: Cell::new(None) }
    }

    pub(crate) fn failure(&self) -> Option<usize> { self.failure.get() }

    #[inline(never)]
    pub(crate) fn checkpoint(&self, offset: usize) -> Result<(), &'static str> {
        if self.failure.get().is_some() {
            return Err("parser stack limit");
        }
        if self.bounds.as_ref().is_some_and(checkpoint_admitted) {
            Ok(())
        } else {
            self.failure.set(Some(offset));
            Err("parser stack limit")
        }
    }
}

#[cfg(target_family = "wasm")]
fn checkpoint_admitted(bounds: &StackBounds) -> bool {
    // Linear-memory bounds do not measure the Wasm execution stack.
    bounds.low < bounds.high
}

#[cfg(not(target_family = "wasm"))]
fn checkpoint_admitted(bounds: &StackBounds) -> bool {
    #[cfg(any(target_arch = "x86_64", target_arch = "aarch64"))]
    {
        let sp = psm::stack_pointer() as usize;
        bounds.low < bounds.high && sp >= bounds.low && sp <= bounds.high
            && sp - bounds.low >= CHECKPOINT_RESERVE
    }
    #[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64")))]
    { let _ = bounds; false }
}
