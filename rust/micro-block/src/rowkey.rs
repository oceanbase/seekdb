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

use core::cmp::Ordering;

use crate::format::checked_slice;
use crate::read::ProjectionCell;
use crate::{Cell, Error, Result};

/// The decoded key columns of one candidate row. Variable-length cells
/// borrow either the block or the scratch bytes for the current comparison.
pub struct RowkeyCells<'a> {
    pub(crate) cells: Vec<ProjectionCell<'a>>,
    pub(crate) decoded: Vec<u8>,
}

impl<'a> RowkeyCells<'a> {
    pub(crate) fn new(column_count: usize) -> Result<Self> {
        let mut cells = Vec::new();
        cells
            .try_reserve_exact(column_count)
            .map_err(|_| Error::SizeOverflow)?;
        Ok(Self {
            cells,
            decoded: Vec::new(),
        })
    }

    pub fn len(&self) -> usize {
        self.cells.len()
    }

    pub fn is_empty(&self) -> bool {
        self.cells.is_empty()
    }

    pub fn get(&self, column: usize) -> Result<Cell<'_>> {
        match self.cells.get(column).ok_or(Error::InvalidArgument)? {
            ProjectionCell::Borrowed(cell) => Ok(*cell),
            ProjectionCell::Decoded { offset, length } => {
                Ok(Cell::Bytes(checked_slice(&self.decoded, *offset, *length)?))
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RowkeyKind {
    Int,
    UInt,
    UnicodeCi,
}

#[derive(Clone, Copy, Debug)]
pub enum RowkeyValue<'a> {
    Int(i64),
    UInt(u64),
    Bytes(&'a [u8]),
}

/// Borrowed UCA weight pages. The implementation owns page lookup, while the
/// row-key comparator owns UTF-8 decoding and comparison.
pub trait UcaTable {
    fn max_char(&self) -> u32;
    fn page_length(&self, page: usize) -> Option<usize>;
    fn page(&self, page: usize) -> Option<&[u16]>;
}

pub(crate) fn find_bound(
    keys: &[RowkeyValue<'_>],
    kinds: &[RowkeyKind],
    lower_bound: bool,
    range: crate::RowRange,
    column_count: usize,
    mut compare: impl FnMut(usize, usize, RowkeyValue<'_>, RowkeyKind) -> Result<Ordering>,
) -> Result<crate::RowkeyBound> {
    if keys.is_empty() || keys.len() != kinds.len() || keys.len() > column_count {
        return Err(Error::InvalidArgument);
    }
    search_bound(lower_bound, range, |row| {
        for (column, (&query, &kind)) in keys.iter().zip(kinds).enumerate() {
            let ordering = compare(row, column, query, kind)?;
            if ordering != Ordering::Equal {
                return Ok(ordering);
            }
        }
        Ok(Ordering::Equal)
    })
}

/// Search a validated range. The comparator orders the query against the
/// stored row and may fail while decoding or comparing a candidate.
pub(crate) fn search_bound<E>(
    lower_bound: bool,
    range: crate::RowRange,
    mut compare: impl FnMut(usize) -> core::result::Result<Ordering, E>,
) -> core::result::Result<crate::RowkeyBound, E> {
    let mut low = range.start;
    let mut high = range.end;
    let mut equal = false;
    while low < high {
        let middle = low + (high - low) / 2;
        match compare(middle)? {
            Ordering::Less => high = middle,
            Ordering::Greater => low = middle + 1,
            Ordering::Equal => {
                equal = true;
                if lower_bound {
                    high = middle;
                } else {
                    low = middle + 1;
                }
            }
        }
    }
    Ok(crate::RowkeyBound { row: low, equal })
}

pub(crate) fn compare_cell(
    query: RowkeyValue<'_>,
    stored: Cell<'_>,
    kind: RowkeyKind,
    uca: &dyn UcaTable,
) -> Result<Ordering> {
    match (query, kind) {
        (RowkeyValue::Int(query), RowkeyKind::Int) => Ok(query.cmp(&stored.as_i64()?)),
        (RowkeyValue::UInt(query), RowkeyKind::UInt) => Ok(query.cmp(&stored.as_u64()?)),
        (RowkeyValue::Bytes(query), RowkeyKind::UnicodeCi) => {
            compare_unicode_ci(query, stored.bytes()?, uca)
        }
        _ => Err(Error::InvalidArgument),
    }
}

pub fn compare_unicode_ci(left: &[u8], right: &[u8], table: &dyn UcaTable) -> Result<Ordering> {
    let space = table
        .page(0)
        .and_then(|page| {
            let length = table.page_length(0)?;
            page.get(0x20 * length).copied()
        })
        .ok_or(Error::InvalidArgument)?;
    let mut left_scanner = UcaScanner::new(left, table)?;
    let mut right_scanner = UcaScanner::new(right, table)?;
    loop {
        let left_weight = left_scanner.next();
        let right_weight = right_scanner.next();
        match (left_weight, right_weight) {
            (Some(left), Some(right)) => match left.cmp(&right) {
                Ordering::Equal => {}
                ordering => return Ok(ordering),
            },
            (None, None) => return Ok(Ordering::Equal),
            (Some(mut left), None) => {
                while left == space {
                    match left_scanner.next() {
                        Some(next) => left = next,
                        None => return Ok(Ordering::Equal),
                    }
                }
                return Ok(left.cmp(&space));
            }
            (None, Some(mut right)) => {
                while right == space {
                    match right_scanner.next() {
                        Some(next) => right = next,
                        None => return Ok(Ordering::Equal),
                    }
                }
                return Ok(space.cmp(&right));
            }
        }
    }
}

struct UcaScanner<'a> {
    input: &'a [u8],
    position: usize,
    table: &'a dyn UcaTable,
    page: Option<usize>,
    weight_index: usize,
    implicit: [u16; 2],
}

impl<'a> UcaScanner<'a> {
    fn new(input: &'a [u8], table: &'a dyn UcaTable) -> Result<Self> {
        if table.page_length(0).unwrap_or(0) == 0 {
            return Err(Error::InvalidArgument);
        }
        Ok(Self {
            input,
            position: 0,
            table,
            page: None,
            weight_index: 0,
            implicit: [0; 2],
        })
    }

    fn next(&mut self) -> Option<u16> {
        loop {
            if let Some(weight) = self.pending_weight() {
                if weight != 0 {
                    return Some(weight);
                }
            }
            let (character, length) = decode_utf8(&self.input[self.position..])?;
            self.position += length;
            if character > self.table.max_char() {
                return Some(0xfffd);
            }
            let page = character as usize >> 8;
            let code = character as usize & 0xff;
            if let Some(length) = self.table.page_length(page) {
                if length != 0 && self.table.page(page).is_some() {
                    self.page = Some(page);
                    self.weight_index = code * length;
                    continue;
                }
            }
            self.implicit = [(character & 0x7fff) as u16 | 0x8000, 0];
            self.weight_index = 0;
            self.page = None;
            let mut implicit_page = character >> 15;
            if (0x3400..=0x4db5).contains(&character) {
                implicit_page += 0xfb80;
            } else if (0x4e00..=0x9fa5).contains(&character) {
                implicit_page += 0xfb40;
            } else {
                implicit_page += 0xfbc0;
            }
            return u16::try_from(implicit_page).ok();
        }
    }

    fn pending_weight(&mut self) -> Option<u16> {
        match self.page {
            Some(page) => {
                let weight = self.table.page(page)?.get(self.weight_index).copied()?;
                self.weight_index += 1;
                Some(weight)
            }
            None => {
                let weight = self.implicit.get(self.weight_index).copied()?;
                self.weight_index += 1;
                Some(weight)
            }
        }
    }
}

fn decode_utf8(input: &[u8]) -> Option<(u32, usize)> {
    let first = *input.first()?;
    if first < 0x80 {
        return Some((u32::from(first), 1));
    }
    let width = match first {
        0xc2..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf4 => 4,
        _ => return None,
    };
    let bytes = input.get(..width)?;
    if !bytes[1..].iter().all(|byte| byte & 0xc0 == 0x80) {
        return None;
    }
    let character = match width {
        2 => (u32::from(first & 0x1f) << 6) | u32::from(bytes[1] & 0x3f),
        3 => {
            (u32::from(first & 0x0f) << 12)
                | (u32::from(bytes[1] & 0x3f) << 6)
                | u32::from(bytes[2] & 0x3f)
        }
        4 => {
            (u32::from(first & 0x07) << 18)
                | (u32::from(bytes[1] & 0x3f) << 12)
                | (u32::from(bytes[2] & 0x3f) << 6)
                | u32::from(bytes[3] & 0x3f)
        }
        _ => return None,
    };
    if (width == 3 && character < 0x800)
        || (width == 4 && !(0x10000..=0x10ffff).contains(&character))
    {
        None
    } else {
        Some((character, width))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{Block, BlockWriter, ColumnSpec, OwnedDatum, RowRange, WriterConfig};

    struct TestUca {
        lengths: [u8; 256],
        zero_page: Vec<u16>,
    }

    impl TestUca {
        fn new() -> Self {
            let mut lengths = [0; 256];
            lengths[0] = 4;
            let mut zero_page = vec![0; 1024];
            zero_page[0x20 * 4] = 1;
            for character in 0x30..=0x39 {
                zero_page[character * 4] = character as u16 + 1;
            }
            for character in 0x41..=0x5a {
                zero_page[character * 4] = character as u16 + 1;
                zero_page[(character + 0x20) * 4] = character as u16 + 1;
            }
            Self { lengths, zero_page }
        }
    }

    impl UcaTable for TestUca {
        fn max_char(&self) -> u32 {
            0xffff
        }

        fn page_length(&self, page: usize) -> Option<usize> {
            self.lengths.get(page).copied().map(usize::from)
        }

        fn page(&self, page: usize) -> Option<&[u16]> {
            (page == 0).then_some(&self.zero_page)
        }
    }

    #[test]
    fn unicode_ci_matches_case_and_trailing_space_semantics() {
        let table = TestUca::new();
        assert_eq!(
            Ordering::Equal,
            compare_unicode_ci("Micro-Proof".as_bytes(), b"micro-proof  ", &table).unwrap()
        );
        assert_eq!(
            Ordering::Less,
            compare_unicode_ci(b"alpha", b"beta", &table).unwrap()
        );
        assert_eq!(
            Ordering::Greater,
            compare_unicode_ci(b"beta", b"alpha ", &table).unwrap()
        );
    }

    #[test]
    fn unicode_ci_handles_implicit_weights_and_invalid_utf8() {
        let table = TestUca::new();
        assert_eq!(
            Ordering::Equal,
            compare_unicode_ci("中".as_bytes(), "中".as_bytes(), &table).unwrap()
        );
        assert_eq!(
            Ordering::Equal,
            compare_unicode_ci(b"abc\xff", b"abc", &table).unwrap()
        );
    }

    #[test]
    fn locates_compound_integer_and_unicode_ci_keys() {
        let table = TestUca::new();
        let mut writer = BlockWriter::new(WriterConfig::new(
            vec![ColumnSpec::fixed(5, 8), ColumnSpec::variable(22)],
            2,
        ))
        .unwrap();
        for (id, key) in [(1i64, "Alpha"), (1, "beta"), (2, "a"), (3, "中")] {
            writer
                .append_row(&[
                    OwnedDatum::Bytes(id.to_le_bytes().to_vec()),
                    OwnedDatum::Bytes(key.as_bytes().to_vec()),
                ])
                .unwrap();
        }
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        let kinds = [RowkeyKind::Int, RowkeyKind::UnicodeCi];

        assert_eq!(
            Some(1),
            block
                .find_rowkey(
                    &[RowkeyValue::Int(1), RowkeyValue::Bytes(b"BETA  ")],
                    &kinds,
                    &table,
                )
                .unwrap()
        );
        assert_eq!(
            Some(2),
            block
                .find_rowkey(
                    &[RowkeyValue::Int(2), RowkeyValue::Bytes(b"A")],
                    &kinds,
                    &table,
                )
                .unwrap()
        );
        assert_eq!(
            Some(3),
            block
                .find_rowkey(
                    &[RowkeyValue::Int(3), RowkeyValue::Bytes("中".as_bytes())],
                    &kinds,
                    &table,
                )
                .unwrap()
        );
        assert_eq!(
            None,
            block
                .find_rowkey(
                    &[RowkeyValue::Int(2), RowkeyValue::Bytes(b"missing")],
                    &kinds,
                    &table,
                )
                .unwrap()
        );
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(1), RowkeyValue::Bytes(b"BETA  ")],
                &kinds,
                &table,
                true,
                RowRange::new(0, block.row_count()).unwrap(),
            )
            .unwrap();
        assert_eq!((1, true), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(1)],
                &kinds[..1],
                &table,
                false,
                RowRange::new(0, block.row_count()).unwrap(),
            )
            .unwrap();
        assert_eq!((2, true), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(2)],
                &kinds[..1],
                &table,
                true,
                RowRange::new(0, block.row_count()).unwrap(),
            )
            .unwrap();
        assert_eq!((2, true), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(4)],
                &kinds[..1],
                &table,
                true,
                RowRange::new(0, block.row_count()).unwrap(),
            )
            .unwrap();
        assert_eq!((4, false), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(3)],
                &kinds[..1],
                &table,
                false,
                RowRange::new(0, block.row_count()).unwrap(),
            )
            .unwrap();
        assert_eq!((4, true), (bound.row, bound.equal));
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_FLAT_FIXTURE_DIR with captured C++ blocks"]
    fn locates_flat_compound_and_prefix_keys() {
        let directory =
            std::path::PathBuf::from(std::env::var_os("MICRO_BLOCK_FLAT_FIXTURE_DIR").unwrap());
        let bytes = std::fs::read(directory.join("flat0.bin")).unwrap();
        let block = crate::FlatBlock::new(&bytes).unwrap();
        let table = TestUca::new();
        let kinds = [RowkeyKind::Int, RowkeyKind::UnicodeCi];
        let range = RowRange::new(0, block.row_count()).unwrap();

        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(-2), RowkeyValue::Bytes(b"alpha  ")],
                &kinds,
                &table,
                true,
                range,
            )
            .unwrap();
        assert_eq!((0, true), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Int(1), RowkeyValue::Bytes("中".as_bytes())],
                &kinds,
                &table,
                true,
                range,
            )
            .unwrap();
        assert_eq!((2, true), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(&[RowkeyValue::Int(10)], &kinds[..1], &table, false, range)
            .unwrap();
        assert_eq!((5, true), (bound.row, bound.equal));
        let bound = block
            .find_rowkey_bound(&[RowkeyValue::Int(3)], &kinds[..1], &table, true, range)
            .unwrap();
        assert_eq!((4, false), (bound.row, bound.equal));
    }

    #[test]
    fn locates_prefix_encoded_unicode_keys() {
        let table = TestUca::new();
        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 1)).unwrap();
        for row in 0..128u32 {
            writer
                .append_row(&[OwnedDatum::Bytes(if row == 127 {
                    "f".repeat(32).into_bytes()
                } else {
                    format!("{row:032x}").into_bytes()
                })])
                .unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_eq!(bytes[header.header_size + 1], 7);
        let block = Block::new(&bytes).unwrap();
        for row in [0u32, 1, 63, 127] {
            let key = if row == 127 {
                "f".repeat(32)
            } else {
                format!("{row:032x}")
            };
            assert_eq!(
                Some(row),
                block
                    .find_rowkey(
                        &[RowkeyValue::Bytes(key.as_bytes())],
                        &[RowkeyKind::UnicodeCi],
                        &table,
                    )
                    .unwrap()
            );
        }
        let missing = "g".repeat(32);
        let bound = block
            .find_rowkey_bound(
                &[RowkeyValue::Bytes(missing.as_bytes())],
                &[RowkeyKind::UnicodeCi],
                &table,
                true,
                RowRange::new(0, block.row_count()).unwrap(),
            )
            .unwrap();
        assert_eq!(bound.row, 128);
        assert!(!bound.equal);
    }
}
