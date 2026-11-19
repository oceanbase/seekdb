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
    header_checksum, write_u16, write_u32, write_u64, ColumnHeader, Header, ALL_LOB_IN_ROW,
    COLUMN_BIT_PACKING, COLUMN_FIX_LENGTH, COLUMN_HAS_EXTEND_VALUE, COLUMN_HEADER_SIZE,
    COLUMN_LAST_VAR_FIELD, ENCODING_ROW_STORE, FIXED_HEADER_SIZE, HAS_COLUMN_CHECKSUM,
    MICRO_BLOCK_MAGIC, MICRO_BLOCK_VERSION,
};
use crate::{Error, Result};

const RAW: u8 = 0;
const CONST: u8 = 3;

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
    pub row_store_type: u8,
    pub block_size_limit: usize,
    pub need_column_checksums: bool,
    pub max_merged_trans_version: i64,
    pub original_size_override: Option<usize>,
}

impl WriterConfig {
    pub fn new(columns: Vec<ColumnSpec>, rowkey_column_count: usize) -> Self {
        Self {
            columns,
            rowkey_column_count,
            row_store_type: ENCODING_ROW_STORE,
            block_size_limit: 2 * 1024 * 1024,
            need_column_checksums: false,
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
    Variable {
        ordinal: usize,
        extend_offset: usize,
        has_extend: bool,
    },
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
            || config.row_store_type != ENCODING_ROW_STORE
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
            if self.config.need_column_checksums && matches!(datum, OwnedDatum::Nop) {
                return Err(Error::Unsupported);
            }
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
            (OwnedDatum::Bytes(bytes), Some(length)) if bytes.len() == length => Ok(()),
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
        if self.frozen {
            return Err(Error::Frozen);
        }
        if self.rows.is_empty() {
            return Err(Error::InvalidArgument);
        }
        let extend_width = self.extend_width();
        let plans = self.plan_columns(extend_width)?;
        let var_column_count = plans
            .iter()
            .filter(|plan| matches!(plan, Plan::Variable { .. }))
            .count();
        let row_extend_bytes = plans
            .iter()
            .map(|plan| match plan {
                Plan::Variable { has_extend, .. } if *has_extend => extend_width,
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
            if spec.fixed_length.is_some() {
                let bit_width = if spec.enable_bit_packing {
                    let mut max_value = 0u64;
                    for row in &self.rows {
                        if let OwnedDatum::UInt(value) = row[column_id] {
                            max_value = max_value.max(value);
                        } else if !matches!(row[column_id], OwnedDatum::Null | OwnedDatum::Nop) {
                            return Err(Error::InvalidArgument);
                        }
                    }
                    Some((64 - max_value.leading_zeros() as usize).max(1))
                } else {
                    None
                };
                plans.push(Plan::Fixed {
                    has_extend,
                    bit_width,
                });
            } else {
                let extend_offset = var_extend_offset;
                if has_extend {
                    var_extend_offset += extend_width;
                }
                plans.push(Plan::Variable {
                    ordinal: var_ordinal,
                    extend_offset,
                    has_extend,
                });
                var_ordinal += 1;
            }
        }
        Ok(plans)
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
                            if let OwnedDatum::UInt(value) = row[column_id] {
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
                    if *ordinal + 1
                        == plans
                            .iter()
                            .filter(|plan| matches!(plan, Plan::Variable { .. }))
                            .count()
                    {
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
            .filter(|(_, plan)| matches!(plan, Plan::Variable { .. }))
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
                .map(|(column_id, _)| datum_len(&row[*column_id], &self.config.columns[*column_id]))
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
                let Plan::Variable {
                    extend_offset,
                    has_extend,
                    ..
                } = plan
                else {
                    return Err(Error::InvalidData);
                };
                if *has_extend {
                    write_bits(
                        output,
                        *extend_offset,
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
                    append_datum_bytes_at(
                        &row[*column_id],
                        &self.config.columns[*column_id],
                        output,
                        data_offset + value_offset,
                    )?;
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
        } | ALL_LOB_IN_ROW;
        let mut header = Header {
            header_size,
            column_count: columns.len(),
            rowkey_column_count: self.config.rowkey_column_count,
            flags,
            row_count: self.rows.len(),
            row_store_type: self.config.row_store_type,
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
        block[20] = self.config.row_store_type;
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
                let checksum = self.column_checksum(column)?;
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
        OwnedDatum::Bytes(bytes) => Ok(bytes.len()),
        OwnedDatum::UInt(_) => spec.fixed_length.ok_or(Error::InvalidArgument),
    }
}

fn append_datum_bytes(datum: &OwnedDatum, spec: &ColumnSpec, output: &mut Vec<u8>) -> Result<()> {
    match datum {
        OwnedDatum::Null | OwnedDatum::Nop => Ok(()),
        OwnedDatum::Bytes(bytes) => {
            output.extend_from_slice(bytes);
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
    use super::{AppendOutcome, BlockWriter, ColumnSpec, OwnedDatum, WriterConfig};
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
                _ => OwnedDatum::Bytes(format!("value-{row:03}").into_bytes()),
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
            assert_eq!(block.cell(row as usize, 0).unwrap().as_u64().unwrap(), row);
            match row % 3 {
                0 => assert_eq!(block.cell(row as usize, 1).unwrap(), Cell::Null),
                1 => assert_eq!(block.cell(row as usize, 1).unwrap(), Cell::Nop),
                _ => assert_eq!(block.cell(row as usize, 1).unwrap().as_u64().unwrap(), row),
            }
            match row % 4 {
                0 => assert_eq!(block.cell(row as usize, 2).unwrap(), Cell::Null),
                1 => assert_eq!(block.cell(row as usize, 2).unwrap(), Cell::Nop),
                _ => assert_eq!(
                    block.cell(row as usize, 2).unwrap().bytes().unwrap(),
                    format!("value-{row:03}").as_bytes()
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
}
