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
use std::collections::BTreeSet;
const TOKEN: u128 = 1_000_000_000;
const SHARE: u128 = 1 << 32;
struct Group {
    limits: GroupLimits,
    tokens: u128,
    minimum: u128,
    finish: u128,
    eligible: bool,
    attempts: u64,
    exempt_attempts: u64,
}
impl Group {
    fn allowed(&self) -> bool {
        !matches!(self.limits.maximum_iops, Rate::PerSecond(0))
            && (matches!(self.limits.maximum_iops, Rate::Unlimited) || self.tokens >= TOKEN)
    }
    fn excess_eligible(&self) -> bool {
        self.eligible && self.allowed() && self.limits.excess != ExcessShare::None
    }
}
pub(super) struct Scheduler {
    groups: BTreeMap<GroupId, Group>,
    last: Instant,
    virtual_time: u128,
    minimum_cursor: Option<GroupId>,
}
impl Scheduler {
    pub fn new(config: &Config, now: Instant) -> Self {
        let mut scheduler = Self {
            groups: BTreeMap::new(),
            last: now,
            virtual_time: 0,
            minimum_cursor: None,
        };
        scheduler.reconfigure(config, now);
        scheduler
    }
    pub fn reconfigure(&mut self, config: &Config, now: Instant) {
        let eligible = self
            .groups
            .iter()
            .filter(|(_, group)| group.eligible)
            .map(|(&id, _)| id)
            .collect();
        self.tick(now, &eligible);
        self.groups.retain(|id, _| config.groups.contains_key(id));
        for (&id, &limits) in &config.groups {
            self.groups
                .entry(id)
                .and_modify(|group| {
                    group.limits = limits;
                    group.tokens = group.tokens.min(limits.burst as u128 * TOKEN);
                    group.minimum = if limits.minimum_iops == 0 {
                        0
                    } else {
                        group.minimum.min(limits.burst as u128 * TOKEN)
                    };
                    group.eligible = false;
                })
                .or_insert(Group {
                    limits,
                    tokens: if limits.maximum_iops == Rate::PerSecond(0) {
                        0
                    } else {
                        limits.burst as u128 * TOKEN
                    },
                    minimum: 0,
                    finish: self.virtual_time,
                    eligible: false,
                    attempts: 0,
                    exempt_attempts: 0,
                });
        }
    }
    pub fn tick(&mut self, now: Instant, eligible: &BTreeSet<GroupId>) {
        let elapsed = now.saturating_duration_since(self.last).as_nanos();
        self.last = now;
        for (&id, group) in &mut self.groups {
            let was_excess_eligible = group.excess_eligible();
            let cap = group.limits.burst as u128 * TOKEN;
            if let Rate::PerSecond(rate) = group.limits.maximum_iops {
                group.tokens = group
                    .tokens
                    .saturating_add(elapsed.saturating_mul(rate as u128))
                    .min(cap);
            }
            if group.eligible {
                group.minimum = group
                    .minimum
                    .saturating_add(elapsed.saturating_mul(group.limits.minimum_iops as u128))
                    .min(cap);
            }
            let next = eligible.contains(&id);
            if !next {
                group.minimum = 0;
            }
            group.eligible = next;
            if group.excess_eligible() && !was_excess_eligible {
                group.finish = group.finish.max(self.virtual_time);
            }
        }
    }
    pub fn allowed(&self, id: GroupId) -> bool {
        self.groups[&id].allowed()
    }
    pub fn choose(&self, eligible: &BTreeSet<GroupId>) -> Option<GroupId> {
        let credited = |id: &GroupId| self.allowed(*id) && self.groups[id].minimum >= TOKEN;
        let after_cursor = self.minimum_cursor.and_then(|cursor| {
            eligible
                .range((
                    std::ops::Bound::Excluded(cursor),
                    std::ops::Bound::Unbounded,
                ))
                .copied()
                .find(credited)
        });
        after_cursor
            .or_else(|| eligible.iter().copied().find(credited))
            .or_else(|| {
                eligible
                    .iter()
                    .copied()
                    .filter(|id| {
                        self.allowed(*id) && self.groups[id].limits.excess != ExcessShare::None
                    })
                    .min_by_key(|id| (self.groups[id].finish, *id))
            })
    }
    pub fn charge(&mut self, id: GroupId) {
        let group = self.groups.get_mut(&id).unwrap();
        let used_excess_share = group.excess_eligible();
        if !matches!(group.limits.maximum_iops, Rate::Unlimited) {
            group.tokens -= TOKEN;
        }
        if group.minimum >= TOKEN {
            self.minimum_cursor = Some(id);
        }
        group.minimum = group.minimum.saturating_sub(TOKEN);
        if let ExcessShare::Weighted(weight) = group.limits.excess {
            group.finish = group.finish.saturating_add(SHARE / weight.get() as u128);
        }
        group.attempts = group.attempts.saturating_add(1);
        let next_time = self
            .groups
            .iter()
            .filter(|(group_id, g)| g.excess_eligible() || (**group_id == id && used_excess_share))
            .map(|(_, g)| g.finish)
            .min()
            .unwrap_or(self.virtual_time);
        self.virtual_time = self.virtual_time.max(next_time);
        if self.virtual_time > (1u128 << 96) {
            let base = self.virtual_time;
            for group in self.groups.values_mut() {
                group.finish = group.finish.saturating_sub(base);
            }
            self.virtual_time = 0;
        }
    }
    pub fn exempt_attempt(&mut self, id: GroupId) {
        let g = self.groups.get_mut(&id).unwrap();
        g.attempts = g.attempts.saturating_add(1);
        g.exempt_attempts = g.exempt_attempts.saturating_add(1);
    }
    pub fn delay(&self) -> Option<Duration> {
        self.groups
            .values()
            .filter(|g| g.eligible)
            .filter_map(|g| {
                let maximum = match g.limits.maximum_iops {
                    Rate::PerSecond(0) => return None,
                    Rate::PerSecond(rate) if g.tokens < TOKEN => {
                        (TOKEN - g.tokens).div_ceil(rate as u128)
                    }
                    _ => 0,
                };
                let minimum = match g.limits.excess {
                    ExcessShare::None if g.limits.minimum_iops == 0 => return None,
                    ExcessShare::None if g.minimum < TOKEN => {
                        (TOKEN - g.minimum).div_ceil(g.limits.minimum_iops as u128)
                    }
                    _ => 0,
                };
                let nanos = maximum.max(minimum);
                (nanos != 0).then(|| Duration::from_nanos(nanos as u64))
            })
            .min()
    }
    pub fn snapshots(&self, backlogged: &BTreeSet<GroupId>) -> Vec<GroupSnapshot> {
        self.groups
            .iter()
            .map(|(&id, g)| GroupSnapshot {
                id,
                physical_attempts: g.attempts,
                rate_exempt_attempts: g.exempt_attempts,
                rate_throttled: g.eligible && !self.allowed(id),
                device_blocked: backlogged.contains(&id) && !g.eligible,
            })
            .collect()
    }
}
