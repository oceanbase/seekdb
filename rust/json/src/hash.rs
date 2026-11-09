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

use crate::{BinaryError, BinaryErrorKind, BinaryValue, JsonType};

/// Both capabilities run synchronously and must not retain borrowed values or bytes.
pub trait HashCapability {
    type Error;
    fn append_bytes(&mut self, seed: u64, bytes: &[u8]) -> u64;
    fn hash_database_scalar(&mut self, seed: u64, value: BinaryValue<'_>) -> Result<u64, Self::Error>;
}

#[derive(Debug)]
pub enum HashError<E> {
    Binary(BinaryError),
    Scalar(E),
}

impl<E> From<BinaryError> for HashError<E> {
    fn from(error: BinaryError) -> Self { Self::Binary(error) }
}

/// Hashes physical member order and mixes each completed child hash into its parent's seed.
pub fn hash<C: HashCapability>(value: BinaryValue<'_>, seed: u64, capability: &mut C) -> Result<u64, HashError<C::Error>> {
    struct Frame<'a> {
        value: BinaryValue<'a>,
        next: u64,
        count: u64,
        hash: u64,
        object: bool,
    }
    let depth_bound = value.traversal_limit();
    let mut stack: Vec<Frame<'_>> = Vec::new();
    let mut value = value;
    let mut seed = seed;
    loop {
        if stack.len() > depth_bound {
            return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, 0).into());
        }
        let kind = value.json_type()?;
        let mut result = match kind {
            JsonType::Array | JsonType::Object => {
                let object = kind == JsonType::Object;
                let mut hash = capability.append_bytes(seed, &[if object { 0x31 } else { 0x30 }]);
                let count = value.len()?;
                if count == 0 { hash }
                else {
                    if object { hash = capability.append_bytes(hash, value.key(0)?); }
                    stack.push(Frame { value, next: 1, count, hash, object });
                    value = value.value(0)?;
                    seed = hash;
                    continue;
                }
            }
            JsonType::Null => capability.append_bytes(seed, &[0x32]),
            JsonType::Boolean => capability.append_bytes(seed, &[if value.boolean()? { 0x33 } else { 0x34 }]),
            _ if kind.is_string() => capability.append_bytes(seed, value.string_bytes()?),
            _ => capability.hash_database_scalar(seed, value).map_err(HashError::Scalar)?,
        };
        loop {
            let Some(frame) = stack.last_mut() else { return Ok(result); };
            frame.hash = capability.append_bytes(frame.hash, &result.to_ne_bytes());
            if frame.next == frame.count {
                result = frame.hash;
                stack.pop();
            } else {
                let index = frame.next;
                if frame.object { frame.hash = capability.append_bytes(frame.hash, frame.value.key(index)?); }
                value = frame.value.value(index)?;
                seed = frame.hash;
                frame.next += 1;
                break;
            }
        }
    }
}
