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

use crate::{Error, Result};

const MAX_ENTRIES: usize = 253;
const MIN_ROWS: usize = 17;
const RESERVED: u8 = 0;
const VACANT: u8 = 255;
const COLLISION: u8 = 254;
const HEADER_SIZE: usize = 3;
const MAX_BUCKETS: usize = bucket_count(MAX_ENTRIES);

const fn bucket_count(entries: usize) -> usize {
    (entries * 4 / 3) | 1
}

#[derive(Clone, Copy)]
struct Entry {
    hash: u32,
    row: u8,
}

pub(crate) struct Builder {
    entries: [Entry; MAX_ENTRIES],
    len: usize,
}

impl Builder {
    pub(crate) fn new() -> Self {
        Self {
            entries: [Entry { hash: 0, row: 0 }; MAX_ENTRIES],
            len: 0,
        }
    }

    pub(crate) fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub(crate) fn append(&mut self, hash: u64, row: usize) -> Result<()> {
        if row >= MAX_ENTRIES {
            return Err(Error::Unsupported);
        }
        if self.len > 0 && row <= usize::from(self.entries[self.len - 1].row) {
            return Err(Error::InvalidArgument);
        }
        self.entries[self.len] = Entry {
            hash: hash as u32,
            row: row as u8,
        };
        self.len += 1;
        Ok(())
    }

    pub(crate) fn estimated_size(&self, next_row: bool) -> usize {
        let len = self.len + usize::from(next_row);
        if len < MIN_ROWS {
            0
        } else {
            HEADER_SIZE + bucket_count(len)
        }
    }

    pub(crate) fn build(&self) -> Option<Trailer> {
        if self.len < MIN_ROWS {
            return None;
        }
        let count = bucket_count(self.len);
        let len = HEADER_SIZE + count;
        let mut bytes = [VACANT; HEADER_SIZE + MAX_BUCKETS];
        bytes[0] = RESERVED;
        bytes[1..HEADER_SIZE].copy_from_slice(&(count as u16).to_le_bytes());
        let mut collisions = 0;
        for entry in &self.entries[..self.len] {
            let bucket = &mut bytes[HEADER_SIZE + entry.hash as usize % count];
            match *bucket {
                VACANT => *bucket = entry.row,
                COLLISION => collisions += 1,
                _ => {
                    *bucket = COLLISION;
                    collisions += 2;
                }
            }
        }
        (collisions * 3 <= self.len * 2).then_some(Trailer { bytes, len })
    }
}

pub(crate) struct Trailer {
    bytes: [u8; HEADER_SIZE + MAX_BUCKETS],
    len: usize,
}

impl Trailer {
    pub(crate) fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }
}

pub(crate) enum Bucket {
    Vacant,
    Collision,
    Row(u8),
}

pub(crate) struct Buckets<'a>(&'a [u8]);

impl<'a> Buckets<'a> {
    pub(crate) fn parse(bytes: &'a [u8]) -> Result<Self> {
        let [RESERVED, low, high, buckets @ ..] = bytes else {
            return Err(Error::InvalidData);
        };
        let count = usize::from(u16::from_le_bytes([*low, *high]));
        if count != buckets.len() || count == 0 || count > MAX_BUCKETS {
            return Err(Error::InvalidData);
        }
        Ok(Self(buckets))
    }

    pub(crate) fn validate_rows(&self, row_count: usize) -> Result<()> {
        for &bucket in self.0 {
            Self::decode_bucket(bucket, row_count)?;
        }
        Ok(())
    }

    pub(crate) fn lookup(&self, hash: u64, row_count: usize) -> Result<Bucket> {
        Self::decode_bucket(self.0[hash as u32 as usize % self.0.len()], row_count)
    }

    fn decode_bucket(bucket: u8, row_count: usize) -> Result<Bucket> {
        Ok(match bucket {
            VACANT => Bucket::Vacant,
            COLLISION => Bucket::Collision,
            row if usize::from(row) < MAX_ENTRIES && usize::from(row) < row_count => {
                Bucket::Row(row)
            }
            _ => return Err(Error::InvalidData),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trailer_requires_enough_entries_and_limited_collisions() {
        let mut index = Builder::new();
        for row in 0..16 {
            index.append(0, row).unwrap();
        }
        assert_eq!(index.estimated_size(false), 0);
        assert_eq!(index.estimated_size(true), 26);
        assert!(index.build().is_none());
        index.append(0, 16).unwrap();
        assert_eq!(index.estimated_size(false), 26);
        assert!(index.build().is_none());
    }

    #[test]
    fn row_indices_must_fit_the_format_and_be_increasing() {
        let mut index = Builder::new();
        index.append(17, 252).unwrap();
        assert_eq!(index.append(18, 252), Err(Error::InvalidArgument));
        assert_eq!(index.append(18, 253), Err(Error::Unsupported));
    }
}
