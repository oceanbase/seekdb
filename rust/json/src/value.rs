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

use crate::binary::ValueLocation;
use crate::{BinaryDocument, BinaryResult, BinaryValue, Path, QueryOptions, QueryOutput, QueryOutputOptions};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};

struct ValueDocument {
    binary: BinaryDocument<Vec<u8>>,
    next_identity: AtomicU64,
}

/// A selected value keeps its immutable document alive independently of a scan.
#[derive(Clone)]
pub struct Value {
    document: Arc<ValueDocument>,
    location: ValueLocation,
    identity: u64,
}

pub struct Selection {
    values: Vec<Value>,
}

impl Value {
    pub(crate) fn root(document: BinaryDocument<Vec<u8>>) -> BinaryResult<Self> {
        let location = document.root()?.location();
        Ok(Self {
            document: Arc::new(ValueDocument { binary: document, next_identity: AtomicU64::new(2) }),
            location,
            identity: 1,
        })
    }

    pub fn as_binary(&self) -> BinaryValue<'_> {
        self.document.binary.value_at(self.location)
    }

    /// Stable for this selection occurrence, including across clones.
    pub fn identity(&self) -> u64 {
        self.identity
    }

    pub fn select(&self, path: &Path, options: QueryOptions) -> BinaryResult<Selection> {
        let values = path.query(self.as_binary(), options)?;
        let first_identity = self.document.next_identity.fetch_update(
            Ordering::Relaxed, Ordering::Relaxed,
            |next| next.checked_add(values.len() as u64),
        ).map_err(|_| crate::BinaryError::new(crate::BinaryErrorKind::InvalidOffset, 0))?;
        Ok(Selection {
            values: values.into_iter().enumerate().map(|(index, value)| Self {
                document: Arc::clone(&self.document),
                location: value.location(),
                identity: first_identity + index as u64,
            }).collect(),
        })
    }

    pub fn query_output(&self, path: &Path, options: QueryOutputOptions) -> BinaryResult<QueryOutput> {
        let values = path.query(self.as_binary(), QueryOptions::default())?;
        crate::path::build_query_output(values, options)
    }
}

impl Selection {
    pub fn len(&self) -> usize { self.values.len() }
    pub fn is_empty(&self) -> bool { self.values.is_empty() }
    pub fn get(&self, index: usize) -> Option<&Value> { self.values.get(index) }
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &Value> { self.values.iter() }
}
