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

use crate::checksum::crc32c;
use crate::format::{
    header_checksum, integer_store_size, is_signed_store_type, write_u16, write_u32, write_u64,
    ColumnHeader, Header, ALL_LOB_IN_ROW, COLUMN_BIT_PACKING, COLUMN_FIX_LENGTH,
    COLUMN_HAS_EXTEND_VALUE, COLUMN_HEADER_SIZE, COLUMN_LAST_VAR_FIELD, ENCODING_ROW_STORE,
    FIXED_HEADER_SIZE, HAS_COLUMN_CHECKSUM, MICRO_BLOCK_MAGIC, MICRO_BLOCK_VERSION,
    SELECTIVE_ENCODING_ROW_STORE,
};
use crate::{Error, Result};

const RAW: u8 = 0;
const DICT: u8 = 1;
const RLE: u8 = 2;
const CONST: u8 = 3;
const INTEGER_BASE_DIFF: u8 = 4;
const STRING_DIFF: u8 = 5;
const HEX_PACKING: u8 = 6;
const STRING_PREFIX: u8 = 7;
const COLUMN_EQUAL: u8 = 8;
const COLUMN_SUBSTR: u8 = 9;
const DICT_META_HEADER_SIZE: usize = 9;
const RLE_META_HEADER_SIZE: usize = 10;
const DICT_FIX_LENGTH: u8 = 1;
const HEX_META_HEADER_SIZE: usize = 13;
const STRING_DIFF_META_HEADER_SIZE: usize = 13;
const STRING_PREFIX_META_HEADER_SIZE: usize = 15;
const COLUMN_EQUAL_META_HEADER_SIZE: usize = 3;
const COLUMN_SUBSTR_META_HEADER_SIZE: usize = 8;
const EXCEPTION_META_HEADER_SIZE: usize = 4;

#[cfg(test)]
thread_local! {
    static FAIL_NEXT_ROW_RESERVE: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum OwnedDatum {
    Null,
    Nop,
    Bytes(Vec<u8>),
    UInt(u64),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ColumnSpec {
    pub obj_type: u8,
    /// `None` selects row-local variable-length storage. Integer values require
    /// a fixed width so their original datum representation stays unambiguous.
    pub fixed_length: Option<usize>,
    pub enable_bit_packing: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum RowStoreType {
    Encoding = ENCODING_ROW_STORE,
    Selective = SELECTIVE_ENCODING_ROW_STORE,
}

impl TryFrom<u8> for RowStoreType {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        match value {
            ENCODING_ROW_STORE => Ok(Self::Encoding),
            SELECTIVE_ENCODING_ROW_STORE => Ok(Self::Selective),
            _ => Err(Error::Unsupported),
        }
    }
}

impl ColumnSpec {
    pub fn fixed(obj_type: u8, length: usize) -> Self {
        Self {
            obj_type,
            fixed_length: Some(length),
            enable_bit_packing: false,
        }
    }

    pub fn variable(obj_type: u8) -> Self {
        Self {
            obj_type,
            fixed_length: None,
            enable_bit_packing: false,
        }
    }
}

#[derive(Clone, Debug)]
pub struct WriterConfig {
    pub columns: Vec<ColumnSpec>,
    pub rowkey_column_count: usize,
    pub row_store_type: RowStoreType,
    pub block_size_limit: usize,
    pub need_column_checksums: bool,
    pub all_lob_in_row: bool,
    pub max_merged_trans_version: i64,
    pub original_size_override: Option<usize>,
}

impl WriterConfig {
    pub fn new(columns: Vec<ColumnSpec>, rowkey_column_count: usize) -> Self {
        Self {
            columns,
            rowkey_column_count,
            row_store_type: RowStoreType::Encoding,
            block_size_limit: 2 * 1024 * 1024,
            need_column_checksums: false,
            all_lob_in_row: true,
            max_merged_trans_version: 0,
            original_size_override: None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AppendOutcome {
    pub accepted_rows: usize,
    pub is_full: bool,
}

#[derive(Clone, Debug)]
enum Plan {
    Const {
        reference: u8,
    },
    Fixed {
        has_extend: bool,
        bit_width: Option<usize>,
    },
    IntegerBaseDiff {
        base: u64,
        has_extend: bool,
        width: usize,
        bit_packed: bool,
    },
    Dict(DictPlan),
    Rle(RlePlan),
    Hex(HexPlan),
    StringDiff(StringDiffPlan),
    StringPrefix(StringPrefixPlan),
    ColumnEqual(ColumnEqualPlan),
    ColumnSubstr(ColumnSubstrPlan),
    Variable {
        ordinal: usize,
        extend_offset: usize,
        has_extend: bool,
    },
}

#[derive(Clone, Debug)]
struct StringPrefixPlan {
    prefix: Vec<u8>,
    alphabet: Vec<u8>,
    indexes: [u8; 256],
    max_string_size: usize,
    value_count: usize,
    packed_size: usize,
    ordinal: usize,
    extend_offset: usize,
    has_extend: bool,
}

impl StringPrefixPlan {
    fn meta_size(&self) -> usize {
        STRING_PREFIX_META_HEADER_SIZE + self.alphabet.len() + self.prefix.len()
    }

    fn encoded_size(&self, row_count: usize, extend_width: usize) -> Result<usize> {
        let extend_size = if self.has_extend {
            (row_count * extend_width).div_ceil(8)
        } else {
            0
        };
        self.meta_size()
            .checked_add(self.value_count * 3)
            .and_then(|size| size.checked_add(self.packed_size))
            .and_then(|size| size.checked_add(row_count * 2))
            .and_then(|size| size.checked_add(extend_size))
            .ok_or(Error::SizeOverflow)
    }
}

#[derive(Clone, Debug)]
struct ColumnEqualPlan {
    ref_col_idx: usize,
    exceptions: Vec<u8>,
}

impl ColumnEqualPlan {
    fn encoded_size(&self) -> usize {
        COLUMN_EQUAL_META_HEADER_SIZE + self.exceptions.len()
    }
}

#[derive(Clone, Debug)]
struct ColumnSubstrPlan {
    ref_col_idx: usize,
    exceptions: Vec<u8>,
    same_start: Option<u16>,
    fixed_length: Option<u16>,
    start_width: usize,
    length_width: usize,
    starts: Vec<u16>,
    lengths: Vec<u16>,
}

impl ColumnSubstrPlan {
    fn encoded_size(&self, row_count: usize) -> Result<usize> {
        COLUMN_SUBSTR_META_HEADER_SIZE
            .checked_add(self.exceptions.len())
            .and_then(|size| size.checked_add(row_count * (self.start_width + self.length_width)))
            .ok_or(Error::SizeOverflow)
    }
}

#[derive(Clone, Debug)]
struct RlePlan {
    dict: DictPlan,
    row_ids: Vec<u32>,
    refs: Vec<u32>,
    row_id_width: usize,
    ref_width: usize,
}

impl RlePlan {
    fn encoded_size(&self) -> Result<usize> {
        self.dict
            .meta_size()?
            .checked_add(RLE_META_HEADER_SIZE)
            .and_then(|size| {
                self.row_ids
                    .len()
                    .checked_mul(self.row_id_width + self.ref_width)
                    .and_then(|runs| size.checked_add(runs))
            })
            .ok_or(Error::SizeOverflow)
    }
}

#[derive(Clone, Debug)]
struct HexPlan {
    indexes: [u8; 256],
    alphabet: Vec<u8>,
    max_string_size: usize,
    fixed_width: Option<usize>,
    packed_size: usize,
    value_count: usize,
    ordinal: usize,
    extend_offset: usize,
    has_extend: bool,
}

impl HexPlan {
    fn meta_size(&self) -> usize {
        HEX_META_HEADER_SIZE + self.alphabet.len()
    }

    fn encoded_size(&self, row_count: usize, extend_width: usize) -> Result<usize> {
        let extend_size = if self.has_extend {
            (row_count * extend_width).div_ceil(8)
        } else {
            0
        };
        let data_size = match self.fixed_width {
            Some(width) => row_count
                .checked_mul(width)
                .and_then(|size| size.checked_add(extend_size)),
            None => self
                .packed_size
                .checked_add(self.value_count)
                .and_then(|size| size.checked_add(row_count * 2))
                .and_then(|size| size.checked_add(extend_size)),
        }
        .ok_or(Error::SizeOverflow)?;
        self.meta_size()
            .checked_add(data_size)
            .ok_or(Error::SizeOverflow)
    }
}

#[derive(Clone, Debug)]
struct StringDiffPlan {
    descriptors: Vec<u8>,
    alphabet: Vec<u8>,
    indexes: [u8; 256],
    common: Vec<u8>,
    string_size: usize,
    diff_size: usize,
    value_count: usize,
    variable: bool,
    ordinal: usize,
    extend_offset: usize,
    has_extend: bool,
}

impl StringDiffPlan {
    fn packed_width(&self) -> usize {
        if self.alphabet.is_empty() {
            self.diff_size
        } else {
            self.diff_size.div_ceil(2)
        }
    }

    fn meta_size(&self) -> Result<usize> {
        STRING_DIFF_META_HEADER_SIZE
            .checked_add(self.descriptors.len())
            .and_then(|size| size.checked_add(self.alphabet.len()))
            .and_then(|size| size.checked_add(self.common.len()))
            .ok_or(Error::SizeOverflow)
    }

    fn encoded_size(&self, row_count: usize, extend_width: usize) -> Result<usize> {
        let extend_size = if self.has_extend {
            (row_count * extend_width).div_ceil(8)
        } else {
            0
        };
        let data_size = if self.variable {
            row_count.checked_mul(2).and_then(|size| {
                self.value_count
                    .checked_mul(self.packed_width())
                    .and_then(|values| size.checked_add(values))
            })
        } else {
            row_count.checked_mul(self.packed_width())
        }
        .and_then(|size| size.checked_add(extend_size))
        .ok_or(Error::SizeOverflow)?;
        self.meta_size()?
            .checked_add(data_size)
            .ok_or(Error::SizeOverflow)
    }
}

impl Plan {
    fn is_variable(&self) -> bool {
        matches!(self, Self::Variable { .. })
            || matches!(self, Self::Hex(hex) if hex.fixed_width.is_none())
            || matches!(self, Self::StringDiff(diff) if diff.variable)
            || matches!(self, Self::StringPrefix(_))
    }
}

#[derive(Clone, Debug)]
struct DictPlan {
    entries: Vec<Vec<u8>>,
    refs: Vec<u32>,
    entry_width: Option<usize>,
    index_width: usize,
    ref_width: usize,
    bit_packed: bool,
}

impl DictPlan {
    fn meta_size(&self) -> Result<usize> {
        let payload = match self.entry_width {
            Some(width) => width
                .checked_mul(self.entries.len())
                .ok_or(Error::SizeOverflow)?,
            None => {
                let values = self.entries.iter().try_fold(0usize, |size, entry| {
                    size.checked_add(entry.len()).ok_or(Error::SizeOverflow)
                })?;
                self.entries
                    .len()
                    .saturating_sub(1)
                    .checked_mul(self.index_width)
                    .and_then(|size| size.checked_add(values))
                    .ok_or(Error::SizeOverflow)?
            }
        };
        DICT_META_HEADER_SIZE
            .checked_add(payload)
            .ok_or(Error::SizeOverflow)
    }

    fn data_size(&self) -> Result<usize> {
        if self.bit_packed {
            self.refs
                .len()
                .checked_mul(self.ref_width)
                .and_then(|bits| bits.checked_add(7))
                .map(|bits| bits / 8)
                .ok_or(Error::SizeOverflow)
        } else {
            self.refs
                .len()
                .checked_mul(self.ref_width)
                .ok_or(Error::SizeOverflow)
        }
    }

    fn encoded_size(&self) -> Result<usize> {
        self.meta_size()?
            .checked_add(self.data_size()?)
            .ok_or(Error::SizeOverflow)
    }
}

/// Owns accepted rows and all encoding decisions for one block. Appending a
/// row is transactional: validation and capacity checks precede mutation.
pub struct BlockWriter {
    config: WriterConfig,
    rows: Vec<Vec<OwnedDatum>>,
    original_size: usize,
    frozen: bool,
}

impl BlockWriter {
    pub fn new(config: WriterConfig) -> Result<Self> {
        if config.columns.is_empty()
            || config.columns.len() > u16::MAX as usize
            || config.rowkey_column_count > config.columns.len()
        {
            return Err(Error::InvalidArgument);
        }
        for column in &config.columns {
            if let Some(length) = column.fixed_length {
                if length == 0 || length > u32::MAX as usize {
                    return Err(Error::InvalidArgument);
                }
            }
        }
        Ok(Self {
            config,
            rows: Vec::new(),
            original_size: 0,
            frozen: false,
        })
    }

    pub fn row_count(&self) -> usize {
        self.rows.len()
    }

    pub fn append_row(&mut self, row: &[OwnedDatum]) -> Result<AppendOutcome> {
        if self.frozen {
            return Err(Error::Frozen);
        }
        if row.len() != self.config.columns.len() || self.rows.len() >= u32::MAX as usize {
            return Err(Error::InvalidArgument);
        }
        let mut row_size = 0usize;
        for (datum, column) in row.iter().zip(&self.config.columns) {
            self.validate_datum(datum, column)?;
            let datum_size = datum_len(datum, column)?;
            row_size = row_size
                .checked_add(datum_size)
                .ok_or(Error::SizeOverflow)?;
            if column.fixed_length.is_none() {
                row_size = row_size.checked_add(8).ok_or(Error::SizeOverflow)?;
            }
        }
        let header_size = self.total_header_size()?;
        let would_exceed = header_size
            .checked_add(self.original_size)
            .and_then(|size| size.checked_add(row_size))
            .ok_or(Error::SizeOverflow)?
            > self.config.block_size_limit;
        if !self.rows.is_empty() && would_exceed {
            return Ok(AppendOutcome {
                accepted_rows: self.rows.len(),
                is_full: true,
            });
        }
        #[cfg(test)]
        if FAIL_NEXT_ROW_RESERVE.with(|fail| fail.replace(false)) {
            return Err(Error::SizeOverflow);
        }
        self.rows.try_reserve(1).map_err(|_| Error::SizeOverflow)?;
        self.rows.push(row.to_vec());
        self.original_size = self
            .original_size
            .checked_add(row_size)
            .ok_or(Error::SizeOverflow)?;
        Ok(AppendOutcome {
            accepted_rows: self.rows.len(),
            is_full: false,
        })
    }

    fn validate_datum(&self, datum: &OwnedDatum, column: &ColumnSpec) -> Result<()> {
        match (datum, column.fixed_length) {
            (OwnedDatum::Null | OwnedDatum::Nop, _) => Ok(()),
            (OwnedDatum::Bytes(bytes), Some(length)) if bytes.len() <= length => Ok(()),
            (OwnedDatum::Bytes(_), None) => Ok(()),
            (OwnedDatum::UInt(value), Some(length @ (1 | 2 | 4))) => {
                if *value < (1u64 << (length * 8)) {
                    Ok(())
                } else {
                    Err(Error::InvalidArgument)
                }
            }
            (OwnedDatum::UInt(_), Some(8)) => Ok(()),
            _ => Err(Error::InvalidArgument),
        }
    }

    fn total_header_size(&self) -> Result<usize> {
        let checksum_bytes = if self.config.need_column_checksums {
            self.config
                .columns
                .len()
                .checked_mul(8)
                .ok_or(Error::SizeOverflow)?
        } else {
            0
        };
        FIXED_HEADER_SIZE
            .checked_add(checksum_bytes)
            .and_then(|size| size.checked_add(self.config.columns.len() * COLUMN_HEADER_SIZE))
            .ok_or(Error::SizeOverflow)
    }

    pub fn finish(&mut self) -> Result<Vec<u8>> {
        self.finish_inner(None)
    }

    pub fn finish_with_column_checksums(&mut self, column_checksums: &[u64]) -> Result<Vec<u8>> {
        self.finish_inner(Some(column_checksums))
    }

    fn finish_inner(&mut self, column_checksums: Option<&[u64]>) -> Result<Vec<u8>> {
        if self.frozen {
            return Err(Error::Frozen);
        }
        if self.rows.is_empty()
            || column_checksums.is_some_and(|checksums| {
                (self.config.need_column_checksums && checksums.len() != self.config.columns.len())
                    || (!self.config.need_column_checksums && !checksums.is_empty())
            })
        {
            return Err(Error::InvalidArgument);
        }
        let extend_width = self.extend_width();
        let plans = self.plan_columns(extend_width)?;
        let var_column_count = plans.iter().filter(|plan| plan.is_variable()).count();
        let row_extend_bytes = plans
            .iter()
            .map(|plan| match plan {
                Plan::Variable { has_extend, .. } if *has_extend => extend_width,
                Plan::Hex(hex) if hex.fixed_width.is_none() && hex.has_extend => extend_width,
                Plan::StringDiff(diff) if diff.variable && diff.has_extend => extend_width,
                Plan::StringPrefix(prefix) if prefix.has_extend => extend_width,
                _ => 0,
            })
            .sum::<usize>()
            .div_ceil(8);
        let (meta, columns) = self.build_meta(&plans, extend_width, row_extend_bytes)?;
        let (row_data, row_index_byte) =
            self.build_row_data(&plans, extend_width, row_extend_bytes)?;
        self.assemble(
            meta,
            columns,
            row_data,
            row_index_byte,
            var_column_count,
            extend_width,
            column_checksums,
        )
    }

    fn extend_width(&self) -> usize {
        let mut has_null = false;
        let mut has_nop = false;
        for column in 0..self.config.columns.len() {
            if self.is_const(column) {
                continue;
            }
            for row in &self.rows {
                match row[column] {
                    OwnedDatum::Null => has_null = true,
                    OwnedDatum::Nop => has_nop = true,
                    _ => {}
                }
            }
        }
        if has_nop {
            2
        } else if has_null {
            1
        } else {
            0
        }
    }

    fn is_const(&self, column: usize) -> bool {
        self.rows
            .iter()
            .all(|row| row[column] == self.rows[0][column])
    }

    fn plan_columns(&self, extend_width: usize) -> Result<Vec<Plan>> {
        let mut plans = Vec::new();
        plans
            .try_reserve_exact(self.config.columns.len())
            .map_err(|_| Error::SizeOverflow)?;
        let mut var_ordinal = 0usize;
        let mut var_extend_offset = 0usize;
        for (column_id, spec) in self.config.columns.iter().enumerate() {
            if self.is_const(column_id) {
                let reference = match self.rows[0][column_id] {
                    OwnedDatum::Null => 1,
                    OwnedDatum::Nop => 2,
                    _ => 0,
                };
                plans.push(Plan::Const { reference });
                continue;
            }
            let has_extend = self
                .rows
                .iter()
                .any(|row| matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop));
            let (mut best, mut best_size) = if let Some(fixed_length) = spec.fixed_length {
                let bit_width = if spec.enable_bit_packing {
                    integer_store_size(spec.obj_type)
                        .map(|_| self.raw_integer_bit_width(column_id, spec))
                        .transpose()?
                } else {
                    None
                };
                let raw = Plan::Fixed {
                    has_extend,
                    bit_width,
                };
                let mut best_size = fixed_data_size(
                    self.rows.len(),
                    extend_width,
                    has_extend,
                    bit_width.unwrap_or(fixed_length),
                    bit_width.is_some(),
                )?;
                let mut best = raw;
                if let Some(diff) = self.integer_diff_plan(column_id, spec, extend_width)? {
                    let (width, bit_packed) = match &diff {
                        Plan::IntegerBaseDiff {
                            width, bit_packed, ..
                        } => (*width, *bit_packed),
                        _ => return Err(Error::InvalidData),
                    };
                    let size = integer_diff_size(
                        self.rows.len(),
                        fixed_length,
                        extend_width,
                        has_extend,
                        width,
                        bit_packed,
                    )?;
                    if size < best_size {
                        best = diff;
                        best_size = size;
                    }
                }
                if let Some(dict) = self.dict_plan(column_id, spec)? {
                    let dict_size = dict.encoded_size()?;
                    if let Some(rle) = self.rle_plan(&dict, best_size.min(dict_size))? {
                        best_size = rle.encoded_size()?;
                        best = Plan::Rle(rle);
                    } else if dict_size < best_size {
                        best_size = dict_size;
                        best = Plan::Dict(dict);
                    }
                }
                if let Some(mut diff) = self.string_diff_plan(column_id, spec)? {
                    let size = diff.encoded_size(self.rows.len(), extend_width)?;
                    if size < best_size {
                        if diff.variable {
                            diff.ordinal = var_ordinal;
                            diff.extend_offset = var_extend_offset;
                        }
                        best_size = size;
                        best = Plan::StringDiff(diff);
                    }
                }
                (best, best_size)
            } else {
                let raw_size = self.rows.iter().try_fold(0usize, |size, row| {
                    size.checked_add(datum_len(&row[column_id], spec)?)
                        .ok_or(Error::SizeOverflow)
                })?;
                let raw_size = raw_size
                    .checked_add(self.rows.len() * 2)
                    .and_then(|size| {
                        size.checked_add(if has_extend {
                            (self.rows.len() * extend_width).div_ceil(8)
                        } else {
                            0
                        })
                    })
                    .ok_or(Error::SizeOverflow)?;
                let extend_offset = var_extend_offset;
                let mut best = Plan::Variable {
                    ordinal: var_ordinal,
                    extend_offset,
                    has_extend,
                };
                let mut best_size = raw_size;
                if let Some(dict) = self.dict_plan(column_id, spec)? {
                    let dict_size = dict.encoded_size()?;
                    if let Some(rle) = self.rle_plan(&dict, best_size.min(dict_size))? {
                        best_size = rle.encoded_size()?;
                        best = Plan::Rle(rle);
                    } else if dict_size < raw_size {
                        best_size = dict.encoded_size()?;
                        best = Plan::Dict(dict);
                    }
                }
                if let Some(mut hex) = self.hex_plan(column_id, spec)? {
                    let hex_size = hex.encoded_size(self.rows.len(), extend_width)?;
                    if hex_size < best_size {
                        if hex.fixed_width.is_none() {
                            hex.ordinal = var_ordinal;
                            hex.extend_offset = extend_offset;
                        }
                        best_size = hex_size;
                        best = Plan::Hex(hex);
                    }
                }
                if let Some(mut diff) = self.string_diff_plan(column_id, spec)? {
                    let size = diff.encoded_size(self.rows.len(), extend_width)?;
                    if size < best_size {
                        if diff.variable {
                            diff.ordinal = var_ordinal;
                            diff.extend_offset = extend_offset;
                        }
                        best_size = size;
                        best = Plan::StringDiff(diff);
                    }
                }
                (best, best_size)
            };
            if let Some(mut prefix) = self.string_prefix_plan(column_id, spec)? {
                let size = prefix.encoded_size(self.rows.len(), extend_width)?;
                if size < best_size {
                    prefix.ordinal = var_ordinal;
                    prefix.extend_offset = var_extend_offset;
                    best_size = size;
                    best = Plan::StringPrefix(prefix);
                }
            }
            if let Some((reference, _)) = self.reference_plan(column_id, best_size)? {
                best = reference;
            }
            if best.is_variable() {
                var_ordinal += 1;
                if has_extend {
                    var_extend_offset += extend_width;
                }
            }
            plans.push(best);
        }
        Ok(plans)
    }

    fn rle_plan(&self, dict: &DictPlan, size_limit: usize) -> Result<Option<RlePlan>> {
        if self.config.row_store_type != RowStoreType::Encoding || dict.refs.len() < 2 {
            return Ok(None);
        }
        let run_count = 1 + dict
            .refs
            .windows(2)
            .filter(|pair| pair[0] != pair[1])
            .count();
        if run_count == dict.refs.len() {
            return Ok(None);
        }
        let mut row_ids = Vec::new();
        let mut refs = Vec::new();
        row_ids
            .try_reserve_exact(run_count)
            .map_err(|_| Error::SizeOverflow)?;
        refs.try_reserve_exact(run_count)
            .map_err(|_| Error::SizeOverflow)?;
        for (row_id, reference) in dict.refs.iter().copied().enumerate() {
            if refs.last().copied() != Some(reference) {
                row_ids.push(row_id as u32);
                refs.push(reference);
            }
        }
        let row_id_width = byte_width(*row_ids.last().unwrap() as u64);
        let ref_width = byte_width(*refs.iter().max().unwrap() as u64);
        let plan = RlePlan {
            dict: dict.clone(),
            row_ids,
            refs,
            row_id_width,
            ref_width,
        };
        if plan.encoded_size()? < size_limit {
            Ok(Some(plan))
        } else {
            Ok(None)
        }
    }

    fn raw_integer_bit_width(&self, column_id: usize, spec: &ColumnSpec) -> Result<usize> {
        let mut bit_width = 1usize;
        for row in &self.rows {
            if let Some(value) = integer_value(&row[column_id], spec)? {
                let value = normalize_integer(
                    value,
                    integer_store_size(spec.obj_type).ok_or(Error::InvalidArgument)?,
                    is_signed_store_type(spec.obj_type),
                );
                let value_width = (64 - value.leading_zeros() as usize).max(1);
                bit_width = bit_width.max(value_width);
            }
        }
        Ok(bit_width)
    }

    fn integer_diff_plan(
        &self,
        column_id: usize,
        spec: &ColumnSpec,
        extend_width: usize,
    ) -> Result<Option<Plan>> {
        let Some(store_size) = integer_store_size(spec.obj_type) else {
            return Ok(None);
        };
        if spec.fixed_length != Some(store_size) {
            return Ok(None);
        }
        let signed = is_signed_store_type(spec.obj_type);
        let mut values =
            self.rows
                .iter()
                .filter_map(|row| match integer_value(&row[column_id], spec) {
                    Ok(value) => value.map(Ok),
                    Err(error) => Some(Err(error)),
                });
        let Some(first) = values.next() else {
            return Ok(None);
        };
        let first = normalize_integer(first?, store_size, signed);
        let mut minimum = first;
        let mut maximum = first;
        for value in values {
            let value = normalize_integer(value?, store_size, signed);
            if signed {
                if (value as i64) < (minimum as i64) {
                    minimum = value;
                }
                if (value as i64) > (maximum as i64) {
                    maximum = value;
                }
            } else {
                minimum = minimum.min(value);
                maximum = maximum.max(value);
            }
        }
        let delta = if signed {
            (maximum as i64).wrapping_sub(minimum as i64) as u64
        } else {
            maximum - minimum
        };
        if delta == 0 {
            return Ok(None);
        }
        let bit_width = (64 - delta.leading_zeros() as usize).max(1);
        let byte_width = byte_width(delta);
        let has_extend = self
            .rows
            .iter()
            .any(|row| matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop));
        let byte_size = integer_diff_size(
            self.rows.len(),
            store_size,
            extend_width,
            has_extend,
            byte_width,
            false,
        )?;
        let use_bits = spec.enable_bit_packing
            && integer_diff_size(
                self.rows.len(),
                store_size,
                extend_width,
                has_extend,
                bit_width,
                true,
            )? < byte_size;
        Ok(Some(Plan::IntegerBaseDiff {
            base: minimum,
            has_extend,
            width: if use_bits { bit_width } else { byte_width },
            bit_packed: use_bits,
        }))
    }

    fn dict_plan(&self, column_id: usize, spec: &ColumnSpec) -> Result<Option<DictPlan>> {
        if spec
            .fixed_length
            .is_some_and(|length| length > u16::MAX as usize)
        {
            return Ok(None);
        }
        let mut entries = Vec::<Vec<u8>>::new();
        let mut refs = Vec::<u32>::new();
        let mut positions = std::collections::HashMap::<Vec<u8>, u32>::new();
        refs.try_reserve_exact(self.rows.len())
            .map_err(|_| Error::SizeOverflow)?;
        for row in &self.rows {
            let count = u32::try_from(entries.len()).map_err(|_| Error::SizeOverflow)?;
            let reference = match &row[column_id] {
                OwnedDatum::Null => u32::MAX,
                OwnedDatum::Nop => u32::MAX - 1,
                datum => {
                    let bytes = canonical_datum_bytes(datum, spec)?;
                    if let Some(reference) = positions.get(&bytes) {
                        *reference
                    } else {
                        entries.try_reserve(1).map_err(|_| Error::SizeOverflow)?;
                        positions.try_reserve(1).map_err(|_| Error::SizeOverflow)?;
                        entries.push(bytes.clone());
                        positions.insert(bytes, count);
                        count
                    }
                }
            };
            refs.push(reference);
        }
        let count = u32::try_from(entries.len()).map_err(|_| Error::SizeOverflow)?;
        let has_null = self
            .rows
            .iter()
            .any(|row| matches!(row[column_id], OwnedDatum::Null));
        let has_nop = self
            .rows
            .iter()
            .any(|row| matches!(row[column_id], OwnedDatum::Nop));
        for reference in &mut refs {
            if has_null && *reference == u32::MAX {
                *reference = count;
            } else if has_nop && *reference == u32::MAX - 1 {
                *reference = count.checked_add(1).ok_or(Error::SizeOverflow)?;
            }
        }
        let max_ref = refs.iter().copied().max().unwrap_or(0) as u64;
        let bit_width = (64 - max_ref.leading_zeros() as usize).max(1);
        let byte_width = byte_width(max_ref);
        let values_size = entries.iter().try_fold(0usize, |size, entry| {
            size.checked_add(entry.len()).ok_or(Error::SizeOverflow)
        })?;
        let index_width = if values_size <= u8::MAX as usize {
            1
        } else if values_size <= u16::MAX as usize {
            2
        } else {
            4
        };
        let mut dict = DictPlan {
            entries,
            refs,
            entry_width: spec.fixed_length,
            index_width,
            ref_width: byte_width,
            bit_packed: false,
        };
        if spec.enable_bit_packing {
            let byte_size = dict.data_size()?;
            dict.ref_width = bit_width;
            dict.bit_packed = true;
            if dict.data_size()? >= byte_size {
                dict.ref_width = byte_width;
                dict.bit_packed = false;
            }
        }
        Ok(Some(dict))
    }

    fn hex_plan(&self, column_id: usize, spec: &ColumnSpec) -> Result<Option<HexPlan>> {
        if self.config.row_store_type != RowStoreType::Encoding
            || spec.fixed_length.is_some()
            || !is_hex_string_type(spec.obj_type)
        {
            return Ok(None);
        }
        let mut indexes = [0u8; 256];
        let mut seen = [false; 256];
        let mut alphabet = Vec::new();
        alphabet
            .try_reserve_exact(16)
            .map_err(|_| Error::SizeOverflow)?;
        let mut min_string_size = usize::MAX;
        let mut max_string_size = 0usize;
        let mut packed_size = 0usize;
        let mut value_count = 0usize;
        let mut null_count = 0usize;
        let mut nop_count = 0usize;
        for row in &self.rows {
            match &row[column_id] {
                OwnedDatum::Null => null_count += 1,
                OwnedDatum::Nop => nop_count += 1,
                OwnedDatum::Bytes(bytes) => {
                    value_count += 1;
                    min_string_size = min_string_size.min(bytes.len());
                    max_string_size = max_string_size.max(bytes.len());
                    packed_size = packed_size
                        .checked_add(bytes.len().div_ceil(2))
                        .ok_or(Error::SizeOverflow)?;
                    for &byte in bytes {
                        if !seen[byte as usize] {
                            if alphabet.len() == 16 {
                                return Ok(None);
                            }
                            seen[byte as usize] = true;
                            indexes[byte as usize] = alphabet.len() as u8;
                            alphabet.push(byte);
                        }
                    }
                }
                OwnedDatum::UInt(_) => return Ok(None),
            }
        }
        if value_count <= 1 {
            return Ok(None);
        }
        let mut fixed_width =
            (min_string_size == max_string_size).then_some(max_string_size.div_ceil(2));
        if fixed_width.is_some() {
            let extend_waste = (null_count + nop_count) * max_string_size.div_ceil(2);
            if extend_waste > self.rows.len() * 3 {
                fixed_width = None;
            }
        }
        Ok(Some(HexPlan {
            indexes,
            alphabet,
            max_string_size,
            fixed_width,
            packed_size,
            value_count,
            ordinal: 0,
            extend_offset: 0,
            has_extend: null_count > 0 || nop_count > 0,
        }))
    }

    fn string_diff_plan(
        &self,
        column_id: usize,
        spec: &ColumnSpec,
    ) -> Result<Option<StringDiffPlan>> {
        if self.config.row_store_type != RowStoreType::Encoding
            || !is_hex_string_type(spec.obj_type)
        {
            return Ok(None);
        }
        let mut values = Vec::new();
        let mut special_count = 0usize;
        for row in &self.rows {
            match &row[column_id] {
                OwnedDatum::Null | OwnedDatum::Nop => special_count += 1,
                OwnedDatum::Bytes(_) => {
                    values.try_reserve(1).map_err(|_| Error::SizeOverflow)?;
                    values.push(canonical_datum_bytes(&row[column_id], spec)?);
                }
                OwnedDatum::UInt(_) => return Ok(None),
            }
        }
        if values.len() <= 2 {
            return Ok(None);
        }
        let string_size = values[0].len();
        if string_size == 0
            || string_size >= u16::MAX as usize
            || values.iter().any(|value| value.len() != string_size)
        {
            return Ok(None);
        }
        let mut different = vec![false; string_size];
        for value in values.iter().skip(1) {
            let mut common_count = 0usize;
            for position in 0..string_size {
                if !different[position] {
                    if values[0][position] == value[position] {
                        common_count += 1;
                    } else {
                        different[position] = true;
                    }
                }
            }
            if common_count == 0 {
                return Ok(None);
            }
        }
        let diff_size = different.iter().filter(|different| **different).count();
        if diff_size == 0 {
            return Ok(None);
        }
        let mut descriptors = Vec::new();
        let mut begin = 0usize;
        while begin < string_size {
            let is_diff = different[begin];
            let mut end = begin + 1;
            while end < string_size && different[end] == is_diff && end - begin < 127 {
                end += 1;
            }
            descriptors
                .try_reserve(1)
                .map_err(|_| Error::SizeOverflow)?;
            descriptors.push(((end - begin) as u8) << 1 | u8::from(is_diff));
            begin = end;
        }
        if descriptors.len() > u8::MAX as usize {
            return Ok(None);
        }
        let mut common = Vec::new();
        common
            .try_reserve_exact(string_size - diff_size)
            .map_err(|_| Error::SizeOverflow)?;
        for position in 0..string_size {
            if !different[position] {
                common.push(values[0][position]);
            }
        }
        let mut indexes = [0u8; 256];
        let mut seen = [false; 256];
        let mut alphabet = Vec::new();
        let mut hex_suitable = diff_size > 1;
        if hex_suitable {
            alphabet
                .try_reserve_exact(16)
                .map_err(|_| Error::SizeOverflow)?;
            for value in &values {
                for position in 0..string_size {
                    let byte = value[position];
                    if different[position] && !seen[byte as usize] {
                        if alphabet.len() == 16 {
                            hex_suitable = false;
                            break;
                        }
                        seen[byte as usize] = true;
                        indexes[byte as usize] = alphabet.len() as u8;
                        alphabet.push(byte);
                    }
                }
                if !hex_suitable {
                    break;
                }
            }
        }
        if !hex_suitable {
            alphabet.clear();
            indexes = [0; 256];
        }
        let packed_width = if alphabet.is_empty() {
            diff_size
        } else {
            diff_size.div_ceil(2)
        };
        let variable = packed_width
            .checked_mul(special_count)
            .ok_or(Error::SizeOverflow)?
            > self.rows.len().checked_mul(2).ok_or(Error::SizeOverflow)?;
        Ok(Some(StringDiffPlan {
            descriptors,
            alphabet,
            indexes,
            common,
            string_size,
            diff_size,
            value_count: values.len(),
            variable,
            ordinal: 0,
            extend_offset: 0,
            has_extend: special_count > 0,
        }))
    }

    fn string_prefix_plan(
        &self,
        column_id: usize,
        spec: &ColumnSpec,
    ) -> Result<Option<StringPrefixPlan>> {
        if self.config.row_store_type != RowStoreType::Encoding
            || !is_hex_string_type(spec.obj_type)
        {
            return Ok(None);
        }
        let mut values = Vec::new();
        let mut special_count = 0usize;
        for row in &self.rows {
            match &row[column_id] {
                OwnedDatum::Null | OwnedDatum::Nop => special_count += 1,
                OwnedDatum::Bytes(_) => values.push(canonical_datum_bytes(&row[column_id], spec)?),
                OwnedDatum::UInt(_) => return Ok(None),
            }
        }
        let Some(prefix) = values.first().cloned() else {
            return Ok(None);
        };
        if prefix.is_empty() || prefix.len() > u16::MAX as usize {
            return Ok(None);
        }
        let mut common_total = 0usize;
        let mut max_string_size = 0usize;
        for row in &self.rows {
            if matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop) {
                continue;
            }
            let value = canonical_datum_bytes(&row[column_id], spec)?;
            max_string_size = max_string_size.max(value.len());
            let common = common_prefix_length(&prefix, &value);
            if common > u16::MAX as usize {
                return Ok(None);
            }
            common_total = common_total
                .checked_add(common)
                .ok_or(Error::SizeOverflow)?;
        }
        if common_total == 0 {
            return Ok(None);
        }
        let mut indexes = [0u8; 256];
        let mut seen = [false; 256];
        let mut alphabet = Vec::new();
        let mut hex_suitable = true;
        for row in &self.rows {
            if matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop) {
                continue;
            }
            let value = canonical_datum_bytes(&row[column_id], spec)?;
            let common = common_prefix_length(&prefix, &value);
            for &byte in &value[common..] {
                if !seen[byte as usize] {
                    if alphabet.len() == 16 {
                        hex_suitable = false;
                        break;
                    }
                    seen[byte as usize] = true;
                    indexes[byte as usize] = alphabet.len() as u8;
                    alphabet.push(byte);
                }
            }
            if !hex_suitable {
                break;
            }
        }
        if !hex_suitable || alphabet.is_empty() {
            alphabet.clear();
            indexes = [0; 256];
        }
        let mut packed_size = 0usize;
        for row in &self.rows {
            if matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop) {
                continue;
            }
            let value = canonical_datum_bytes(&row[column_id], spec)?;
            let suffix_len = value.len() - common_prefix_length(&prefix, &value);
            packed_size = packed_size
                .checked_add(if alphabet.is_empty() {
                    suffix_len
                } else {
                    suffix_len.div_ceil(2)
                })
                .ok_or(Error::SizeOverflow)?;
        }
        Ok(Some(StringPrefixPlan {
            prefix,
            alphabet,
            indexes,
            max_string_size,
            value_count: values.len(),
            packed_size,
            ordinal: 0,
            extend_offset: 0,
            has_extend: special_count > 0,
        }))
    }

    fn exception_meta(
        &self,
        column_id: usize,
        spec: &ColumnSpec,
        row_ids: &[usize],
    ) -> Result<Option<Vec<u8>>> {
        if row_ids.is_empty() || row_ids.len() > u8::MAX as usize {
            return Ok(None);
        }
        let mut values = Vec::new();
        values
            .try_reserve_exact(row_ids.len())
            .map_err(|_| Error::SizeOverflow)?;
        let mut has_extend = false;
        let mut fixed_width = None;
        let mut variable = false;
        let mut value_size = 0usize;
        for &row_id in row_ids {
            let datum = &self.rows[row_id][column_id];
            if matches!(datum, OwnedDatum::Null | OwnedDatum::Nop) {
                has_extend = true;
                values.push(None);
                continue;
            }
            let bytes = canonical_datum_bytes(datum, spec)?;
            value_size = value_size
                .checked_add(bytes.len())
                .ok_or(Error::SizeOverflow)?;
            match fixed_width {
                Some(width) if width != bytes.len() => variable = true,
                None => fixed_width = Some(bytes.len()),
                _ => {}
            }
            values.push(Some(bytes));
        }
        if fixed_width.is_none() {
            variable = true;
        }
        let bitset_size = self.rows.len().div_ceil(64) * 8;
        let extend_size = if has_extend {
            (row_ids.len() * 2).div_ceil(8)
        } else {
            0
        };
        let index_width = byte_width(value_size as u64);
        let index_size = if variable {
            (row_ids.len() - 1) * index_width
        } else {
            0
        };
        let ext_offset = bitset_size;
        let index_offset = ext_offset + extend_size;
        let data_offset = index_offset + index_size;
        if data_offset > u8::MAX as usize {
            return Ok(None);
        }
        let fixed_width = fixed_width.unwrap_or(0);
        let data_size = if variable {
            value_size
        } else {
            fixed_width * row_ids.len()
        };
        let total_size = EXCEPTION_META_HEADER_SIZE + data_offset + data_size;
        let mut meta = vec![0u8; total_size];
        meta[0] = ext_offset as u8;
        meta[1] = index_offset as u8;
        meta[2] = data_offset as u8;
        meta[3] = if variable {
            index_width as u8
        } else {
            row_ids.len() as u8
        };
        let mut data_position = 0usize;
        for (reference, (&row_id, value)) in row_ids.iter().zip(&values).enumerate() {
            meta[EXCEPTION_META_HEADER_SIZE + row_id / 8] |= 1 << (row_id % 8);
            if has_extend {
                write_bits(
                    &mut meta,
                    (EXCEPTION_META_HEADER_SIZE + ext_offset) * 8 + reference * 2,
                    2,
                    extend_value(&self.rows[row_id][column_id]),
                )?;
            }
            if variable && reference > 0 {
                write_uint(
                    &mut meta,
                    EXCEPTION_META_HEADER_SIZE + index_offset + (reference - 1) * index_width,
                    index_width,
                    data_position,
                )?;
            }
            if let Some(value) = value {
                let start = EXCEPTION_META_HEADER_SIZE + data_offset + data_position;
                meta[start..start + value.len()].copy_from_slice(value);
                data_position += if variable { value.len() } else { fixed_width };
            } else if !variable {
                data_position += fixed_width;
            }
        }
        Ok(Some(meta))
    }

    fn column_equal_plan(
        &self,
        column_id: usize,
        ref_col_idx: usize,
    ) -> Result<Option<ColumnEqualPlan>> {
        let spec = self.config.columns[column_id];
        if self.config.columns[ref_col_idx].obj_type != spec.obj_type {
            return Ok(None);
        }
        let max_exceptions = 100.min(self.rows.len() / 10 + 1);
        let mut exceptions = Vec::new();
        for row_id in 0..self.rows.len() {
            if !datums_equal(
                &self.rows[row_id][column_id],
                &self.rows[row_id][ref_col_idx],
                &spec,
            )? {
                if exceptions.len() == max_exceptions {
                    return Ok(None);
                }
                exceptions.push(row_id);
            }
        }
        let exceptions = if exceptions.is_empty() {
            Vec::new()
        } else {
            let Some(meta) = self.exception_meta(column_id, &spec, &exceptions)? else {
                return Ok(None);
            };
            meta
        };
        Ok(Some(ColumnEqualPlan {
            ref_col_idx,
            exceptions,
        }))
    }

    fn column_substr_plan(
        &self,
        column_id: usize,
        ref_col_idx: usize,
    ) -> Result<Option<ColumnSubstrPlan>> {
        let spec = self.config.columns[column_id];
        if self.config.row_store_type != RowStoreType::Encoding
            || !is_hex_string_type(spec.obj_type)
            || self.config.columns[ref_col_idx].obj_type != spec.obj_type
        {
            return Ok(None);
        }
        let max_exceptions = 100.min(self.rows.len() / 10 + 1);
        let mut exception_rows = Vec::new();
        let mut starts = Vec::new();
        let mut lengths = Vec::new();
        starts
            .try_reserve_exact(self.rows.len())
            .map_err(|_| Error::SizeOverflow)?;
        lengths
            .try_reserve_exact(self.rows.len())
            .map_err(|_| Error::SizeOverflow)?;
        let mut start_candidate = None;
        let mut length_candidate = None;
        let mut start_varies = false;
        let mut length_varies = false;
        let mut max_start = 0usize;
        let mut max_length = 0usize;
        for row_id in 0..self.rows.len() {
            let datum = &self.rows[row_id][column_id];
            let referenced = &self.rows[row_id][ref_col_idx];
            let mut start = 0usize;
            let mut length = 0usize;
            match (datum, referenced) {
                (OwnedDatum::Null, OwnedDatum::Null) | (OwnedDatum::Nop, OwnedDatum::Nop) => {}
                (OwnedDatum::Null | OwnedDatum::Nop, _)
                | (_, OwnedDatum::Null | OwnedDatum::Nop) => {
                    if exception_rows.len() == max_exceptions {
                        return Ok(None);
                    }
                    exception_rows.push(row_id);
                }
                (OwnedDatum::Bytes(_), OwnedDatum::Bytes(_)) => {
                    let value = canonical_datum_bytes(datum, &spec)?;
                    let referenced = canonical_datum_bytes(referenced, &spec)?;
                    length = value.len();
                    max_length = max_length.max(length);
                    if let Some(found) = find_substring(&referenced, &value) {
                        start = found;
                        max_start = max_start.max(start);
                        if !start_varies {
                            if let Some(candidate) = start_candidate {
                                if candidate != start {
                                    start_varies = true;
                                }
                            } else {
                                start_candidate = Some(start);
                            }
                        }
                        if !length_varies {
                            if let Some(candidate) = length_candidate {
                                if candidate != length {
                                    length_varies = true;
                                }
                            } else {
                                length_candidate = Some(length);
                            }
                        }
                    } else {
                        if exception_rows.len() == max_exceptions {
                            return Ok(None);
                        }
                        exception_rows.push(row_id);
                    }
                }
                _ => return Ok(None),
            }
            starts.push(start);
            lengths.push(length);
        }
        if max_start >= u16::MAX as usize || max_length >= u16::MAX as usize {
            return Ok(None);
        }
        let same_start = (!start_varies).then_some(start_candidate.unwrap_or(0) as u16);
        let fixed_length = (!length_varies).then_some(length_candidate.unwrap_or(0) as u16);
        let start_width = if same_start.is_some() {
            0
        } else if max_start <= u8::MAX as usize {
            1
        } else {
            2
        };
        let length_width = if fixed_length.is_some() {
            0
        } else if max_length <= u8::MAX as usize {
            1
        } else {
            2
        };
        let exceptions = if exception_rows.is_empty() {
            Vec::new()
        } else {
            let Some(meta) = self.exception_meta(column_id, &spec, &exception_rows)? else {
                return Ok(None);
            };
            meta
        };
        Ok(Some(ColumnSubstrPlan {
            ref_col_idx,
            exceptions,
            same_start,
            fixed_length,
            start_width,
            length_width,
            starts: starts.into_iter().map(|value| value as u16).collect(),
            lengths: lengths.into_iter().map(|value| value as u16).collect(),
        }))
    }

    fn reference_plan(&self, column_id: usize, size_limit: usize) -> Result<Option<(Plan, usize)>> {
        if self.config.row_store_type != RowStoreType::Encoding {
            return Ok(None);
        }
        let mut best = None;
        let mut best_size = size_limit;
        for ref_col_idx in 0..column_id {
            if let Some(equal) = self.column_equal_plan(column_id, ref_col_idx)? {
                let size = equal.encoded_size();
                if size < best_size {
                    best_size = size;
                    best = Some(Plan::ColumnEqual(equal));
                }
            }
            if let Some(substr) = self.column_substr_plan(column_id, ref_col_idx)? {
                let size = substr.encoded_size(self.rows.len())?;
                if size < best_size {
                    best_size = size;
                    best = Some(Plan::ColumnSubstr(substr));
                }
            }
        }
        Ok(best.map(|plan| (plan, best_size)))
    }

    fn build_meta(
        &self,
        plans: &[Plan],
        extend_width: usize,
        row_extend_bytes: usize,
    ) -> Result<(Vec<u8>, Vec<ColumnHeader>)> {
        let mut meta = Vec::new();
        let mut columns = Vec::new();
        columns
            .try_reserve_exact(plans.len())
            .map_err(|_| Error::SizeOverflow)?;
        for (column_id, plan) in plans.iter().enumerate() {
            let spec = self.config.columns[column_id];
            let mut header = ColumnHeader {
                kind: RAW,
                attr: 0,
                obj_type: spec.obj_type,
                extend_value_offset: 0,
                offset: 0,
                length: 0,
            };
            match plan {
                Plan::Const { reference } => {
                    header.kind = CONST;
                    header.offset = meta.len();
                    meta.extend_from_slice(&[0, 0, *reference, 0, 6, 0]);
                    if *reference == 0 {
                        append_datum_bytes(&self.rows[0][column_id], &spec, &mut meta)?;
                    }
                    header.length = meta.len() - header.offset;
                }
                Plan::Fixed {
                    has_extend,
                    bit_width,
                } => {
                    header.attr = COLUMN_FIX_LENGTH;
                    if *has_extend {
                        header.attr |= COLUMN_HAS_EXTEND_VALUE;
                    }
                    header.offset = meta.len();
                    let extend_bits = if *has_extend {
                        self.rows.len() * extend_width
                    } else {
                        0
                    };
                    if let Some(width) = bit_width {
                        header.attr |= COLUMN_BIT_PACKING;
                        header.length = *width;
                        let bit_len = extend_bits + self.rows.len() * *width;
                        meta.resize(meta.len() + bit_len.div_ceil(8), 0);
                        let data = &mut meta[header.offset..];
                        for (row_id, row) in self.rows.iter().enumerate() {
                            if *has_extend {
                                write_bits(
                                    data,
                                    row_id * extend_width,
                                    extend_width,
                                    extend_value(&row[column_id]),
                                )?;
                            }
                            if let Some(value) = integer_value(&row[column_id], &spec)? {
                                write_bits(data, extend_bits + row_id * *width, *width, value)?;
                            }
                        }
                    } else {
                        let fixed_length = spec.fixed_length.ok_or(Error::InvalidData)?;
                        header.length = fixed_length;
                        let extend_bytes = extend_bits.div_ceil(8);
                        let data_len = extend_bytes + self.rows.len() * fixed_length;
                        meta.resize(meta.len() + data_len, 0);
                        let data = &mut meta[header.offset..];
                        for (row_id, row) in self.rows.iter().enumerate() {
                            if *has_extend {
                                write_bits(
                                    data,
                                    row_id * extend_width,
                                    extend_width,
                                    extend_value(&row[column_id]),
                                )?;
                            }
                            if !matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop) {
                                let start = extend_bytes + row_id * fixed_length;
                                let mut target = Vec::new();
                                append_datum_bytes(&row[column_id], &spec, &mut target)?;
                                data[start..start + fixed_length].copy_from_slice(&target);
                            }
                        }
                    }
                }
                Plan::IntegerBaseDiff {
                    base,
                    has_extend,
                    width,
                    bit_packed,
                } => {
                    let store_size = integer_store_size(spec.obj_type).ok_or(Error::InvalidData)?;
                    header.kind = INTEGER_BASE_DIFF;
                    header.attr = COLUMN_FIX_LENGTH;
                    if *has_extend {
                        header.attr |= COLUMN_HAS_EXTEND_VALUE;
                    }
                    if *bit_packed {
                        header.attr |= COLUMN_BIT_PACKING;
                    }
                    header.offset = meta.len();
                    header.length = 2 + store_size;
                    meta.extend_from_slice(&[0, *width as u8]);
                    meta.extend_from_slice(&base.to_le_bytes()[..store_size]);
                    let extend_bits = if *has_extend {
                        self.rows.len() * extend_width
                    } else {
                        0
                    };
                    let data_size = fixed_data_size(
                        self.rows.len(),
                        extend_width,
                        *has_extend,
                        *width,
                        *bit_packed,
                    )?;
                    let data_offset = meta.len();
                    meta.resize(data_offset + data_size, 0);
                    let data = &mut meta[data_offset..];
                    for (row_id, row) in self.rows.iter().enumerate() {
                        if *has_extend {
                            write_bits(
                                data,
                                row_id * extend_width,
                                extend_width,
                                extend_value(&row[column_id]),
                            )?;
                        }
                        if let Some(value) = integer_value(&row[column_id], &spec)? {
                            let value = normalize_integer(
                                value,
                                store_size,
                                is_signed_store_type(spec.obj_type),
                            );
                            let delta = value.wrapping_sub(*base);
                            if *bit_packed {
                                write_bits(data, extend_bits + row_id * *width, *width, delta)?;
                            } else {
                                let start = extend_bits.div_ceil(8) + row_id * *width;
                                write_uint64(data, start, *width, delta)?;
                            }
                        }
                    }
                }
                Plan::Dict(dict) => {
                    header.kind = DICT;
                    header.attr = COLUMN_FIX_LENGTH;
                    if dict.bit_packed {
                        header.attr |= COLUMN_BIT_PACKING;
                    }
                    header.offset = meta.len();
                    let meta_start = meta.len();
                    write_dict_meta(&mut meta, dict, dict.ref_width)?;
                    header.length = meta.len() - meta_start;
                    let refs_start = meta.len();
                    meta.resize(refs_start + dict.data_size()?, 0);
                    let refs_data = &mut meta[refs_start..];
                    for (row_id, reference) in dict.refs.iter().enumerate() {
                        if dict.bit_packed {
                            write_bits(
                                refs_data,
                                row_id * dict.ref_width,
                                dict.ref_width,
                                *reference as u64,
                            )?;
                        } else {
                            write_uint(
                                refs_data,
                                row_id * dict.ref_width,
                                dict.ref_width,
                                *reference as usize,
                            )?;
                        }
                    }
                }
                Plan::Rle(rle) => {
                    header.kind = RLE;
                    header.offset = meta.len();
                    let meta_start = meta.len();
                    let dict_offset = RLE_META_HEADER_SIZE
                        + rle.row_ids.len() * (rle.row_id_width + rle.ref_width);
                    meta.resize(meta_start + dict_offset, 0);
                    meta[meta_start] = 0;
                    meta[meta_start + 1] = ((rle.ref_width as u8) << 3) | rle.row_id_width as u8;
                    write_u32(&mut meta, meta_start + 2, rle.row_ids.len() as u32)?;
                    write_u32(&mut meta, meta_start + 6, dict_offset as u32)?;
                    let refs_start =
                        meta_start + RLE_META_HEADER_SIZE + rle.row_ids.len() * rle.row_id_width;
                    for index in 0..rle.row_ids.len() {
                        write_uint(
                            &mut meta,
                            meta_start + RLE_META_HEADER_SIZE + index * rle.row_id_width,
                            rle.row_id_width,
                            rle.row_ids[index] as usize,
                        )?;
                        write_uint(
                            &mut meta,
                            refs_start + index * rle.ref_width,
                            rle.ref_width,
                            rle.refs[index] as usize,
                        )?;
                    }
                    write_dict_meta(&mut meta, &rle.dict, 0)?;
                    header.length = meta.len() - meta_start;
                }
                Plan::Hex(hex) => {
                    header.kind = HEX_PACKING;
                    if hex.has_extend {
                        header.attr |= COLUMN_HAS_EXTEND_VALUE;
                    }
                    header.offset = meta.len();
                    header.length = hex.meta_size();
                    let meta_start = meta.len();
                    meta.resize(meta_start + header.length, 0);
                    meta[meta_start] = 0;
                    write_u32(
                        &mut meta,
                        meta_start + 5,
                        hex.fixed_width.unwrap_or(hex.ordinal) as u32,
                    )?;
                    write_u32(&mut meta, meta_start + 9, hex.max_string_size as u32)?;
                    meta[meta_start + HEX_META_HEADER_SIZE..meta_start + header.length]
                        .copy_from_slice(&hex.alphabet);
                    match hex.fixed_width {
                        Some(width) => {
                            header.attr |= COLUMN_FIX_LENGTH;
                            let data_start = meta.len();
                            let extend_bits = if hex.has_extend {
                                self.rows.len() * extend_width
                            } else {
                                0
                            };
                            let data_size = extend_bits.div_ceil(8) + self.rows.len() * width;
                            meta.resize(data_start + data_size, 0);
                            for (row_id, row) in self.rows.iter().enumerate() {
                                if hex.has_extend {
                                    write_bits(
                                        &mut meta[data_start..],
                                        row_id * extend_width,
                                        extend_width,
                                        extend_value(&row[column_id]),
                                    )?;
                                }
                                pack_hex_datum(
                                    &row[column_id],
                                    hex,
                                    &mut meta,
                                    data_start + extend_bits.div_ceil(8) + row_id * width,
                                    false,
                                )?;
                            }
                        }
                        None => {
                            write_u32(&mut meta, meta_start + 1, row_extend_bytes as u32)?;
                            header.extend_value_offset = hex.extend_offset;
                            if hex.ordinal + 1
                                == plans.iter().filter(|plan| plan.is_variable()).count()
                            {
                                header.attr |= COLUMN_LAST_VAR_FIELD;
                            }
                        }
                    }
                }
                Plan::StringDiff(diff) => {
                    header.kind = STRING_DIFF;
                    if !diff.variable {
                        header.attr |= COLUMN_FIX_LENGTH;
                    }
                    if diff.has_extend {
                        header.attr |= COLUMN_HAS_EXTEND_VALUE;
                    }
                    header.offset = meta.len();
                    header.length = diff.meta_size()?;
                    header.extend_value_offset = diff.extend_offset;
                    let meta_start = meta.len();
                    meta.resize(meta_start + header.length, 0);
                    meta[meta_start] = 0;
                    meta[meta_start + 1] = diff.alphabet.len() as u8;
                    write_u16(&mut meta, meta_start + 2, diff.string_size as u16)?;
                    write_u32(
                        &mut meta,
                        meta_start + 4,
                        if diff.variable {
                            row_extend_bytes as u32
                        } else {
                            0
                        },
                    )?;
                    write_u32(
                        &mut meta,
                        meta_start + 8,
                        if diff.variable {
                            diff.ordinal as u32
                        } else {
                            diff.packed_width() as u32
                        },
                    )?;
                    meta[meta_start + 12] = diff.descriptors.len() as u8;
                    let descriptors_start = meta_start + STRING_DIFF_META_HEADER_SIZE;
                    let alphabet_start = descriptors_start + diff.descriptors.len();
                    let common_start = alphabet_start + diff.alphabet.len();
                    meta[descriptors_start..alphabet_start].copy_from_slice(&diff.descriptors);
                    meta[alphabet_start..common_start].copy_from_slice(&diff.alphabet);
                    meta[common_start..meta_start + header.length].copy_from_slice(&diff.common);
                    if diff.variable {
                        if diff.ordinal + 1
                            == plans.iter().filter(|plan| plan.is_variable()).count()
                        {
                            header.attr |= COLUMN_LAST_VAR_FIELD;
                        }
                    } else {
                        let data_start = meta.len();
                        let extend_bits = if diff.has_extend {
                            self.rows.len() * extend_width
                        } else {
                            0
                        };
                        let data_size =
                            extend_bits.div_ceil(8) + self.rows.len() * diff.packed_width();
                        meta.resize(data_start + data_size, 0);
                        for (row_id, row) in self.rows.iter().enumerate() {
                            if diff.has_extend {
                                write_bits(
                                    &mut meta[data_start..],
                                    row_id * extend_width,
                                    extend_width,
                                    extend_value(&row[column_id]),
                                )?;
                            }
                            pack_string_diff_datum(
                                &row[column_id],
                                &spec,
                                diff,
                                &mut meta,
                                data_start + extend_bits.div_ceil(8) + row_id * diff.packed_width(),
                            )?;
                        }
                    }
                }
                Plan::StringPrefix(prefix) => {
                    header.kind = STRING_PREFIX;
                    if prefix.has_extend {
                        header.attr |= COLUMN_HAS_EXTEND_VALUE;
                    }
                    header.offset = meta.len();
                    header.length = prefix.meta_size();
                    header.extend_value_offset = prefix.extend_offset;
                    let meta_start = meta.len();
                    meta.resize(meta_start + header.length, 0);
                    meta[meta_start] = 0;
                    meta[meta_start + 1] = 1;
                    write_u32(&mut meta, meta_start + 2, row_extend_bytes as u32)?;
                    write_u32(&mut meta, meta_start + 6, prefix.ordinal as u32)?;
                    write_u32(&mut meta, meta_start + 10, prefix.max_string_size as u32)?;
                    meta[meta_start + 14] = 1 | ((prefix.alphabet.len() as u8) << 2);
                    let alphabet_start = meta_start + STRING_PREFIX_META_HEADER_SIZE;
                    let prefix_start = alphabet_start + prefix.alphabet.len();
                    meta[alphabet_start..prefix_start].copy_from_slice(&prefix.alphabet);
                    meta[prefix_start..meta_start + header.length].copy_from_slice(&prefix.prefix);
                    if prefix.ordinal + 1 == plans.iter().filter(|plan| plan.is_variable()).count()
                    {
                        header.attr |= COLUMN_LAST_VAR_FIELD;
                    }
                }
                Plan::ColumnEqual(equal) => {
                    header.kind = COLUMN_EQUAL;
                    header.offset = meta.len();
                    header.length = equal.encoded_size();
                    let meta_start = meta.len();
                    meta.resize(meta_start + header.length, 0);
                    meta[meta_start] = 0;
                    write_u16(&mut meta, meta_start + 1, equal.ref_col_idx as u16)?;
                    meta[meta_start + COLUMN_EQUAL_META_HEADER_SIZE..meta_start + header.length]
                        .copy_from_slice(&equal.exceptions);
                }
                Plan::ColumnSubstr(substr) => {
                    header.kind = COLUMN_SUBSTR;
                    header.attr = COLUMN_FIX_LENGTH;
                    header.offset = meta.len();
                    header.length = COLUMN_SUBSTR_META_HEADER_SIZE + substr.exceptions.len();
                    let meta_start = meta.len();
                    meta.resize(meta_start + header.length, 0);
                    meta[meta_start] = 0;
                    meta[meta_start + 1] = (substr.start_width as u8)
                        | ((substr.length_width as u8) << 2)
                        | if substr.same_start.is_some() { 0x10 } else { 0 }
                        | if substr.fixed_length.is_some() {
                            0x20
                        } else {
                            0
                        };
                    write_u16(&mut meta, meta_start + 2, substr.same_start.unwrap_or(0))?;
                    write_u16(&mut meta, meta_start + 4, substr.fixed_length.unwrap_or(0))?;
                    write_u16(&mut meta, meta_start + 6, substr.ref_col_idx as u16)?;
                    meta[meta_start + COLUMN_SUBSTR_META_HEADER_SIZE..meta_start + header.length]
                        .copy_from_slice(&substr.exceptions);
                    let row_width = substr.start_width + substr.length_width;
                    let data_start = meta.len();
                    meta.resize(data_start + self.rows.len() * row_width, 0);
                    for row_id in 0..self.rows.len() {
                        let offset = data_start + row_id * row_width;
                        if substr.start_width > 0 {
                            write_uint(
                                &mut meta,
                                offset,
                                substr.start_width,
                                substr.starts[row_id] as usize,
                            )?;
                        }
                        if substr.length_width > 0 {
                            write_uint(
                                &mut meta,
                                offset + substr.start_width,
                                substr.length_width,
                                substr.lengths[row_id] as usize,
                            )?;
                        }
                    }
                }
                Plan::Variable {
                    ordinal,
                    extend_offset,
                    has_extend,
                } => {
                    header.offset = row_extend_bytes;
                    header.length = *ordinal;
                    header.extend_value_offset = *extend_offset;
                    if *has_extend {
                        header.attr |= COLUMN_HAS_EXTEND_VALUE;
                    }
                    if *ordinal + 1 == plans.iter().filter(|plan| plan.is_variable()).count() {
                        header.attr |= COLUMN_LAST_VAR_FIELD;
                    }
                }
            }
            columns.push(header);
        }
        Ok((meta, columns))
    }

    fn build_row_data(
        &self,
        plans: &[Plan],
        extend_width: usize,
        row_extend_bytes: usize,
    ) -> Result<(Vec<u8>, usize)> {
        let variable: Vec<(usize, &Plan)> = plans
            .iter()
            .enumerate()
            .filter(|(_, plan)| plan.is_variable())
            .collect();
        if variable.is_empty() {
            return Ok((Vec::new(), 0));
        }
        let mut rows_data = Vec::new();
        let mut row_offsets = Vec::new();
        row_offsets
            .try_reserve_exact(self.rows.len() + 1)
            .map_err(|_| Error::SizeOverflow)?;
        row_offsets.push(0usize);
        for row in &self.rows {
            let lengths: Vec<usize> = variable
                .iter()
                .map(|(column_id, plan)| match plan {
                    Plan::Hex(_) => hex_var_len(&row[*column_id]),
                    Plan::StringDiff(diff) => string_diff_var_len(&row[*column_id], diff),
                    Plan::StringPrefix(prefix) => string_prefix_var_len(
                        &row[*column_id],
                        &self.config.columns[*column_id],
                        prefix,
                    ),
                    Plan::Variable { .. } => {
                        datum_len(&row[*column_id], &self.config.columns[*column_id])
                    }
                    _ => Err(Error::InvalidData),
                })
                .collect::<Result<_>>()?;
            let total_var_len = lengths.iter().try_fold(0usize, |sum, length| {
                sum.checked_add(*length).ok_or(Error::SizeOverflow)
            })?;
            let index_width = if variable.len() == 1 {
                0
            } else {
                let before_last = total_var_len - lengths[lengths.len() - 1];
                if before_last <= u8::MAX as usize {
                    1
                } else if before_last <= u16::MAX as usize {
                    2
                } else {
                    4
                }
            };
            let row_len = row_extend_bytes
                .checked_add(total_var_len)
                .and_then(|size| {
                    if index_width == 0 {
                        Some(size)
                    } else {
                        size.checked_add(1 + index_width * (variable.len() - 1))
                    }
                })
                .ok_or(Error::SizeOverflow)?;
            let row_start = rows_data.len();
            rows_data.resize(row_start + row_len, 0);
            let output = &mut rows_data[row_start..];
            let mut data_offset = row_extend_bytes;
            if index_width > 0 {
                output[row_extend_bytes] = index_width as u8;
                data_offset += 1 + index_width * (variable.len() - 1);
            }
            let mut value_offset = 0usize;
            for (var_id, (column_id, plan)) in variable.iter().enumerate() {
                let (extend_offset, has_extend) = match plan {
                    Plan::Variable {
                        extend_offset,
                        has_extend,
                        ..
                    } => (*extend_offset, *has_extend),
                    Plan::Hex(hex) if hex.fixed_width.is_none() => {
                        (hex.extend_offset, hex.has_extend)
                    }
                    Plan::StringDiff(diff) if diff.variable => {
                        (diff.extend_offset, diff.has_extend)
                    }
                    Plan::StringPrefix(prefix) => (prefix.extend_offset, prefix.has_extend),
                    _ => return Err(Error::InvalidData),
                };
                if has_extend {
                    write_bits(
                        output,
                        extend_offset,
                        extend_width,
                        extend_value(&row[*column_id]),
                    )?;
                }
                if var_id > 0 {
                    write_uint(
                        output,
                        row_extend_bytes + 1 + (var_id - 1) * index_width,
                        index_width,
                        value_offset,
                    )?;
                }
                if lengths[var_id] > 0 {
                    match plan {
                        Plan::Hex(hex) => pack_hex_datum(
                            &row[*column_id],
                            hex,
                            output,
                            data_offset + value_offset,
                            true,
                        )?,
                        Plan::StringDiff(diff) => pack_string_diff_datum(
                            &row[*column_id],
                            &self.config.columns[*column_id],
                            diff,
                            output,
                            data_offset + value_offset,
                        )?,
                        Plan::StringPrefix(prefix) => pack_string_prefix_datum(
                            &row[*column_id],
                            &self.config.columns[*column_id],
                            prefix,
                            output,
                            data_offset + value_offset,
                        )?,
                        Plan::Variable { .. } => append_datum_bytes_at(
                            &row[*column_id],
                            &self.config.columns[*column_id],
                            output,
                            data_offset + value_offset,
                        )?,
                        _ => return Err(Error::InvalidData),
                    }
                }
                value_offset += lengths[var_id];
            }
            row_offsets.push(rows_data.len());
        }
        let row_index_byte = if row_offsets[row_offsets.len() - 1] > u16::MAX as usize {
            4
        } else {
            2
        };
        for offset in row_offsets {
            let start = rows_data.len();
            rows_data.resize(start + row_index_byte, 0);
            write_uint(&mut rows_data, start, row_index_byte, offset)?;
        }
        Ok((rows_data, row_index_byte))
    }

    fn assemble(
        &mut self,
        meta: Vec<u8>,
        columns: Vec<ColumnHeader>,
        row_data: Vec<u8>,
        row_index_byte: usize,
        var_column_count: usize,
        extend_width: usize,
        column_checksums: Option<&[u64]>,
    ) -> Result<Vec<u8>> {
        let header_size = FIXED_HEADER_SIZE
            + if self.config.need_column_checksums {
                columns.len() * 8
            } else {
                0
            };
        let row_data_offset = header_size + columns.len() * COLUMN_HEADER_SIZE + meta.len();
        let total_size = row_data_offset
            .checked_add(row_data.len())
            .ok_or(Error::SizeOverflow)?;
        let data_length = total_size - header_size;
        let payload_checksum_offset = header_size;
        let flags = if self.config.need_column_checksums {
            HAS_COLUMN_CHECKSUM
        } else {
            0
        } | if self.config.all_lob_in_row {
            ALL_LOB_IN_ROW
        } else {
            0
        };
        let mut header = Header {
            header_size,
            column_count: columns.len(),
            rowkey_column_count: self.config.rowkey_column_count,
            flags,
            row_count: self.rows.len(),
            row_store_type: self.config.row_store_type as u8,
            opt: (row_index_byte | (extend_width << 3)) as u8,
            var_column_count,
            row_data_offset,
            original_length: i32::try_from(
                self.config
                    .original_size_override
                    .unwrap_or(self.original_size),
            )
            .map_err(|_| Error::SizeOverflow)?,
            max_merged_trans_version: self.config.max_merged_trans_version,
            data_length,
            data_zlength: data_length,
            data_checksum: 0,
        };
        let mut block = vec![0u8; total_size];
        write_u16(&mut block, 0, MICRO_BLOCK_MAGIC)?;
        write_u16(&mut block, 2, MICRO_BLOCK_VERSION)?;
        write_u32(
            &mut block,
            4,
            u32::try_from(header_size).map_err(|_| Error::SizeOverflow)?,
        )?;
        write_u16(&mut block, 10, columns.len() as u16)?;
        write_u16(&mut block, 12, self.config.rowkey_column_count as u16)?;
        write_u16(&mut block, 14, flags)?;
        write_u32(&mut block, 16, self.rows.len() as u32)?;
        block[20] = self.config.row_store_type as u8;
        block[21] = header.opt;
        write_u16(&mut block, 22, var_column_count as u16)?;
        write_u32(
            &mut block,
            24,
            u32::try_from(row_data_offset).map_err(|_| Error::SizeOverflow)?,
        )?;
        write_u32(&mut block, 28, header.original_length as u32)?;
        write_u64(&mut block, 32, self.config.max_merged_trans_version as u64)?;
        write_u32(
            &mut block,
            40,
            u32::try_from(data_length).map_err(|_| Error::SizeOverflow)?,
        )?;
        write_u32(
            &mut block,
            44,
            u32::try_from(data_length).map_err(|_| Error::SizeOverflow)?,
        )?;
        if self.config.need_column_checksums {
            for column in 0..columns.len() {
                let checksum = match column_checksums {
                    Some(checksums) => checksums[column],
                    None => self.column_checksum(column)?,
                };
                write_u64(&mut block, FIXED_HEADER_SIZE + column * 8, checksum)?;
            }
        }
        for (index, column) in columns.iter().enumerate() {
            let offset = header_size + index * COLUMN_HEADER_SIZE;
            block[offset] = 0;
            block[offset + 1] = column.kind;
            block[offset + 2] = column.attr;
            block[offset + 3] = column.obj_type;
            write_u32(&mut block, offset + 4, column.extend_value_offset as u32)?;
            write_u32(&mut block, offset + 8, column.offset as u32)?;
            write_u32(&mut block, offset + 12, column.length as u32)?;
        }
        let meta_offset = header_size + columns.len() * COLUMN_HEADER_SIZE;
        block[meta_offset..row_data_offset].copy_from_slice(&meta);
        block[row_data_offset..].copy_from_slice(&row_data);
        header.data_checksum = crc32c(0, &block[payload_checksum_offset..]);
        write_u64(&mut block, 48, header.data_checksum)?;
        let checksum = header_checksum(&block, &header);
        write_u16(&mut block, 8, checksum)?;
        self.frozen = true;
        Ok(block)
    }

    fn column_checksum(&self, column: usize) -> Result<u64> {
        let spec = self.config.columns[column];
        let mut checksum = 0u64;
        for row in &self.rows {
            let (pack, len) = match row[column] {
                OwnedDatum::Null => (1u32 << 31, 0),
                OwnedDatum::Nop => return Err(Error::Unsupported),
                _ => (
                    datum_len(&row[column], &spec)? as u32,
                    datum_len(&row[column], &spec)?,
                ),
            };
            let mut value_checksum = crc32c(0, &pack.to_le_bytes());
            if len > 0 {
                let mut bytes = Vec::new();
                append_datum_bytes(&row[column], &spec, &mut bytes)?;
                value_checksum = crc32c(value_checksum, &bytes);
            }
            checksum = checksum.wrapping_add(value_checksum);
        }
        Ok(checksum)
    }
}

fn fixed_data_size(
    row_count: usize,
    extend_width: usize,
    has_extend: bool,
    width: usize,
    bit_packed: bool,
) -> Result<usize> {
    let extend_bits = if has_extend {
        row_count
            .checked_mul(extend_width)
            .ok_or(Error::SizeOverflow)?
    } else {
        0
    };
    let value_size = row_count.checked_mul(width).ok_or(Error::SizeOverflow)?;
    if bit_packed {
        extend_bits
            .checked_add(value_size)
            .and_then(|bits| bits.checked_add(7))
            .map(|bits| bits / 8)
            .ok_or(Error::SizeOverflow)
    } else {
        extend_bits
            .div_ceil(8)
            .checked_add(value_size)
            .ok_or(Error::SizeOverflow)
    }
}

fn integer_diff_size(
    row_count: usize,
    store_size: usize,
    extend_width: usize,
    has_extend: bool,
    width: usize,
    bit_packed: bool,
) -> Result<usize> {
    let data_size = fixed_data_size(row_count, extend_width, has_extend, width, bit_packed)?;
    2usize
        .checked_add(store_size)
        .and_then(|size| size.checked_add(data_size))
        .ok_or(Error::SizeOverflow)
}

fn byte_width(value: u64) -> usize {
    if value <= u8::MAX as u64 {
        1
    } else if value <= u16::MAX as u64 {
        2
    } else if value <= u32::MAX as u64 {
        4
    } else {
        8
    }
}

fn normalize_integer(value: u64, store_size: usize, signed: bool) -> u64 {
    if store_size == 8 {
        return value;
    }
    let bits = store_size * 8;
    let mask = (1u64 << bits) - 1;
    let value = value & mask;
    if signed && value & (1u64 << (bits - 1)) != 0 {
        value | !mask
    } else {
        value
    }
}

fn common_prefix_length(left: &[u8], right: &[u8]) -> usize {
    left.iter()
        .zip(right)
        .take_while(|(left, right)| left == right)
        .count()
}

fn find_substring(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    haystack
        .windows(needle.len())
        .position(|window| window == needle)
}

fn datums_equal(left: &OwnedDatum, right: &OwnedDatum, spec: &ColumnSpec) -> Result<bool> {
    match (left, right) {
        (OwnedDatum::Null, OwnedDatum::Null) | (OwnedDatum::Nop, OwnedDatum::Nop) => Ok(true),
        (OwnedDatum::Null | OwnedDatum::Nop, _) | (_, OwnedDatum::Null | OwnedDatum::Nop) => {
            Ok(false)
        }
        _ => Ok(canonical_datum_bytes(left, spec)? == canonical_datum_bytes(right, spec)?),
    }
}

fn integer_value(datum: &OwnedDatum, spec: &ColumnSpec) -> Result<Option<u64>> {
    let Some(store_size) = integer_store_size(spec.obj_type) else {
        return Err(Error::InvalidArgument);
    };
    if spec.fixed_length != Some(store_size) {
        return Err(Error::InvalidArgument);
    }
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(None),
        OwnedDatum::UInt(value) => Ok(Some(*value)),
        OwnedDatum::Bytes(bytes) if bytes.len() <= store_size => {
            let mut value = [0u8; 8];
            value[..bytes.len()].copy_from_slice(bytes);
            Ok(Some(u64::from_le_bytes(value)))
        }
        OwnedDatum::Bytes(_) => Err(Error::InvalidArgument),
    }
}

fn canonical_datum_bytes(datum: &OwnedDatum, spec: &ColumnSpec) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    append_datum_bytes(datum, spec, &mut bytes)?;
    Ok(bytes)
}

fn extend_value(datum: &OwnedDatum) -> u64 {
    match datum {
        OwnedDatum::Null => 1,
        OwnedDatum::Nop => 2,
        _ => 0,
    }
}

fn datum_len(datum: &OwnedDatum, spec: &ColumnSpec) -> Result<usize> {
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(0),
        OwnedDatum::Bytes(bytes) => match spec.fixed_length {
            Some(length) if bytes.len() <= length => Ok(length),
            Some(_) => Err(Error::InvalidArgument),
            None => Ok(bytes.len()),
        },
        OwnedDatum::UInt(_) => spec.fixed_length.ok_or(Error::InvalidArgument),
    }
}

fn hex_var_len(datum: &OwnedDatum) -> Result<usize> {
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(0),
        OwnedDatum::Bytes(bytes) => 1usize
            .checked_add(bytes.len().div_ceil(2))
            .ok_or(Error::SizeOverflow),
        OwnedDatum::UInt(_) => Err(Error::InvalidData),
    }
}

fn string_diff_var_len(datum: &OwnedDatum, plan: &StringDiffPlan) -> Result<usize> {
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(0),
        OwnedDatum::Bytes(_) => Ok(plan.packed_width()),
        OwnedDatum::UInt(_) => Err(Error::InvalidData),
    }
}

fn string_prefix_var_len(
    datum: &OwnedDatum,
    spec: &ColumnSpec,
    plan: &StringPrefixPlan,
) -> Result<usize> {
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(0),
        OwnedDatum::Bytes(_) => {
            let value = canonical_datum_bytes(datum, spec)?;
            let suffix_len = value.len() - common_prefix_length(&plan.prefix, &value);
            3usize
                .checked_add(if plan.alphabet.is_empty() {
                    suffix_len
                } else {
                    suffix_len.div_ceil(2)
                })
                .ok_or(Error::SizeOverflow)
        }
        OwnedDatum::UInt(_) => Err(Error::InvalidData),
    }
}

fn pack_string_prefix_datum(
    datum: &OwnedDatum,
    spec: &ColumnSpec,
    plan: &StringPrefixPlan,
    output: &mut [u8],
    offset: usize,
) -> Result<()> {
    let OwnedDatum::Bytes(_) = datum else {
        return match datum {
            OwnedDatum::Null | OwnedDatum::Nop => Ok(()),
            _ => Err(Error::InvalidData),
        };
    };
    let value = canonical_datum_bytes(datum, spec)?;
    let common = common_prefix_length(&plan.prefix, &value);
    let suffix = &value[common..];
    let packed_size = if plan.alphabet.is_empty() {
        suffix.len()
    } else {
        suffix.len().div_ceil(2)
    };
    let target = output
        .get_mut(offset..offset + 3 + packed_size)
        .ok_or(Error::OutputTooSmall)?;
    target[0] = if plan.alphabet.is_empty() {
        0
    } else {
        ((suffix.len() % 2) as u8) << 4
    };
    target[1..3].copy_from_slice(&(common as u16).to_le_bytes());
    if plan.alphabet.is_empty() {
        target[3..].copy_from_slice(suffix);
    } else {
        for (position, byte) in suffix.iter().enumerate() {
            let shift = if position % 2 == 0 { 4 } else { 0 };
            target[3 + position / 2] |= plan.indexes[*byte as usize] << shift;
        }
    }
    Ok(())
}

fn pack_hex_datum(
    datum: &OwnedDatum,
    plan: &HexPlan,
    output: &mut [u8],
    offset: usize,
    variable: bool,
) -> Result<()> {
    let OwnedDatum::Bytes(bytes) = datum else {
        return match datum {
            OwnedDatum::Null | OwnedDatum::Nop => Ok(()),
            _ => Err(Error::InvalidData),
        };
    };
    let packed_size = bytes.len().div_ceil(2);
    let cell_size = packed_size + usize::from(variable);
    let target = output
        .get_mut(offset..offset.checked_add(cell_size).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    let packed = if variable {
        target[0] = (bytes.len() % 2) as u8;
        &mut target[1..]
    } else {
        target
    };
    for (position, &byte) in bytes.iter().enumerate() {
        let shift = if position % 2 == 0 { 4 } else { 0 };
        packed[position / 2] |= plan.indexes[byte as usize] << shift;
    }
    Ok(())
}

fn pack_string_diff_datum(
    datum: &OwnedDatum,
    spec: &ColumnSpec,
    plan: &StringDiffPlan,
    output: &mut [u8],
    offset: usize,
) -> Result<()> {
    if matches!(datum, OwnedDatum::Null | OwnedDatum::Nop) {
        return Ok(());
    }
    let bytes = canonical_datum_bytes(datum, spec)?;
    if bytes.len() != plan.string_size {
        return Err(Error::InvalidData);
    }
    let width = plan.packed_width();
    let target = output
        .get_mut(offset..offset.checked_add(width).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    let mut string_position = 0usize;
    let mut diff_position = 0usize;
    for &descriptor in &plan.descriptors {
        let count = (descriptor >> 1) as usize;
        if descriptor & 1 == 0 {
            string_position += count;
            continue;
        }
        if plan.alphabet.is_empty() {
            target[diff_position..diff_position + count]
                .copy_from_slice(&bytes[string_position..string_position + count]);
            diff_position += count;
        } else {
            for position in 0..count {
                let shift = if diff_position.is_multiple_of(2) {
                    4
                } else {
                    0
                };
                target[diff_position / 2] |=
                    plan.indexes[bytes[string_position + position] as usize] << shift;
                diff_position += 1;
            }
        }
        string_position += count;
    }
    Ok(())
}

fn is_hex_string_type(obj_type: u8) -> bool {
    matches!(obj_type, 22..=24 | 27..=30 | 36..=38 | 40)
}

fn append_datum_bytes(datum: &OwnedDatum, spec: &ColumnSpec, output: &mut Vec<u8>) -> Result<()> {
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(()),
        OwnedDatum::Bytes(bytes) => {
            if spec.fixed_length.is_some_and(|length| bytes.len() > length) {
                return Err(Error::InvalidArgument);
            }
            output.extend_from_slice(bytes);
            if let Some(length) = spec.fixed_length {
                output.resize(output.len() + length - bytes.len(), 0);
            }
            Ok(())
        }
        OwnedDatum::UInt(value) => {
            let length = spec.fixed_length.ok_or(Error::InvalidArgument)?;
            output.extend_from_slice(&value.to_le_bytes()[..length]);
            Ok(())
        }
    }
}

fn append_datum_bytes_at(
    datum: &OwnedDatum,
    spec: &ColumnSpec,
    output: &mut [u8],
    offset: usize,
) -> Result<()> {
    let length = datum_len(datum, spec)?;
    let target = output
        .get_mut(offset..offset.checked_add(length).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => {}
        OwnedDatum::Bytes(bytes) => target.copy_from_slice(bytes),
        OwnedDatum::UInt(value) => {
            target.copy_from_slice(&value.to_le_bytes()[..length]);
        }
    }
    Ok(())
}

fn write_dict_meta(meta: &mut Vec<u8>, dict: &DictPlan, row_ref_width: usize) -> Result<()> {
    let meta_start = meta.len();
    meta.resize(meta_start + DICT_META_HEADER_SIZE, 0);
    meta[meta_start] = 0;
    meta[meta_start + 1] = row_ref_width as u8;
    write_u32(meta, meta_start + 2, dict.entries.len() as u32)?;
    match dict.entry_width {
        Some(width) => {
            write_u16(meta, meta_start + 6, width as u16)?;
            meta[meta_start + 8] = DICT_FIX_LENGTH;
            for entry in &dict.entries {
                if entry.len() != width {
                    return Err(Error::InvalidData);
                }
                meta.extend_from_slice(entry);
            }
        }
        None => {
            write_u16(meta, meta_start + 6, dict.index_width as u16)?;
            let index_size = dict
                .entries
                .len()
                .saturating_sub(1)
                .checked_mul(dict.index_width)
                .ok_or(Error::SizeOverflow)?;
            let values_size = dict.entries.iter().try_fold(0usize, |size, entry| {
                size.checked_add(entry.len()).ok_or(Error::SizeOverflow)
            })?;
            let payload_start = meta.len();
            meta.resize(payload_start + index_size + values_size, 0);
            let values_start = payload_start + index_size;
            let mut offset = 0usize;
            for (index, entry) in dict.entries.iter().enumerate() {
                if index > 0 {
                    write_uint(
                        meta,
                        payload_start + (index - 1) * dict.index_width,
                        dict.index_width,
                        offset,
                    )?;
                }
                let target = values_start + offset;
                meta[target..target + entry.len()].copy_from_slice(entry);
                offset += entry.len();
            }
        }
    }
    Ok(())
}

fn write_bits(bytes: &mut [u8], bit_offset: usize, width: usize, value: u64) -> Result<()> {
    if width > 64 || bit_offset.checked_add(width).ok_or(Error::SizeOverflow)? > bytes.len() * 8 {
        return Err(Error::OutputTooSmall);
    }
    for bit in 0..width {
        let position = bit_offset + bit;
        let mask = 1u8 << (position % 8);
        if value & (1u64 << bit) == 0 {
            bytes[position / 8] &= !mask;
        } else {
            bytes[position / 8] |= mask;
        }
    }
    Ok(())
}

fn write_uint(bytes: &mut [u8], offset: usize, width: usize, value: usize) -> Result<()> {
    let value = u64::try_from(value).map_err(|_| Error::SizeOverflow)?;
    write_uint64(bytes, offset, width, value)
}

fn write_uint64(bytes: &mut [u8], offset: usize, width: usize, value: u64) -> Result<()> {
    if width > 8 || (width < 8 && value >= (1u64 << (width * 8))) {
        return Err(Error::SizeOverflow);
    }
    let target = bytes
        .get_mut(offset..offset.checked_add(width).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    target.copy_from_slice(&value.to_le_bytes()[..width]);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        AppendOutcome, BlockWriter, ColumnSpec, OwnedDatum, RowStoreType, WriterConfig, DICT,
        FAIL_NEXT_ROW_RESERVE, INTEGER_BASE_DIFF, RLE,
    };
    use crate::format::COLUMN_HEADER_SIZE;
    use crate::format::{COLUMN_FIX_LENGTH, COLUMN_HAS_EXTEND_VALUE};
    use crate::{Block, Cell, Error};

    fn specs() -> Vec<ColumnSpec> {
        let mut key = ColumnSpec::fixed(5, 8);
        key.enable_bit_packing = true;
        vec![
            key,
            ColumnSpec::fixed(5, 2),
            ColumnSpec::variable(22),
            ColumnSpec::variable(22),
        ]
    }

    #[test]
    fn roundtrips_raw_const_null_nop_and_variable_cells() {
        let mut writer = BlockWriter::new(WriterConfig::new(specs(), 1)).unwrap();
        for row in 0..100u64 {
            let second = match row % 3 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes((row as u16).to_le_bytes().to_vec()),
            };
            let third = match row % 4 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(
                    format!("value-{row:03}!{}", "x".repeat(row as usize % 5)).into_bytes(),
                ),
            };
            let outcome = writer
                .append_row(&[
                    OwnedDatum::UInt(row),
                    second,
                    third,
                    OwnedDatum::Bytes(b"constant".to_vec()),
                ])
                .unwrap();
            assert_eq!(outcome.accepted_rows, row as usize + 1);
            assert!(!outcome.is_full);
        }
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.row_count(), 100);
        for row in 0..100u64 {
            let third = block.project(&[row as u32], &[2]).unwrap();
            assert_eq!(block.cell(row as usize, 0).unwrap().as_u64().unwrap(), row);
            match row % 3 {
                0 => assert_eq!(block.cell(row as usize, 1).unwrap(), Cell::Null),
                1 => assert_eq!(block.cell(row as usize, 1).unwrap(), Cell::Nop),
                _ => assert_eq!(block.cell(row as usize, 1).unwrap().as_u64().unwrap(), row),
            }
            match row % 4 {
                0 => assert_eq!(third.get(0, 0).unwrap(), Cell::Null),
                1 => assert_eq!(third.get(0, 0).unwrap(), Cell::Nop),
                _ => assert_eq!(
                    third.get(0, 0).unwrap().bytes().unwrap(),
                    format!("value-{row:03}!{}", "x".repeat(row as usize % 5)).as_bytes()
                ),
            }
            assert_eq!(
                block.cell(row as usize, 3).unwrap().bytes().unwrap(),
                b"constant"
            );
        }
        for end in 0..bytes.len() {
            assert!(Block::new(&bytes[..end]).is_err());
        }
    }

    #[test]
    fn full_and_failed_appends_preserve_accepted_rows() {
        let mut config = WriterConfig::new(specs(), 1);
        config.block_size_limit = 300;
        let mut writer = BlockWriter::new(config).unwrap();
        let row = [
            OwnedDatum::UInt(1),
            OwnedDatum::Bytes(2u16.to_le_bytes().to_vec()),
            OwnedDatum::Bytes(vec![b'x'; 180]),
            OwnedDatum::Bytes(b"constant".to_vec()),
        ];
        assert!(!writer.append_row(&row).unwrap().is_full);
        let full = writer.append_row(&row).unwrap();
        assert_eq!(
            full,
            AppendOutcome {
                accepted_rows: 1,
                is_full: true
            }
        );
        assert_eq!(writer.row_count(), 1);
        assert_eq!(
            writer.append_row(&row[..3]).unwrap_err(),
            Error::InvalidArgument
        );
        assert_eq!(writer.row_count(), 1);
        assert_eq!(
            Block::new(&writer.finish().unwrap()).unwrap().row_count(),
            1
        );
        assert_eq!(writer.finish().unwrap_err(), Error::Frozen);
    }

    #[test]
    fn allocation_failure_is_transactional_and_retryable() {
        let config = WriterConfig::new(vec![ColumnSpec::fixed(5, 8)], 1);
        let mut writer = BlockWriter::new(config).unwrap();
        writer.append_row(&[OwnedDatum::UInt(1)]).unwrap();
        FAIL_NEXT_ROW_RESERVE.with(|fail| fail.set(true));
        assert_eq!(
            writer.append_row(&[OwnedDatum::UInt(2)]),
            Err(Error::SizeOverflow)
        );
        assert_eq!(writer.row_count(), 1);
        let outcome = writer.append_row(&[OwnedDatum::UInt(2)]).unwrap();
        assert_eq!(outcome.accepted_rows, 2);
        assert!(!outcome.is_full);
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.cell(0, 0).unwrap().as_u64().unwrap(), 1);
        assert_eq!(block.cell(1, 0).unwrap().as_u64().unwrap(), 2);
    }

    #[test]
    fn retries_the_unaccepted_row_in_the_next_block() {
        let mut config = WriterConfig::new(vec![ColumnSpec::fixed(5, 8)], 1);
        config.block_size_limit = 220;
        let mut next_row = 0u64;
        let mut block_count = 0;
        let mut full_count = 0;
        let mut actual = Vec::new();
        while next_row < 64 {
            let mut writer = BlockWriter::new(config.clone()).unwrap();
            loop {
                let outcome = writer.append_row(&[OwnedDatum::UInt(next_row)]).unwrap();
                if outcome.is_full {
                    assert_eq!(outcome.accepted_rows, writer.row_count());
                    full_count += 1;
                    break;
                }
                next_row += 1;
                if next_row == 64 {
                    break;
                }
            }
            let bytes = writer.finish().unwrap();
            let block = Block::new(&bytes).unwrap();
            for row in 0..block.row_count() {
                actual.push(block.cell(row, 0).unwrap().as_u64().unwrap());
            }
            block_count += 1;
        }
        assert!(block_count > 1);
        assert_eq!(full_count, block_count - 1);
        assert_eq!(actual, (0..64).collect::<Vec<_>>());
    }

    #[test]
    fn emits_column_checksums_and_all_fixed_layout() {
        let mut config =
            WriterConfig::new(vec![ColumnSpec::fixed(5, 8), ColumnSpec::variable(22)], 1);
        config.need_column_checksums = true;
        let mut writer = BlockWriter::new(config).unwrap();
        writer
            .append_row(&[OwnedDatum::UInt(7), OwnedDatum::Bytes(b"same".to_vec())])
            .unwrap();
        writer
            .append_row(&[OwnedDatum::UInt(8), OwnedDatum::Bytes(b"same".to_vec())])
            .unwrap();
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.find_uint(0, 8).unwrap(), Some(1));
        assert_eq!(block.cell(1, 1).unwrap().bytes().unwrap(), b"same");
    }

    #[test]
    fn accepts_external_column_checksums_with_nop() {
        let mut config =
            WriterConfig::new(vec![ColumnSpec::fixed(5, 8), ColumnSpec::variable(22)], 1);
        config.need_column_checksums = true;
        let checksums = [0x0123_4567_89ab_cdef, 0xfedc_ba98_7654_3210];
        let mut writer = BlockWriter::new(config).unwrap();
        writer
            .append_row(&[OwnedDatum::UInt(7), OwnedDatum::Nop])
            .unwrap();
        writer
            .append_row(&[OwnedDatum::UInt(8), OwnedDatum::Bytes(b"value".to_vec())])
            .unwrap();
        let bytes = writer.finish_with_column_checksums(&checksums).unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.cell(0, 1).unwrap(), Cell::Nop);
        assert_eq!(block.cell(1, 1).unwrap().bytes().unwrap(), b"value");
        for (column, checksum) in checksums.iter().enumerate() {
            assert_eq!(
                crate::format::u64_at(&bytes, crate::format::FIXED_HEADER_SIZE + column * 8)
                    .unwrap(),
                *checksum
            );
        }

        let mut config = WriterConfig::new(vec![ColumnSpec::variable(22)], 1);
        config.need_column_checksums = true;
        let mut writer = BlockWriter::new(config).unwrap();
        writer.append_row(&[OwnedDatum::Nop]).unwrap();
        assert_eq!(writer.finish().unwrap_err(), Error::Unsupported);
        assert_eq!(
            writer.finish_with_column_checksums(&[]).unwrap_err(),
            Error::InvalidArgument
        );
        assert_eq!(writer.row_count(), 1);
        let bytes = writer.finish_with_column_checksums(&[42]).unwrap();
        assert_eq!(Block::new(&bytes).unwrap().cell(0, 0).unwrap(), Cell::Nop);
    }

    #[test]
    fn pads_short_fixed_width_byte_values() {
        for row_count in 1..=2 {
            let config = WriterConfig::new(vec![ColumnSpec::fixed(5, 8)], 1);
            let mut writer = BlockWriter::new(config).unwrap();
            writer
                .append_row(&[OwnedDatum::Bytes(vec![0x34, 0x12])])
                .unwrap();
            if row_count == 2 {
                writer
                    .append_row(&[OwnedDatum::Bytes(vec![0x78, 0x56, 0x34])])
                    .unwrap();
            }
            let bytes = writer.finish().unwrap();
            let block = Block::new(&bytes).unwrap();
            assert_eq!(
                block.cell(0, 0).unwrap().bytes().unwrap(),
                &[0x34, 0x12, 0, 0, 0, 0, 0, 0]
            );
            if row_count == 2 {
                assert_eq!(
                    block.cell(1, 0).unwrap().bytes().unwrap(),
                    &[0x78, 0x56, 0x34, 0, 0, 0, 0, 0]
                );
            }
        }
    }

    #[test]
    fn writes_selective_blocks_with_the_shared_layout() {
        let mut config = WriterConfig::new(vec![ColumnSpec::variable(22)], 1);
        config.row_store_type = RowStoreType::Selective;
        let mut writer = BlockWriter::new(config).unwrap();
        writer
            .append_row(&[OwnedDatum::Bytes(b"index-key".to_vec())])
            .unwrap();
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.header.row_store_type, RowStoreType::Selective as u8);
        assert_eq!(block.cell(0, 0).unwrap().bytes().unwrap(), b"index-key");
    }

    #[test]
    fn selects_integer_diff_and_dict_and_roundtrips_special_values() {
        let mut key = ColumnSpec::fixed(5, 8);
        key.enable_bit_packing = true;
        let mut repeated = ColumnSpec::fixed(5, 8);
        repeated.enable_bit_packing = true;
        let mut text = ColumnSpec::variable(22);
        text.enable_bit_packing = true;
        let mut writer = BlockWriter::new(WriterConfig::new(vec![key, repeated, text], 1)).unwrap();
        for row in 0..4096u64 {
            let repeated_value = match row % 119 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::UInt((row % 11) * 1_000_000_000 + 7),
            };
            let text_value = match row % 137 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(
                    format!("category-{:02}-shared-dictionary-padding", row % 7).into_bytes(),
                ),
            };
            writer
                .append_row(&[
                    OwnedDatum::UInt(1_000_000 + row),
                    repeated_value,
                    text_value,
                ])
                .unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_eq!(bytes[header.header_size + 1], INTEGER_BASE_DIFF);
        assert_eq!(bytes[header.header_size + COLUMN_HEADER_SIZE + 1], DICT);
        assert_eq!(bytes[header.header_size + 2 * COLUMN_HEADER_SIZE + 1], DICT);
        assert!(bytes.len() < 4096 * 8);
        let block = Block::new(&bytes).unwrap();
        for row in 0..4096u64 {
            let row_id = row as usize;
            assert_eq!(
                block.cell(row_id, 0).unwrap().as_u64().unwrap(),
                1_000_000 + row
            );
            match row % 119 {
                0 => assert_eq!(block.cell(row_id, 1).unwrap(), Cell::Null),
                1 => assert_eq!(block.cell(row_id, 1).unwrap(), Cell::Nop),
                _ => assert_eq!(
                    block.cell(row_id, 1).unwrap().as_u64().unwrap(),
                    (row % 11) * 1_000_000_000 + 7
                ),
            }
            match row % 137 {
                0 => assert_eq!(block.cell(row_id, 2).unwrap(), Cell::Null),
                1 => assert_eq!(block.cell(row_id, 2).unwrap(), Cell::Nop),
                _ => assert_eq!(
                    block.cell(row_id, 2).unwrap().bytes().unwrap(),
                    format!("category-{:02}-shared-dictionary-padding", row % 7).as_bytes()
                ),
            }
        }
        assert_eq!(block.find_uint(0, 1_004_095).unwrap(), Some(4095));
    }

    #[test]
    fn selects_string_diff_for_fixed_width_values() {
        let config = WriterConfig::new(vec![ColumnSpec::fixed(22, 16)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        for row in 0..64u64 {
            let value = match row % 29 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => {
                    let mut bytes = b"shared!!".to_vec();
                    bytes.extend_from_slice(&row.to_le_bytes());
                    OwnedDatum::Bytes(bytes)
                }
            };
            writer.append_row(&[value]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        let column_header = header.header_size;
        assert_eq!(bytes[column_header + 1], 5);
        assert_ne!(bytes[column_header + 2] & COLUMN_FIX_LENGTH, 0);
        assert_ne!(bytes[column_header + 2] & COLUMN_HAS_EXTEND_VALUE, 0);
        let column_offset = u32::from_le_bytes(
            bytes[column_header + 8..column_header + 12]
                .try_into()
                .unwrap(),
        ) as usize;
        let meta_offset = header.header_size + COLUMN_HEADER_SIZE + column_offset;
        assert_eq!(bytes[meta_offset + 1], 0);

        let block = Block::new(&bytes).unwrap();
        let rows = (0..64u32).rev().collect::<Vec<_>>();
        let projection = block.project(&rows, &[0]).unwrap();
        for output_row in 0..64usize {
            let row = 63 - output_row;
            let cell = projection.get(output_row, 0).unwrap();
            match row % 29 {
                0 => assert_eq!(cell, Cell::Null),
                1 => assert_eq!(cell, Cell::Nop),
                _ => {
                    let mut expected = b"shared!!".to_vec();
                    expected.extend_from_slice(&(row as u64).to_le_bytes());
                    assert_eq!(cell.bytes().unwrap(), expected);
                }
            }
        }
    }

    #[test]
    fn selects_variable_string_diff_for_sparse_values() {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        for row in 0..96u64 {
            let value = match row % 4 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                2 => OwnedDatum::Null,
                _ => {
                    let mut bytes = vec![b'x'; 56];
                    bytes.extend_from_slice(
                        format!("{:08}", row * 7_654_321 % 100_000_000).as_bytes(),
                    );
                    OwnedDatum::Bytes(bytes)
                }
            };
            writer.append_row(&[value]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        let column_header = header.header_size;
        assert_eq!(bytes[column_header + 1], 5);
        assert_eq!(bytes[column_header + 2] & COLUMN_FIX_LENGTH, 0);
        assert_ne!(bytes[column_header + 2] & COLUMN_HAS_EXTEND_VALUE, 0);
        let column_offset = u32::from_le_bytes(
            bytes[column_header + 8..column_header + 12]
                .try_into()
                .unwrap(),
        ) as usize;
        let meta_offset = header.header_size + COLUMN_HEADER_SIZE + column_offset;
        assert!(bytes[meta_offset + 1] > 0);

        let block = Block::new(&bytes).unwrap();
        let projection = block
            .project(&(0..96u32).collect::<Vec<_>>(), &[0])
            .unwrap();
        for row in 0..96usize {
            let cell = projection.get(row, 0).unwrap();
            match row % 4 {
                0 | 2 => assert_eq!(cell, Cell::Null),
                1 => assert_eq!(cell, Cell::Nop),
                _ => {
                    let mut expected = vec![b'x'; 56];
                    expected.extend_from_slice(
                        format!("{:08}", row * 7_654_321 % 100_000_000).as_bytes(),
                    );
                    assert_eq!(cell.bytes().unwrap(), expected);
                }
            }
        }
    }

    #[test]
    fn selects_rle_for_sorted_runs_and_preserves_special_values() {
        let fixed = ColumnSpec::fixed(5, 8);
        let variable = ColumnSpec::variable(22);
        let mut writer = BlockWriter::new(WriterConfig::new(vec![fixed, variable], 1)).unwrap();
        for row in 0..512usize {
            let run = row / 32;
            let fixed_value = match run {
                5 => OwnedDatum::Null,
                11 => OwnedDatum::Nop,
                _ => OwnedDatum::UInt((run * 1_000_003 + 17) as u64),
            };
            let variable_value = match run {
                6 => OwnedDatum::Null,
                12 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(format!("category-{}-shared-padding", run % 4).into_bytes()),
            };
            writer.append_row(&[fixed_value, variable_value]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_eq!(bytes[header.header_size + 1], RLE);
        assert_eq!(bytes[header.header_size + COLUMN_HEADER_SIZE + 1], RLE);
        for column in 0..2 {
            let column_header = header.header_size + column * COLUMN_HEADER_SIZE;
            assert_eq!(bytes[column_header + 2], 0);
            let column_offset = u32::from_le_bytes(
                bytes[column_header + 8..column_header + 12]
                    .try_into()
                    .unwrap(),
            ) as usize;
            let rle_offset =
                header.header_size + header.column_count * COLUMN_HEADER_SIZE + column_offset;
            let dict_offset =
                u32::from_le_bytes(bytes[rle_offset + 6..rle_offset + 10].try_into().unwrap())
                    as usize;
            assert_eq!(bytes[rle_offset + dict_offset + 1], 0);
        }
        let block = Block::new(&bytes).unwrap();
        let rows = (0..512u32).rev().collect::<Vec<_>>();
        let projection = block.project(&rows, &[0, 1]).unwrap();
        for output_row in 0..512usize {
            let row = 511 - output_row;
            let run = row / 32;
            let fixed_cell = projection.get(output_row, 0).unwrap();
            match run {
                5 => assert_eq!(fixed_cell, Cell::Null),
                11 => assert_eq!(fixed_cell, Cell::Nop),
                _ => assert_eq!(
                    fixed_cell.bytes().unwrap(),
                    ((run * 1_000_003 + 17) as u64).to_le_bytes()
                ),
            }
            let variable_cell = projection.get(output_row, 1).unwrap();
            match run {
                6 => assert_eq!(variable_cell, Cell::Null),
                12 => assert_eq!(variable_cell, Cell::Nop),
                _ => assert_eq!(
                    variable_cell.bytes().unwrap(),
                    format!("category-{}-shared-padding", run % 4).as_bytes()
                ),
            }
        }
        for end in 0..bytes.len() {
            assert!(Block::new(&bytes[..end]).is_err());
        }
    }

    #[test]
    fn integer_base_diff_preserves_signed_extremes() {
        for (width, values) in [
            (2, (-300..-44).collect::<Vec<i64>>()),
            (4, (-70_000..-69_744).collect::<Vec<i64>>()),
            (8, (0..256).map(|delta| i64::MIN + delta).collect()),
            (8, (-1_000_000_000..-999_999_744).collect()),
        ] {
            let obj_type = match width {
                2 => 2,
                4 => 4,
                8 => 5,
                _ => unreachable!(),
            };
            let spec = ColumnSpec::fixed(obj_type, width);
            let mut writer = BlockWriter::new(WriterConfig::new(vec![spec], 0)).unwrap();
            for value in &values {
                writer
                    .append_row(&[OwnedDatum::Bytes(value.to_le_bytes()[..width].to_vec())])
                    .unwrap();
            }
            let bytes = writer.finish().unwrap();
            let header = crate::format::parse_header(&bytes).unwrap();
            assert_eq!(bytes[header.header_size + 1], INTEGER_BASE_DIFF);
            let block = Block::new(&bytes).unwrap();
            for (row, value) in values.iter().enumerate() {
                assert_eq!(block.cell(row, 0).unwrap().as_i64().unwrap(), *value);
            }
        }
    }

    #[test]
    fn dictionaries_handle_only_null_and_nop_rows() {
        for spec in [ColumnSpec::fixed(5, 8)] {
            let mut writer = BlockWriter::new(WriterConfig::new(vec![spec], 0)).unwrap();
            for value in [OwnedDatum::Null, OwnedDatum::Nop, OwnedDatum::Null] {
                writer.append_row(&[value]).unwrap();
            }
            let bytes = writer.finish().unwrap();
            let header = crate::format::parse_header(&bytes).unwrap();
            assert_eq!(bytes[header.header_size + 1], DICT);
            assert_eq!(header.var_column_count, 0);
            let block = Block::new(&bytes).unwrap();
            assert_eq!(block.cell(0, 0).unwrap(), Cell::Null);
            assert_eq!(block.cell(1, 0).unwrap(), Cell::Nop);
            assert_eq!(block.cell(2, 0).unwrap(), Cell::Null);
        }
    }

    #[test]
    fn preserves_out_row_lob_header_flag() {
        let mut config = WriterConfig::new(vec![ColumnSpec::variable(28)], 0);
        config.all_lob_in_row = false;
        let mut writer = BlockWriter::new(config).unwrap();
        writer
            .append_row(&[OwnedDatum::Bytes(b"lob-locator".to_vec())])
            .unwrap();
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.header.flags & crate::format::ALL_LOB_IN_ROW, 0);
        assert_eq!(block.cell(0, 0).unwrap().bytes().unwrap(), b"lob-locator");
    }

    #[test]
    fn rejects_integer_values_that_do_not_fit_the_column() {
        for (length, value) in [(1, 256), (2, 65_536), (4, 1_u64 << 32)] {
            let config = WriterConfig::new(vec![ColumnSpec::fixed(5, length)], 1);
            let mut writer = BlockWriter::new(config).unwrap();
            assert_eq!(
                writer.append_row(&[OwnedDatum::UInt(value)]),
                Err(Error::InvalidArgument)
            );
            assert_eq!(writer.row_count(), 0);
        }
    }

    #[test]
    fn hex_strings_use_compact_native_encoding() {
        let values = (0..128usize)
            .map(|row| {
                (0..32usize)
                    .map(|position| {
                        let nibble = (row * 13 + position * 7 + row / (position + 1)) & 0xf;
                        b"0123456789abcdef"[nibble]
                    })
                    .collect::<Vec<u8>>()
            })
            .collect::<Vec<_>>();
        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        for (row, value) in values.iter().enumerate() {
            let datum = match row % 29 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(value.clone()),
            };
            writer.append_row(&[datum]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_eq!(bytes[header.header_size + 1], 6);
        assert!(
            bytes.len() < 128 * 32,
            "HEX packing did not reduce the block size"
        );

        let block = Block::new(&bytes).unwrap();
        let rows = (0..values.len() as u32).collect::<Vec<_>>();
        let projection = block.project(&rows, &[0]).unwrap();
        for row in 0..values.len() {
            let cell = projection.get(row, 0).unwrap();
            match row % 29 {
                0 => assert_eq!(cell, Cell::Null),
                1 => assert_eq!(cell, Cell::Nop),
                _ => assert_eq!(cell.bytes().unwrap(), values[row]),
            }
        }
    }

    #[test]
    fn variable_hex_roundtrips_empty_odd_even_null_and_nop() {
        let values = (0..73usize)
            .map(|row| {
                (0..row % 41)
                    .map(|position| b"0123456789abcdef"[(row + position * 5) & 0xf])
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>();
        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        for (row, value) in values.iter().enumerate() {
            let datum = match row % 17 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(value.clone()),
            };
            writer.append_row(&[datum]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        let column = header.header_size;
        assert_eq!(bytes[column + 1], 6);
        assert_eq!(bytes[column + 2] & 1, 0);

        let block = Block::new(&bytes).unwrap();
        let rows = (0..values.len() as u32).collect::<Vec<_>>();
        let projection = block.project(&rows, &[0]).unwrap();
        for row in 0..values.len() {
            let cell = projection.get(row, 0).unwrap();
            match row % 17 {
                0 => assert_eq!(cell, Cell::Null),
                1 => assert_eq!(cell, Cell::Nop),
                _ => assert_eq!(cell.bytes().unwrap(), values[row]),
            }
        }
    }

    #[test]
    fn hex_only_wins_when_it_reduces_encoded_size() {
        let mut non_hex =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        for row in 0..32u8 {
            let mut value = (0..=16).collect::<Vec<_>>();
            value.extend_from_slice(&[row; 64]);
            non_hex.append_row(&[OwnedDatum::Bytes(value)]).unwrap();
        }
        let bytes = non_hex.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_ne!(bytes[header.header_size + 1], 6);

        let values = [b"0123456789abcdef".as_slice(); 4];
        let mut dictionary =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        for row in 0..256usize {
            let mut value = values[row % values.len()].to_vec();
            value.extend_from_slice(values[(row / values.len()) % values.len()]);
            value.push(b'0' + (row % 4) as u8);
            value.extend_from_slice(&b"012"[..(row / 16) % 4]);
            dictionary.append_row(&[OwnedDatum::Bytes(value)]).unwrap();
        }
        let bytes = dictionary.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_eq!(bytes[header.header_size + 1], DICT);
    }
    fn column_kind(bytes: &[u8], column: usize) -> u8 {
        let header = crate::format::parse_header(bytes).unwrap();
        crate::format::parse_column_header(bytes, header.header_size + column * COLUMN_HEADER_SIZE)
            .unwrap()
            .kind
    }

    fn assert_projected_datum(cell: Cell<'_>, datum: &OwnedDatum) {
        match datum {
            OwnedDatum::Null => assert_eq!(cell, Cell::Null),
            OwnedDatum::Nop => assert_eq!(cell, Cell::Nop),
            OwnedDatum::Bytes(bytes) => assert_eq!(cell.bytes().unwrap(), bytes),
            OwnedDatum::UInt(value) => assert_eq!(cell.as_u64().unwrap(), *value),
        }
    }

    #[test]
    fn selects_and_roundtrips_string_prefix() {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        let prefix = "https://example.com/this/is/a/long/common/prefix/that/repeats/";
        let mut expected = Vec::new();
        for row in 0..40usize {
            let value = match row {
                5 => OwnedDatum::Null,
                9 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(
                    format!("{prefix}{row:02x}{}", "a".repeat(row % 7)).into_bytes(),
                ),
            };
            writer.append_row(&[value.clone()]).unwrap();
            expected.push(value);
        }
        let bytes = writer.finish().unwrap();
        assert_eq!(column_kind(&bytes, 0), super::STRING_PREFIX);
        let block = Block::new(&bytes).unwrap();
        let projection = block
            .project(&(0..expected.len() as u32).collect::<Vec<_>>(), &[0])
            .unwrap();
        for (row, datum) in expected.iter().enumerate() {
            assert_projected_datum(projection.get(row, 0).unwrap(), datum);
        }
    }

    #[test]
    fn selects_and_roundtrips_column_equal_with_exceptions() {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22), ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        let mut expected = Vec::new();
        for row in 0..40usize {
            let referenced = match row {
                5 => OwnedDatum::Null,
                9 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(
                    format!("referenced-row-{row:02}-{}", "x".repeat(40 + row)).into_bytes(),
                ),
            };
            let equal = match row {
                3 => OwnedDatum::Bytes(b"a-short-exception".to_vec()),
                7 => OwnedDatum::Bytes(
                    format!("a-much-longer-exception-{row}-{}", "y".repeat(50)).into_bytes(),
                ),
                11 => OwnedDatum::Null,
                _ => referenced.clone(),
            };
            writer
                .append_row(&[referenced.clone(), equal.clone()])
                .unwrap();
            expected.push((referenced, equal));
        }
        let bytes = writer.finish().unwrap();
        assert_eq!(column_kind(&bytes, 1), super::COLUMN_EQUAL);
        let block = Block::new(&bytes).unwrap();
        let projection = block
            .project(&(0..expected.len() as u32).collect::<Vec<_>>(), &[0, 1])
            .unwrap();
        for (row, (referenced, equal)) in expected.iter().enumerate() {
            assert_projected_datum(projection.get(row, 0).unwrap(), referenced);
            assert_projected_datum(projection.get(row, 1).unwrap(), equal);
        }
    }

    #[test]
    fn selects_and_roundtrips_column_substr_with_exceptions() {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22), ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        let mut expected = Vec::new();
        for row in 0..40usize {
            let referenced = match row {
                5 => OwnedDatum::Null,
                9 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(
                    format!(
                        "https://example.com/a/shared/prefix/that/is/long/enough/{row:02}/{}",
                        "abcdefghij".repeat(8)
                    )
                    .into_bytes(),
                ),
            };
            let substring = match (&referenced, row) {
                (_, 3) => OwnedDatum::Bytes(b"not-a-substring".to_vec()),
                (_, 11) => OwnedDatum::Null,
                (OwnedDatum::Bytes(bytes), _) => {
                    let start = 2 + row % 3;
                    let length = 8 + row % 5;
                    OwnedDatum::Bytes(bytes[start..start + length].to_vec())
                }
                _ => referenced.clone(),
            };
            writer
                .append_row(&[referenced.clone(), substring.clone()])
                .unwrap();
            expected.push((referenced, substring));
        }
        let bytes = writer.finish().unwrap();
        assert_eq!(column_kind(&bytes, 0), super::STRING_DIFF);
        assert_eq!(column_kind(&bytes, 1), super::COLUMN_SUBSTR);
        let block = Block::new(&bytes).unwrap();
        let projection = block
            .project(&(0..expected.len() as u32).collect::<Vec<_>>(), &[0, 1])
            .unwrap();
        for (row, (referenced, substring)) in expected.iter().enumerate() {
            assert_projected_datum(projection.get(row, 0).unwrap(), referenced);
            assert_projected_datum(projection.get(row, 1).unwrap(), substring);
        }
    }

    #[test]
    fn selects_and_roundtrips_integer_column_equal() {
        let config = WriterConfig::new(vec![ColumnSpec::fixed(5, 8), ColumnSpec::fixed(5, 8)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        let mut expected = Vec::new();
        for row in 0..40u64 {
            let referenced = match row {
                5 => OwnedDatum::Null,
                9 => OwnedDatum::Nop,
                _ => OwnedDatum::UInt(row * 17),
            };
            let equal = match row {
                3 | 11 => OwnedDatum::UInt(u64::MAX - row),
                _ => referenced.clone(),
            };
            writer
                .append_row(&[referenced.clone(), equal.clone()])
                .unwrap();
            expected.push((referenced, equal));
        }
        let bytes = writer.finish().unwrap();
        assert_eq!(column_kind(&bytes, 1), super::COLUMN_EQUAL);
        let block = Block::new(&bytes).unwrap();
        let projection = block
            .project(&(0..expected.len() as u32).collect::<Vec<_>>(), &[0, 1])
            .unwrap();
        for (row, (referenced, equal)) in expected.iter().enumerate() {
            assert_projected_datum(projection.get(row, 0).unwrap(), referenced);
            assert_projected_datum(projection.get(row, 1).unwrap(), equal);
            assert_projected_datum(block.cell(row, 1).unwrap(), equal);
        }
    }
}
