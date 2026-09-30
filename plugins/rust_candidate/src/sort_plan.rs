// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Plugin-owned wire slots, never invocation-local expression IDs.
use seekdb_extension::{sys, Result};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key {
    pub column: usize,
    pub descending: bool,
    pub nulls_first: bool,
}
#[derive(Debug, PartialEq, Eq)]
pub struct Plan {
    pub keys: Vec<Key>,
    pub outputs: Vec<usize>,
}
impl Plan {
    pub fn encode(&self) -> Result<Vec<u8>> {
        if self.keys.is_empty()
            || self.keys.len() > 1024
            || self.outputs.len() > 1024
            || self.keys.iter().any(|k| k.column >= 1024)
            || self.outputs.iter().any(|&s| s >= 1024)
        {
            return Err(sys::INVALID);
        }
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(12 + self.keys.len() * 8 + self.outputs.len() * 4)
            .map_err(|_| sys::NO_MEMORY)?;
        bytes.extend_from_slice(b"SSO1");
        let mut word = |v: u32| bytes.extend_from_slice(&v.to_le_bytes());
        word(self.keys.len() as u32);
        word(self.outputs.len() as u32);
        for key in &self.keys {
            word(key.column as u32);
            word(u32::from(key.descending) | (u32::from(key.nulls_first) << 1));
        }
        for &output in &self.outputs {
            word(output as u32);
        }
        Ok(bytes)
    }
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() < 12 || &bytes[..4] != b"SSO1" {
            return Err(sys::INVALID);
        }
        let word = |at| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as usize;
        let (keys, outputs) = (word(4), word(8));
        if keys == 0 || keys > 1024 || outputs > 1024 || bytes.len() != 12 + keys * 8 + outputs * 4
        {
            return Err(sys::INVALID);
        }
        let mut plan = Self {
            keys: Vec::new(),
            outputs: Vec::new(),
        };
        plan.keys
            .try_reserve_exact(keys)
            .map_err(|_| sys::NO_MEMORY)?;
        plan.outputs
            .try_reserve_exact(outputs)
            .map_err(|_| sys::NO_MEMORY)?;
        for i in 0..keys {
            let (column, flags) = (word(12 + 8 * i), word(16 + 8 * i));
            if column >= 1024 || flags & !3 != 0 {
                return Err(sys::INVALID);
            }
            plan.keys.push(Key {
                column,
                descending: flags & 1 != 0,
                nulls_first: flags & 2 != 0,
            });
        }
        for i in 0..outputs {
            let slot = word(12 + keys * 8 + i * 4);
            if slot >= 1024 {
                return Err(sys::INVALID);
            }
            plan.outputs.push(slot);
        }
        Ok(plan)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn sort_plan_preserves_key_directions_and_independent_output_order() {
        let plan = Plan {
            keys: (0..4)
                .map(|i| Key {
                    column: 3 - i,
                    descending: i & 1 != 0,
                    nulls_first: i & 2 != 0,
                })
                .collect(),
            outputs: vec![2, 0, 2],
        };
        assert_eq!(Plan::parse(&plan.encode().unwrap()).unwrap(), plan);
        let empty = Plan {
            keys: vec![plan.keys[0]],
            outputs: vec![],
        };
        assert_eq!(Plan::parse(&empty.encode().unwrap()).unwrap(), empty);
    }
    #[test]
    fn sort_plan_rejects_truncation_trailing_bounds_and_unknown_flags() {
        let bytes = Plan {
            keys: vec![Key {
                column: 0,
                descending: false,
                nulls_first: false,
            }],
            outputs: vec![0],
        }
        .encode()
        .unwrap();
        for n in 0..bytes.len() {
            assert!(Plan::parse(&bytes[..n]).is_err());
        }
        let mut extra = bytes.clone();
        extra.push(0);
        assert!(Plan::parse(&extra).is_err());
        for (at, value) in [
            (4, 0u32),
            (4, 1025),
            (8, 1025),
            (12, 1024),
            (16, 4),
            (20, 1024),
        ] {
            let mut bad = bytes.clone();
            bad[at..at + 4].copy_from_slice(&value.to_le_bytes());
            assert!(Plan::parse(&bad).is_err());
        }
    }
}
