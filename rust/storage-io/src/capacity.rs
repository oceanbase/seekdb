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

use crate::{BlockError, SpaceBudget};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FileSize {
    Bytes(u64),
    DiskPercentage(u8),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GrowthPolicy {
    pub max_bytes: u64,
    pub next_bytes: u64,
    pub usage_upper_percent: u8,
}

impl FileSize {
    pub(crate) fn resolve(
        self,
        block_size: u64,
        current: u64,
        space: file_io::Space,
        budget: SpaceBudget,
    ) -> Result<u64, BlockError> {
        let requested = match self {
            Self::Bytes(bytes) => u128::from(bytes),
            Self::DiskPercentage(percent) if percent > 0 && percent < 100 => {
                let usable = (u128::from(space.capacity) + u128::from(space.available))
                    .saturating_sub(u128::from(space.free))
                    .saturating_sub(u128::from(budget.reserved_bytes));
                usable * u128::from(percent) / 100
            }
            Self::DiskPercentage(_) => {
                return Err(BlockError::InvalidInput("invalid datafile disk percentage"));
            }
        };
        if block_size == 0 || requested <= u128::from(block_size) || requested > i64::MAX as u128 {
            return Err(BlockError::InvalidInput("invalid requested datafile size"));
        }
        let target = requested as u64 / block_size * block_size;
        if u128::from(target)
            > u128::from(current)
                + u128::from(space.available.saturating_sub(budget.reserved_bytes))
        {
            return Err(BlockError::Exhausted);
        }
        Ok(target)
    }
}

impl GrowthPolicy {
    pub(crate) fn validate(self) -> Result<(), BlockError> {
        if self.max_bytes > i64::MAX as u64
            || self.next_bytes > i64::MAX as u64
            || !(1..100).contains(&self.usage_upper_percent)
        {
            return Err(BlockError::InvalidInput("invalid datafile growth policy"));
        }
        Ok(())
    }

    pub(crate) fn needs_growth(self, total: u64, free: u64) -> bool {
        let threshold =
            total - (u128::from(total) * u128::from(self.usage_upper_percent) / 100) as u64;
        free <= threshold || free <= 8
    }

    pub(crate) fn target(self, current: u64, block_size: u64) -> Result<u64, BlockError> {
        self.validate()?;
        if self.max_bytes <= current || block_size == 0 {
            return Err(BlockError::Exhausted);
        }
        let step = match self.next_bytes {
            0 => current.min(1024 * 1024 * 1024),
            value if value < 32 * 1024 * 1024 => (self.max_bytes / 10).min(32 * 1024 * 1024),
            value => value,
        };
        let target = (current + step.min(self.max_bytes - current)) / block_size * block_size;
        if target <= current {
            return Err(BlockError::Exhausted);
        }
        Ok(target)
    }
}

pub(crate) fn maximum_capacity(
    current: u64,
    block_size: u64,
    configured: u64,
    space: file_io::Space,
    budget: SpaceBudget,
) -> u64 {
    if configured <= current {
        return current;
    }
    let maximum =
        u128::from(current) + u128::from(space.available.saturating_sub(budget.reserved_bytes));
    (maximum.min(u128::from(configured)) as u64) / block_size * block_size
}

pub(crate) fn used_percentage(
    block_size: u64,
    total: u64,
    free: u64,
    maximum: u64,
    required: u64,
) -> Result<i64, BlockError> {
    if block_size == 0 || total == 0 {
        return Err(BlockError::InvalidInput("uninitialized datafile capacity"));
    }
    let available = i128::from(free) + i128::from((maximum / block_size).saturating_sub(total));
    let usage = 100 - 100 * (available - i128::from(required / block_size)) / i128::from(total);
    i64::try_from(usage).map_err(|_| BlockError::InvalidInput("datafile usage exceeds range"))
}
