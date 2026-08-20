// Copyright (c) 2025 OceanBase.
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

use std::ffi::{c_char, c_int};
use std::slice;

use crate::ffi_check::{checked_array_len, checked_bytes_len, checked_out_range, ranges_overlap};

use crate::codec::read_lenenc;

const MYSQL_TYPE_DECIMAL: u16 = 0;
const MYSQL_TYPE_TINY: u16 = 1;
const MYSQL_TYPE_SHORT: u16 = 2;
const MYSQL_TYPE_LONG: u16 = 3;
const MYSQL_TYPE_FLOAT: u16 = 4;
const MYSQL_TYPE_DOUBLE: u16 = 5;
const MYSQL_TYPE_NULL: u16 = 6;
const MYSQL_TYPE_TIMESTAMP: u16 = 7;
const MYSQL_TYPE_LONGLONG: u16 = 8;
const MYSQL_TYPE_INT24: u16 = 9;
const MYSQL_TYPE_DATE: u16 = 10;
const MYSQL_TYPE_TIME: u16 = 11;
const MYSQL_TYPE_DATETIME: u16 = 12;
const MYSQL_TYPE_YEAR: u16 = 13;
const MYSQL_TYPE_NEWDATE: u16 = 14;
const MYSQL_TYPE_VARCHAR: u16 = 15;
const MYSQL_TYPE_BIT: u16 = 16;
const MYSQL_TYPE_JSON: u16 = 245;
const MYSQL_TYPE_NEWDECIMAL: u16 = 246;
const MYSQL_TYPE_ENUM: u16 = 247;
const MYSQL_TYPE_SET: u16 = 248;
const MYSQL_TYPE_TINY_BLOB: u16 = 249;
const MYSQL_TYPE_MEDIUM_BLOB: u16 = 250;
const MYSQL_TYPE_LONG_BLOB: u16 = 251;
const MYSQL_TYPE_BLOB: u16 = 252;
const MYSQL_TYPE_VAR_STRING: u16 = 253;
const MYSQL_TYPE_STRING: u16 = 254;
const MYSQL_TYPE_GEOMETRY: u16 = 255;

const UNSIGNED_TYPE_FLAG: u8 = 0x80;

pub const NIO_MYSQL_EXECUTE_PARSE_OK: c_int = 0;
pub const NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT: c_int = -1;
pub const NIO_MYSQL_EXECUTE_PARSE_MALFORMED: c_int = -2;
pub const NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED: c_int = -3;
pub const NIO_MYSQL_EXECUTE_PARSE_CAPACITY: c_int = -4;

pub const NIO_MYSQL_EXECUTE_VALUE_NULL: u8 = 1;
pub const NIO_MYSQL_EXECUTE_VALUE_I64: u8 = 2;
pub const NIO_MYSQL_EXECUTE_VALUE_U64: u8 = 3;
pub const NIO_MYSQL_EXECUTE_VALUE_F32_BITS: u8 = 4;
pub const NIO_MYSQL_EXECUTE_VALUE_F64_BITS: u8 = 5;
pub const NIO_MYSQL_EXECUTE_VALUE_BYTES: u8 = 6;
pub const NIO_MYSQL_EXECUTE_VALUE_YEAR: u8 = 7;
pub const NIO_MYSQL_EXECUTE_VALUE_DATE: u8 = 8;
pub const NIO_MYSQL_EXECUTE_VALUE_DATETIME: u8 = 9;
pub const NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP: u8 = 10;
pub const NIO_MYSQL_EXECUTE_VALUE_TIME: u8 = 11;
pub const NIO_MYSQL_EXECUTE_VALUE_LONG_DATA: u8 = 12;

pub const NIO_MYSQL_EXECUTE_PARAM_UNSIGNED: u8 = 1 << 0;
pub const NIO_MYSQL_EXECUTE_PARAM_NEGATIVE: u8 = 1 << 1;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct NioMysqlExecuteParamMeta {
    pub mysql_type: u16,
    pub type_flags: u8,
    pub reserved: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct NioMysqlExecuteParam {
    pub value: u64,
    pub value_off: i32,
    pub value_len: i32,
    pub days: u32,
    pub microseconds: u32,
    pub year: u16,
    pub mysql_type: u16,
    pub type_flags: u8,
    pub kind: u8,
    pub flags: u8,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
    pub reserved: [u8; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct NioMysqlExecuteParseResult {
    pub new_params_bound_flag: u8,
    pub reserved: [u8; 7],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ParseError {
    Malformed,
    ObExtension,
    Capacity,
}

fn is_ob_extension(mysql_type: u16) -> bool {
    matches!(
        mysql_type,
        160 | 161
            | 162
            | 163
            | 200
            | 201
            | 202
            | 203
            | 206
            | 207
            | 208
            | 209
            | 210
            | 211
            | 215
            | 216
            | 217
            | 218
            | 219
            | 256
            | 257
            | 258
    )
}

fn supports_long_data(mysql_type: u16) -> bool {
    matches!(
        mysql_type,
        MYSQL_TYPE_VARCHAR
            | MYSQL_TYPE_JSON
            | MYSQL_TYPE_NEWDECIMAL
            | MYSQL_TYPE_TINY_BLOB
            | MYSQL_TYPE_MEDIUM_BLOB
            | MYSQL_TYPE_LONG_BLOB
            | MYSQL_TYPE_BLOB
            | MYSQL_TYPE_VAR_STRING
            | MYSQL_TYPE_STRING
            | MYSQL_TYPE_GEOMETRY
    )
}

fn to_i32(value: usize) -> Result<i32, ParseError> {
    i32::try_from(value).map_err(|_| ParseError::Capacity)
}

fn read_fixed<const N: usize>(tail: &[u8], pos: &mut usize) -> Result<[u8; N], ParseError> {
    let end = pos.checked_add(N).ok_or(ParseError::Malformed)?;
    let bytes = tail.get(*pos..end).ok_or(ParseError::Malformed)?;
    *pos = end;
    bytes.try_into().map_err(|_| ParseError::Malformed)
}

fn descriptor(meta: NioMysqlExecuteParamMeta) -> NioMysqlExecuteParam {
    NioMysqlExecuteParam {
        mysql_type: meta.mysql_type,
        type_flags: meta.type_flags,
        flags: if meta.type_flags & UNSIGNED_TYPE_FLAG != 0 {
            NIO_MYSQL_EXECUTE_PARAM_UNSIGNED
        } else {
            0
        },
        ..NioMysqlExecuteParam::default()
    }
}

fn parse_lenenc_bytes(
    tail: &[u8],
    pos: &mut usize,
    out: &mut NioMysqlExecuteParam,
) -> Result<(), ParseError> {
    let (len, prefix_len) = read_lenenc(tail, *pos).ok_or(ParseError::Malformed)?;
    let len = usize::try_from(len).map_err(|_| ParseError::Malformed)?;
    let start = pos.checked_add(prefix_len).ok_or(ParseError::Malformed)?;
    let end = start.checked_add(len).ok_or(ParseError::Malformed)?;
    tail.get(start..end).ok_or(ParseError::Malformed)?;
    out.kind = NIO_MYSQL_EXECUTE_VALUE_BYTES;
    out.value_off = to_i32(start)?;
    out.value_len = to_i32(len)?;
    *pos = end;
    Ok(())
}

fn parse_temporal(
    tail: &[u8],
    pos: &mut usize,
    out: &mut NioMysqlExecuteParam,
) -> Result<(), ParseError> {
    let len = read_fixed::<1>(tail, pos)?[0] as usize;
    let expected = match out.mysql_type {
        MYSQL_TYPE_TIME => matches!(len, 0 | 8 | 12),
        MYSQL_TYPE_DATE | MYSQL_TYPE_NEWDATE | MYSQL_TYPE_DATETIME | MYSQL_TYPE_TIMESTAMP => {
            matches!(len, 0 | 4 | 7 | 11)
        }
        _ => false,
    };
    if !expected {
        return Err(ParseError::Malformed);
    }
    out.value_len = to_i32(len)?;
    if len == 0 {
        out.kind = match out.mysql_type {
            MYSQL_TYPE_TIME => NIO_MYSQL_EXECUTE_VALUE_TIME,
            MYSQL_TYPE_DATE | MYSQL_TYPE_NEWDATE => NIO_MYSQL_EXECUTE_VALUE_DATE,
            MYSQL_TYPE_DATETIME => NIO_MYSQL_EXECUTE_VALUE_DATETIME,
            _ => NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP,
        };
        return Ok(());
    }

    if out.mysql_type == MYSQL_TYPE_TIME {
        let bytes = read_fixed::<8>(tail, pos)?;
        if bytes[0] != 0 {
            out.flags |= NIO_MYSQL_EXECUTE_PARAM_NEGATIVE;
        }
        out.days = u32::from_le_bytes(bytes[1..5].try_into().unwrap());
        out.hour = bytes[5];
        out.minute = bytes[6];
        out.second = bytes[7];
        if len == 12 {
            out.microseconds = u32::from_le_bytes(read_fixed::<4>(tail, pos)?);
        }
        out.kind = NIO_MYSQL_EXECUTE_VALUE_TIME;
    } else {
        let date = read_fixed::<4>(tail, pos)?;
        out.year = u16::from_le_bytes(date[..2].try_into().unwrap());
        out.month = date[2];
        out.day = date[3];
        if len >= 7 {
            let time = read_fixed::<3>(tail, pos)?;
            out.hour = time[0];
            out.minute = time[1];
            out.second = time[2];
        }
        if len == 11 {
            out.microseconds = u32::from_le_bytes(read_fixed::<4>(tail, pos)?);
        }
        out.kind = match out.mysql_type {
            MYSQL_TYPE_DATE | MYSQL_TYPE_NEWDATE => NIO_MYSQL_EXECUTE_VALUE_DATE,
            MYSQL_TYPE_DATETIME => NIO_MYSQL_EXECUTE_VALUE_DATETIME,
            _ => NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP,
        };
    }
    Ok(())
}

fn parse_value(
    tail: &[u8],
    pos: &mut usize,
    meta: NioMysqlExecuteParamMeta,
    is_null: bool,
    has_long_data: bool,
) -> Result<NioMysqlExecuteParam, ParseError> {
    if meta.reserved != 0 {
        return Err(ParseError::Malformed);
    }
    let mut out = descriptor(meta);
    if has_long_data {
        if !supports_long_data(meta.mysql_type) {
            return Err(ParseError::Malformed);
        }
        out.kind = NIO_MYSQL_EXECUTE_VALUE_LONG_DATA;
        return Ok(out);
    }
    if is_null {
        out.kind = NIO_MYSQL_EXECUTE_VALUE_NULL;
        return Ok(out);
    }
    if meta.mysql_type == MYSQL_TYPE_NULL {
        return Err(ParseError::Malformed);
    }

    match meta.mysql_type {
        MYSQL_TYPE_TINY => {
            let raw = read_fixed::<1>(tail, pos)?[0];
            if out.flags & NIO_MYSQL_EXECUTE_PARAM_UNSIGNED != 0 {
                out.kind = NIO_MYSQL_EXECUTE_VALUE_U64;
                out.value = u64::from(raw);
            } else {
                out.kind = NIO_MYSQL_EXECUTE_VALUE_I64;
                out.value = (i64::from(raw as i8)) as u64;
            }
        }
        MYSQL_TYPE_SHORT => {
            let raw = u16::from_le_bytes(read_fixed::<2>(tail, pos)?);
            if out.flags & NIO_MYSQL_EXECUTE_PARAM_UNSIGNED != 0 {
                out.kind = NIO_MYSQL_EXECUTE_VALUE_U64;
                out.value = u64::from(raw);
            } else {
                out.kind = NIO_MYSQL_EXECUTE_VALUE_I64;
                out.value = (i64::from(raw as i16)) as u64;
            }
        }
        MYSQL_TYPE_LONG | MYSQL_TYPE_INT24 => {
            let raw = u32::from_le_bytes(read_fixed::<4>(tail, pos)?);
            if out.flags & NIO_MYSQL_EXECUTE_PARAM_UNSIGNED != 0 {
                out.kind = NIO_MYSQL_EXECUTE_VALUE_U64;
                out.value = u64::from(raw);
            } else {
                out.kind = NIO_MYSQL_EXECUTE_VALUE_I64;
                out.value = (i64::from(raw as i32)) as u64;
            }
        }
        MYSQL_TYPE_LONGLONG => {
            let raw = u64::from_le_bytes(read_fixed::<8>(tail, pos)?);
            out.kind = if out.flags & NIO_MYSQL_EXECUTE_PARAM_UNSIGNED != 0 {
                NIO_MYSQL_EXECUTE_VALUE_U64
            } else {
                NIO_MYSQL_EXECUTE_VALUE_I64
            };
            out.value = raw;
        }
        MYSQL_TYPE_FLOAT => {
            out.kind = NIO_MYSQL_EXECUTE_VALUE_F32_BITS;
            out.value = u64::from(u32::from_le_bytes(read_fixed::<4>(tail, pos)?));
        }
        MYSQL_TYPE_DOUBLE => {
            out.kind = NIO_MYSQL_EXECUTE_VALUE_F64_BITS;
            out.value = u64::from_le_bytes(read_fixed::<8>(tail, pos)?);
        }
        MYSQL_TYPE_YEAR => {
            out.kind = NIO_MYSQL_EXECUTE_VALUE_YEAR;
            out.value = u64::from(u16::from_le_bytes(read_fixed::<2>(tail, pos)?));
        }
        MYSQL_TYPE_DATE | MYSQL_TYPE_TIME | MYSQL_TYPE_DATETIME | MYSQL_TYPE_TIMESTAMP => {
            parse_temporal(tail, pos, &mut out)?
        }
        MYSQL_TYPE_NEWDATE => return Err(ParseError::Malformed),
        MYSQL_TYPE_DECIMAL
        | MYSQL_TYPE_VARCHAR
        | MYSQL_TYPE_BIT
        | MYSQL_TYPE_JSON
        | MYSQL_TYPE_NEWDECIMAL
        | MYSQL_TYPE_ENUM
        | MYSQL_TYPE_SET
        | MYSQL_TYPE_TINY_BLOB
        | MYSQL_TYPE_MEDIUM_BLOB
        | MYSQL_TYPE_LONG_BLOB
        | MYSQL_TYPE_BLOB
        | MYSQL_TYPE_VAR_STRING
        | MYSQL_TYPE_STRING
        | MYSQL_TYPE_GEOMETRY => parse_lenenc_bytes(tail, pos, &mut out)?,
        _ => return Err(ParseError::Malformed),
    }
    Ok(out)
}

fn parse(
    tail: &[u8],
    param_count: usize,
    cached: &[NioMysqlExecuteParamMeta],
    long_data: &[u8],
) -> Result<(Vec<NioMysqlExecuteParam>, NioMysqlExecuteParseResult), ParseError> {
    if param_count == 0 {
        // mysqlnd 5.x emits the zero-valued new-params-bound flag even when
        // COM_STMT_PREPARE reported no parameters. MySQL and OceanBase accept
        // that compatibility padding, so accept exactly that one extra byte.
        if (!tail.is_empty() && tail != [0]) || !cached.is_empty() || !long_data.is_empty() {
            return Err(ParseError::Malformed);
        }
        return Ok((Vec::new(), NioMysqlExecuteParseResult::default()));
    }
    let bitmap_len = param_count.checked_add(7).ok_or(ParseError::Capacity)? / 8;
    let flag_off = bitmap_len;
    let new_flag = *tail.get(flag_off).ok_or(ParseError::Malformed)?;
    let mut pos = flag_off.checked_add(1).ok_or(ParseError::Capacity)?;

    let mut metas = Vec::new();
    metas
        .try_reserve_exact(param_count)
        .map_err(|_| ParseError::Capacity)?;
    if new_flag == 1 {
        let type_bytes = param_count.checked_mul(2).ok_or(ParseError::Capacity)?;
        let type_end = pos.checked_add(type_bytes).ok_or(ParseError::Capacity)?;
        let table = tail.get(pos..type_end).ok_or(ParseError::Malformed)?;
        for pair in table.chunks_exact(2) {
            metas.push(NioMysqlExecuteParamMeta {
                mysql_type: u16::from(pair[0]),
                type_flags: pair[1],
                reserved: 0,
            });
        }
        pos = type_end;
    } else {
        if cached.len() != param_count || cached.iter().any(|meta| meta.reserved != 0) {
            return Err(ParseError::Malformed);
        }
        metas.extend_from_slice(cached);
    }
    if metas.iter().any(|meta| is_ob_extension(meta.mysql_type)) {
        return Err(ParseError::ObExtension);
    }
    if !long_data.is_empty()
        && (long_data.len() != param_count || long_data.iter().any(|value| *value > 1))
    {
        return Err(ParseError::Malformed);
    }

    let bitmap = tail.get(..bitmap_len).ok_or(ParseError::Malformed)?;
    let mut params = Vec::new();
    params
        .try_reserve_exact(param_count)
        .map_err(|_| ParseError::Capacity)?;
    for (index, meta) in metas.into_iter().enumerate() {
        let is_null = bitmap[index / 8] & (1 << (index % 8)) != 0;
        let has_long_data = !long_data.is_empty() && long_data[index] != 0;
        params.push(parse_value(tail, &mut pos, meta, is_null, has_long_data)?);
    }
    if pos != tail.len() {
        return Err(ParseError::Malformed);
    }

    let result = NioMysqlExecuteParseResult {
        new_params_bound_flag: new_flag,
        reserved: [0; 7],
    };
    Ok((params, result))
}

#[no_mangle]
pub unsafe extern "C" fn nio_parse_mysql_execute_params(
    tail: *const c_char,
    tail_len: i64,
    param_count: i64,
    cached_meta: *const NioMysqlExecuteParamMeta,
    cached_count: i64,
    long_data: *const u8,
    long_data_count: i64,
    out_params: *mut NioMysqlExecuteParam,
    out_capacity: i64,
    out_result: *mut NioMysqlExecuteParseResult,
) -> c_int {
    let (tail_len, param_count, out_capacity) = match (
        checked_bytes_len(tail, tail_len),
        usize::try_from(param_count),
        usize::try_from(out_capacity),
    ) {
        (Some(a), Ok(b), Ok(c)) => (a, b, c),
        _ => return NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT,
    };
    let (cached_count, long_data_count) = match (
        checked_array_len(cached_meta, cached_count),
        checked_array_len(long_data, long_data_count),
    ) {
        (Some(a), Some(b)) => (a, b),
        _ => return NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT,
    };
    if out_capacity < param_count
        || checked_array_len(out_params.cast_const(), out_capacity as i64).is_none()
        || (out_capacity != 0 && out_params.is_null())
    {
        return NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT;
    }
    let out_result_range = match checked_out_range(out_result) {
        Some(range) => range,
        None => return NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT,
    };
    let out_params_range = (
        out_params as usize,
        (out_params as usize) + out_capacity * std::mem::size_of::<NioMysqlExecuteParam>(),
    );
    let tail_range = (tail as usize, (tail as usize) + tail_len);
    if ranges_overlap(out_result_range, tail_range)
        || ranges_overlap(out_params_range, tail_range)
        || ranges_overlap(out_result_range, out_params_range)
    {
        return NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT;
    }
    let tail = if tail_len == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(tail.cast::<u8>(), tail_len) }
    };
    let cached = if cached_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(cached_meta, cached_count) }
    };
    let long_data = if long_data_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(long_data, long_data_count) }
    };

    let (params, result) = match parse(tail, param_count, cached, long_data) {
        Ok(parsed) => parsed,
        Err(ParseError::Malformed) => return NIO_MYSQL_EXECUTE_PARSE_MALFORMED,
        Err(ParseError::ObExtension) => return NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED,
        Err(ParseError::Capacity) => return NIO_MYSQL_EXECUTE_PARSE_CAPACITY,
    };
    if !params.is_empty() {
        unsafe { out_params.copy_from_nonoverlapping(params.as_ptr(), params.len()) };
    }
    unsafe { out_result.write(result) };
    NIO_MYSQL_EXECUTE_PARSE_OK
}
