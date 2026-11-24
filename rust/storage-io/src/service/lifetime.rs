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

pub(crate) struct CapturedTarget {
    pub(crate) target: Target,
    #[cfg(test)]
    pub(super) drop_hook: Option<Box<dyn FnOnce() + Send + Sync>>,
}
#[cfg(test)]
impl Drop for CapturedTarget {
    fn drop(&mut self) {
        if let Some(hook) = self.drop_hook.take() {
            hook();
        }
    }
}

/// Keeps admission and the original target alive independently of delivery or buffer recycling.
/// Used during submission and delivery; the immutable result retains its own charge.
#[derive(Default)]
pub(crate) struct RequestLifetime {
    pub(crate) target: Option<Arc<CapturedTarget>>,
    _cell: Option<Arc<ControlCell>>,
}
impl RequestLifetime {
    #[cfg(any(test, feature = "ownership-test-support"))]
    pub fn watch_final_charge(&self) -> io::Result<crate::DropWitness> {
        self._cell
            .as_ref()
            .ok_or_else(|| invalid("request has no charge"))?
            .charge
            .drop_witness
            .watch()
    }

    #[cfg(any(test, feature = "ownership-test-support"))]
    pub fn watch_final_block_lease(&self) -> io::Result<crate::DropWitness> {
        let target = &self
            .target
            .as_ref()
            .ok_or_else(|| invalid("request has no target"))?
            .target;
        match target {
            Target::Block(region) => region.watch_final_lease(),
            Target::File { .. } | Target::Named { .. } => {
                Err(invalid("request target is not a block lease"))
            }
        }
    }

    pub(super) fn reserved(cell: Arc<ControlCell>) -> Self {
        Self {
            _cell: Some(cell),
            ..Self::default()
        }
    }

    pub(crate) fn capture(&mut self, _admission: &Admission, target: Target) {
        let target = Arc::new(CapturedTarget {
            target,
            #[cfg(test)]
            drop_hook: None,
        });
        self.target = Some(target);
    }

    #[cfg(test)]
    pub(crate) fn belongs_to(&self, admission: &Admission) -> bool {
        self._cell
            .as_ref()
            .is_some_and(|cell| Arc::ptr_eq(cell, &admission.cell))
    }
}
