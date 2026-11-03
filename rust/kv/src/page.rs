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

pub const PAGE_SIZE: usize = 4096;
pub const MAX_KEY_SIZE: usize = 512;
pub const MAX_VALUE_SIZE: usize = 16 * 1024 * 1024;
pub(crate) const INLINE_VALUE_SIZE: usize = 1024;
pub(crate) const HEADER_SIZE: usize = 24;
pub(crate) const PAYLOAD_SIZE: usize = PAGE_SIZE - HEADER_SIZE - 4;
pub(crate) const MAX_DEPTH: usize = 64;
const PAGE_MAGIC: &[u8; 8] = b"KVPAGE01";
const ROOT_MAGIC: &[u8; 8] = b"KVROOT01";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Root {
    pub generation: u64,
    pub page: u64,
    pub pages: u64,
}

#[derive(Debug)]
pub(crate) struct Value {
    pub length: usize,
    pub head: u64,
    pub inline: Vec<u8>,
}

#[derive(Debug)]
pub(crate) struct Entry {
    pub key: Vec<u8>,
    pub value: Value,
}

#[derive(Debug)]
pub(crate) struct Child {
    pub key: Vec<u8>,
    pub page: u64,
}

#[derive(Debug)]
pub(crate) enum Node {
    Leaf(Vec<Entry>),
    Branch(Vec<Child>),
}

impl Node {
    pub fn len(&self) -> usize {
        match self {
            Self::Leaf(entries) => entries.len(),
            Self::Branch(children) => children.len(),
        }
    }

    pub fn first_key(&self) -> Option<&[u8]> {
        match self {
            Self::Leaf(entries) => entries.first().map(|entry| entry.key.as_slice()),
            Self::Branch(children) => children.first().map(|child| child.key.as_slice()),
        }
    }

    fn item_size(&self, index: usize) -> usize {
        match self {
            Self::Leaf(entries) => {
                let entry = &entries[index];
                14 + entry.key.len() + entry.value.inline.len()
            }
            Self::Branch(children) => 10 + children[index].key.len(),
        }
    }

    pub fn encoded_size(&self) -> usize {
        (0..self.len()).map(|index| self.item_size(index)).sum()
    }

    pub fn split(&mut self) -> Result<Self> {
        let total = self.encoded_size();
        let mut left = 0;
        let mut best = None;
        for index in 1..self.len() {
            left += self.item_size(index - 1);
            let right = total - left;
            if left <= PAYLOAD_SIZE && right <= PAYLOAD_SIZE {
                let weight = left.max(right);
                if best.is_none_or(|(_, previous)| weight < previous) {
                    best = Some((index, weight));
                }
            }
        }
        let index = best.ok_or(Error::Corrupt("unsplittable tree page"))?.0;
        Ok(match self {
            Self::Leaf(entries) => Self::Leaf(entries.split_off(index)),
            Self::Branch(children) => Self::Branch(children.split_off(index)),
        })
    }

    pub fn encode(&self, id: u64) -> Result<[u8; PAGE_SIZE]> {
        let mut payload = Vec::with_capacity(self.encoded_size());
        match self {
            Self::Leaf(entries) => {
                for entry in entries {
                    payload.extend_from_slice(&(entry.key.len() as u16).to_le_bytes());
                    payload.extend_from_slice(&(entry.value.length as u32).to_le_bytes());
                    payload.extend_from_slice(&entry.value.head.to_le_bytes());
                    payload.extend_from_slice(&entry.key);
                    payload.extend_from_slice(&entry.value.inline);
                }
            }
            Self::Branch(children) => {
                for child in children {
                    payload.extend_from_slice(&(child.key.len() as u16).to_le_bytes());
                    payload.extend_from_slice(&child.page.to_le_bytes());
                    payload.extend_from_slice(&child.key);
                }
            }
        }
        encode_page(
            id,
            if matches!(self, Self::Leaf(_)) { 1 } else { 2 },
            self.len(),
            &payload,
        )
    }

    pub fn decode(page: &[u8; PAGE_SIZE], id: u64) -> Result<Self> {
        let (kind, count, payload) = decode_page(page, id)?;
        // A corrupt count must not request an unbounded allocation.
        if count > PAYLOAD_SIZE / 10 {
            return Err(Error::Corrupt("invalid tree item count"));
        }
        let mut input = Input(payload);
        let node = match kind {
            1 => {
                let mut entries = Vec::with_capacity(count);
                for _ in 0..count {
                    let key_length = input.u16()? as usize;
                    let length = input.u32()? as usize;
                    let head = input.u64()?;
                    if key_length > MAX_KEY_SIZE || length > MAX_VALUE_SIZE {
                        return Err(Error::Corrupt("invalid key or value length"));
                    }
                    let key = input.take(key_length)?.to_vec();
                    let inline = if head == 0 {
                        if length > INLINE_VALUE_SIZE {
                            return Err(Error::Corrupt("invalid inline value length"));
                        }
                        input.take(length)?.to_vec()
                    } else {
                        Vec::new()
                    };
                    entries.push(Entry {
                        key,
                        value: Value {
                            length,
                            head,
                            inline,
                        },
                    });
                }
                Self::Leaf(entries)
            }
            2 => {
                if count == 0 {
                    return Err(Error::Corrupt("empty branch page"));
                }
                let mut children = Vec::with_capacity(count);
                for _ in 0..count {
                    let length = input.u16()? as usize;
                    let page = input.u64()?;
                    if length > MAX_KEY_SIZE {
                        return Err(Error::Corrupt("invalid branch key length"));
                    }
                    children.push(Child {
                        key: input.take(length)?.to_vec(),
                        page,
                    });
                }
                Self::Branch(children)
            }
            _ => return Err(Error::Corrupt("expected tree page")),
        };
        if !input.0.is_empty() {
            return Err(Error::Corrupt("trailing tree payload"));
        }
        let keys: Box<dyn Iterator<Item = &[u8]> + '_> = match &node {
            Self::Leaf(entries) => Box::new(entries.iter().map(|entry| entry.key.as_slice())),
            Self::Branch(children) => Box::new(children.iter().map(|child| child.key.as_slice())),
        };
        let mut previous = None;
        for key in keys {
            if previous.is_some_and(|previous| previous >= key) {
                return Err(Error::Corrupt("unordered tree keys"));
            }
            previous = Some(key);
        }
        Ok(node)
    }
}

pub(crate) fn encode_overflow(id: u64, next: u64, bytes: &[u8]) -> Result<[u8; PAGE_SIZE]> {
    let mut payload = Vec::with_capacity(8 + bytes.len());
    payload.extend_from_slice(&next.to_le_bytes());
    payload.extend_from_slice(bytes);
    encode_page(id, 3, 0, &payload)
}

pub(crate) fn decode_overflow(page: &[u8; PAGE_SIZE], id: u64) -> Result<(u64, &[u8])> {
    let (kind, count, payload) = decode_page(page, id)?;
    if kind != 3 || count != 0 || payload.len() <= 8 {
        return Err(Error::Corrupt("invalid overflow page"));
    }
    let mut input = Input(payload);
    Ok((input.u64()?, input.0))
}

fn encode_page(id: u64, kind: u8, count: usize, payload: &[u8]) -> Result<[u8; PAGE_SIZE]> {
    if payload.len() > PAYLOAD_SIZE {
        return Err(Error::Corrupt("tree page exceeds page size"));
    }
    let mut page = [0; PAGE_SIZE];
    page[..8].copy_from_slice(PAGE_MAGIC);
    page[8..16].copy_from_slice(&id.to_le_bytes());
    page[16] = kind;
    page[18..20].copy_from_slice(&(count as u16).to_le_bytes());
    page[20..24].copy_from_slice(&(payload.len() as u32).to_le_bytes());
    page[HEADER_SIZE..HEADER_SIZE + payload.len()].copy_from_slice(payload);
    seal(&mut page);
    Ok(page)
}

fn decode_page(page: &[u8; PAGE_SIZE], id: u64) -> Result<(u8, usize, &[u8])> {
    if &page[..8] != PAGE_MAGIC || !valid_checksum(page) {
        return Err(Error::Corrupt("invalid page header or checksum"));
    }
    let mut input = Input(&page[8..24]);
    if input.u64()? != id {
        return Err(Error::Corrupt("page identity mismatch"));
    }
    let kind = input.take(2)?[0];
    let count = input.u16()? as usize;
    let length = input.u32()? as usize;
    if length > PAYLOAD_SIZE {
        return Err(Error::Corrupt("invalid page payload size"));
    }
    Ok((kind, count, &page[HEADER_SIZE..HEADER_SIZE + length]))
}

pub(crate) fn encode_root(root: Root) -> [u8; PAGE_SIZE] {
    let mut page = [0; PAGE_SIZE];
    page[..8].copy_from_slice(ROOT_MAGIC);
    page[8..12].copy_from_slice(&1u32.to_le_bytes());
    page[12..16].copy_from_slice(&(PAGE_SIZE as u32).to_le_bytes());
    page[16..24].copy_from_slice(&root.generation.to_le_bytes());
    page[24..32].copy_from_slice(&root.page.to_le_bytes());
    page[32..40].copy_from_slice(&root.pages.to_le_bytes());
    seal(&mut page);
    page
}

pub(crate) fn decode_root(page: &[u8; PAGE_SIZE], physical_pages: u64) -> Option<Root> {
    if &page[..8] != ROOT_MAGIC || !valid_checksum(page) {
        return None;
    }
    let mut input = Input(&page[8..40]);
    if input.u32().ok()? != 1 || input.u32().ok()? != PAGE_SIZE as u32 {
        return None;
    }
    let root = Root {
        generation: input.u64().ok()?,
        page: input.u64().ok()?,
        pages: input.u64().ok()?,
    };
    (root.generation > 0
        && root.generation < u64::MAX
        && root.page >= 2
        && root.page < root.pages
        && root.pages <= physical_pages)
        .then_some(root)
}

const fn crc_table() -> [u32; 256] {
    let mut table = [0; 256];
    let mut index = 0;
    while index < 256 {
        let mut crc = index as u32;
        let mut bit = 0;
        while bit < 8 {
            crc = (crc >> 1) ^ if crc & 1 != 0 { 0x82f63b78 } else { 0 };
            bit += 1;
        }
        table[index] = crc;
        index += 1;
    }
    table
}

const CRC_TABLE: [u32; 256] = crc_table();

fn checksum(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in bytes {
        crc = (crc >> 8) ^ CRC_TABLE[((crc as u8) ^ byte) as usize];
    }
    !crc
}

fn seal(page: &mut [u8; PAGE_SIZE]) {
    let crc = checksum(&page[..PAGE_SIZE - 4]);
    page[PAGE_SIZE - 4..].copy_from_slice(&crc.to_le_bytes());
}

fn valid_checksum(page: &[u8; PAGE_SIZE]) -> bool {
    checksum(&page[..PAGE_SIZE - 4]).to_le_bytes() == page[PAGE_SIZE - 4..]
}

struct Input<'a>(&'a [u8]);

impl<'a> Input<'a> {
    fn take(&mut self, length: usize) -> Result<&'a [u8]> {
        if length > self.0.len() {
            return Err(Error::Corrupt("truncated page payload"));
        }
        let (bytes, rest) = self.0.split_at(length);
        self.0 = rest;
        Ok(bytes)
    }

    fn u16(&mut self) -> Result<u16> {
        Ok(u16::from_le_bytes(self.take(2)?.try_into().unwrap()))
    }

    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }

    fn u64(&mut self) -> Result<u64> {
        Ok(u64::from_le_bytes(self.take(8)?.try_into().unwrap()))
    }
}
