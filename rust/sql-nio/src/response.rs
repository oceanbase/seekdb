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

use std::ptr;

use crate::packet::{HEADER_SIZE, MAX_LOGICAL_PAYLOAD, MAX_PAYLOAD};
use crate::{
    NIO_MYSQL_CELL_BIT, NIO_MYSQL_CELL_DATE, NIO_MYSQL_CELL_DATETIME, NIO_MYSQL_CELL_F32_BITS,
    NIO_MYSQL_CELL_F64_BITS, NIO_MYSQL_CELL_I16, NIO_MYSQL_CELL_I32, NIO_MYSQL_CELL_I64,
    NIO_MYSQL_CELL_I8, NIO_MYSQL_CELL_LEGACY_LENENC_NULL, NIO_MYSQL_CELL_LENENC_BYTES,
    NIO_MYSQL_CELL_NULL, NIO_MYSQL_CELL_TIME, NIO_MYSQL_CELL_TIME_NEGATIVE, NIO_MYSQL_CELL_YEAR,
    NIO_MYSQL_ROW_BINARY, NIO_MYSQL_ROW_TEXT,
};

pub const MAX_LENENC_SIZE: usize = 9;

const ERROR_FIXED_SIZE: usize = 1 + 2 + 1 + 5;
const AUTH_SWITCH_FIXED_SIZE: usize = 1 + 1;
const FIELD_BASE_FIXED_SIZE: usize = 17;
pub const MYSQL_TYPE_COMPLEX: u32 = 160;
const MYSQL_TYPE_NOT_DEFINED: u32 = 65_535;

pub use crate::capability::{CLIENT_PROTOCOL_41, CLIENT_SESSION_TRACK, CLIENT_TRANSACTIONS};
pub const SERVER_SESSION_STATE_CHANGED: u16 = 1 << 14;

const SESSION_TRACK_SYSTEM_VARIABLES: u8 = 0x00;
const SESSION_TRACK_SCHEMA: u8 = 0x01;
const SESSION_TRACK_STATE_CHANGE: u8 = 0x02;
const LEGACY_USER_VAR_SEPARATOR: &[u8] = b"__NULL";

pub fn encode_result_header_payload(field_count: u64, out: &mut [u8; MAX_LENENC_SIZE]) -> usize {
    if field_count < 251 {
        out[0] = field_count as u8;
        1
    } else if field_count < 0x1_0000 {
        out[0] = 0xfc;
        out[1..3].copy_from_slice(&(field_count as u16).to_le_bytes());
        3
    } else if field_count < 0x100_0000 {
        out[0] = 0xfd;
        out[1..4].copy_from_slice(&(field_count as u32).to_le_bytes()[..3]);
        4
    } else if field_count < u64::MAX {
        out[0] = 0xfe;
        out[1..9].copy_from_slice(&field_count.to_le_bytes());
        9
    } else {
        out[0] = 0xfb;
        1
    }
}

pub fn encode_eof_payload(field_count: u8, warnings: u16, status_flags: u16) -> [u8; 5] {
    let warnings = warnings.to_le_bytes();
    let status_flags = status_flags.to_le_bytes();
    [
        field_count,
        warnings[0],
        warnings[1],
        status_flags[0],
        status_flags[1],
    ]
}

pub fn encode_prepare_ok_payload(
    statement_id: u32,
    column_count: u16,
    parameter_count: u16,
    warnings: u16,
) -> [u8; 12] {
    let statement_id = statement_id.to_le_bytes();
    let column_count = column_count.to_le_bytes();
    let parameter_count = parameter_count.to_le_bytes();
    let warnings = warnings.to_le_bytes();
    [
        0,
        statement_id[0],
        statement_id[1],
        statement_id[2],
        statement_id[3],
        column_count[0],
        column_count[1],
        parameter_count[0],
        parameter_count[1],
        0,
        warnings[0],
        warnings[1],
    ]
}

pub fn error_payload_len(message_len: usize) -> Option<usize> {
    ERROR_FIXED_SIZE.checked_add(message_len)
}

pub fn encode_error_payload(
    error_code: u16,
    sql_state: &[u8; 5],
    message: &[u8],
    out: &mut [u8],
) -> Option<usize> {
    let payload_len = error_payload_len(message.len())?;
    if out.len() < payload_len {
        return None;
    }

    out[0] = 0xff;
    out[1..3].copy_from_slice(&error_code.to_le_bytes());
    out[3] = b'#';
    out[4..9].copy_from_slice(sql_state);
    out[9..payload_len].copy_from_slice(message);
    Some(payload_len)
}

pub fn auth_switch_payload_len(plugin_name_len: usize, scramble_len: usize) -> Option<usize> {
    AUTH_SWITCH_FIXED_SIZE
        .checked_add(plugin_name_len)?
        .checked_add(scramble_len)
}

pub fn encode_auth_switch_payload(
    plugin_name: &[u8],
    scramble: &[u8],
    out: &mut [u8],
) -> Option<usize> {
    let payload_len = auth_switch_payload_len(plugin_name.len(), scramble.len())?;
    if out.len() < payload_len {
        return None;
    }

    out[0] = 0xfe;
    let plugin_end = 1 + plugin_name.len();
    out[1..plugin_end].copy_from_slice(plugin_name);
    out[plugin_end] = 0;
    out[plugin_end + 1..payload_len].copy_from_slice(scramble);
    Some(payload_len)
}

pub fn local_infile_payload_len(filename_len: usize) -> Option<usize> {
    1usize.checked_add(filename_len)
}

pub fn encode_local_infile_payload(filename: &[u8], out: &mut [u8]) -> Option<usize> {
    let payload_len = local_infile_payload_len(filename.len())?;
    if out.len() < payload_len {
        return None;
    }

    out[0] = 0xfb;
    out[1..payload_len].copy_from_slice(filename);
    Some(payload_len)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FieldPayloadMeta {
    pub schema_len: usize,
    pub table_len: usize,
    pub org_table_len: usize,
    pub name_len: usize,
    pub org_name_len: usize,
    pub type_owner_len: usize,
    pub type_name_len: usize,
    pub column_length: u32,
    pub charset: u16,
    pub flags: u16,
    pub field_type: u32,
    pub default_type: u32,
    pub decimals: u8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FieldPayloadPlan {
    pub payload_len: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FieldPayloadBytes<'a> {
    pub schema: &'a [u8],
    pub table: &'a [u8],
    pub org_table: &'a [u8],
    pub name: &'a [u8],
    pub org_name: &'a [u8],
    pub type_owner: &'a [u8],
    pub type_name: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RowCellMeta {
    pub bytes_len: usize,
    pub value: u64,
    pub days: u32,
    pub microseconds: u32,
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
    pub kind: u8,
    pub flags: u8,
    pub bit_len: u8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RowCell<'a> {
    pub meta: RowCellMeta,
    pub bytes: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RowPayloadMeta {
    pub protocol: u32,
    pub cell_count: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RowPayloadPlan {
    pub payload_len: usize,
    pub bitmap_len: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OkKvLengths {
    pub key_len: usize,
    pub value_len: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OkKv<'a> {
    pub key: &'a [u8],
    pub value: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OkPayloadMeta {
    pub affected_rows: u64,
    pub last_insert_id: u64,
    pub capability_flags: u32,
    pub status_flags: u16,
    pub warnings: u16,
    pub use_standard_serialize: bool,
    pub schema_changed: bool,
    pub state_changed: bool,
    pub message_len: usize,
    pub changed_schema_len: usize,
    pub system_var_count: usize,
    pub user_var_count: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OkPayloadPlan {
    pub payload_len: usize,
    pub state_info_len: usize,
    pub legacy_vars_len: usize,
}

fn checked_add_assign(total: &mut usize, value: usize) -> Option<()> {
    *total = total.checked_add(value)?;
    Some(())
}

fn lenenc_int_len(value: u64) -> usize {
    if value < 251 || value == u64::MAX {
        1
    } else if value < 0x1_0000 {
        3
    } else if value < 0x100_0000 {
        4
    } else {
        9
    }
}

fn lenenc_usize_len(value: usize) -> Option<usize> {
    Some(lenenc_int_len(u64::try_from(value).ok()?))
}

fn lenenc_bytes_len(value_len: usize) -> Option<usize> {
    lenenc_usize_len(value_len)?.checked_add(value_len)
}

fn bit_bytes_len(bit_len: u8) -> Option<usize> {
    (1..=64)
        .contains(&bit_len)
        .then_some(usize::from(bit_len).checked_add(7)?.checked_div(8)?)
}

fn date_cell_wire_len(meta: &RowCellMeta) -> usize {
    if meta.year != 0 || meta.month != 0 || meta.day != 0 {
        1 + 4
    } else {
        1
    }
}

fn datetime_cell_wire_len(meta: &RowCellMeta) -> usize {
    if meta.microseconds != 0 {
        1 + 11
    } else if meta.hour != 0 || meta.minute != 0 || meta.second != 0 {
        1 + 7
    } else {
        date_cell_wire_len(meta)
    }
}

fn time_cell_wire_len(meta: &RowCellMeta) -> usize {
    if meta.microseconds != 0 {
        1 + 12
    } else if meta.days != 0 || meta.hour != 0 || meta.minute != 0 || meta.second != 0 {
        1 + 8
    } else {
        1
    }
}

fn row_cell_wire_len(protocol: u32, meta: &RowCellMeta) -> Option<usize> {
    if meta.kind != NIO_MYSQL_CELL_LENENC_BYTES && meta.bytes_len != 0 {
        return None;
    }
    if meta.kind != NIO_MYSQL_CELL_BIT && meta.bit_len != 0 {
        return None;
    }
    if meta.kind == NIO_MYSQL_CELL_TIME {
        if meta.flags & !NIO_MYSQL_CELL_TIME_NEGATIVE != 0 {
            return None;
        }
    } else if meta.flags != 0 {
        return None;
    }

    match protocol {
        NIO_MYSQL_ROW_TEXT => match meta.kind {
            NIO_MYSQL_CELL_NULL | NIO_MYSQL_CELL_LEGACY_LENENC_NULL => Some(1),
            NIO_MYSQL_CELL_LENENC_BYTES => lenenc_bytes_len(meta.bytes_len),
            NIO_MYSQL_CELL_BIT => {
                let bytes_len = bit_bytes_len(meta.bit_len)?;
                lenenc_bytes_len(bytes_len)
            }
            _ => None,
        },
        NIO_MYSQL_ROW_BINARY => match meta.kind {
            NIO_MYSQL_CELL_NULL => Some(0),
            NIO_MYSQL_CELL_LEGACY_LENENC_NULL => Some(1),
            NIO_MYSQL_CELL_LENENC_BYTES => lenenc_bytes_len(meta.bytes_len),
            NIO_MYSQL_CELL_I8 => Some(1),
            NIO_MYSQL_CELL_I16 => Some(2),
            NIO_MYSQL_CELL_I32 | NIO_MYSQL_CELL_F32_BITS => Some(4),
            NIO_MYSQL_CELL_I64 | NIO_MYSQL_CELL_F64_BITS => Some(8),
            NIO_MYSQL_CELL_YEAR => Some(2),
            NIO_MYSQL_CELL_DATE => Some(date_cell_wire_len(meta)),
            NIO_MYSQL_CELL_DATETIME => Some(datetime_cell_wire_len(meta)),
            NIO_MYSQL_CELL_TIME => Some(time_cell_wire_len(meta)),
            NIO_MYSQL_CELL_BIT => {
                let bytes_len = bit_bytes_len(meta.bit_len)?;
                lenenc_bytes_len(bytes_len)
            }
            _ => None,
        },
        _ => None,
    }
}

pub fn plan_row_payload<F>(meta: &RowPayloadMeta, mut cell_meta: F) -> Option<RowPayloadPlan>
where
    F: FnMut(usize) -> Option<RowCellMeta>,
{
    if meta.cell_count == 0
        || (meta.protocol != NIO_MYSQL_ROW_TEXT && meta.protocol != NIO_MYSQL_ROW_BINARY)
    {
        return None;
    }

    let bitmap_len = if meta.protocol == NIO_MYSQL_ROW_BINARY {
        meta.cell_count.checked_add(9)?.checked_div(8)?
    } else {
        0
    };
    let mut payload_len = if meta.protocol == NIO_MYSQL_ROW_BINARY {
        1usize.checked_add(bitmap_len)?
    } else {
        0
    };
    for index in 0..meta.cell_count {
        checked_add_assign(
            &mut payload_len,
            row_cell_wire_len(meta.protocol, &cell_meta(index)?)?,
        )?;
    }
    (payload_len <= MAX_LOGICAL_PAYLOAD).then_some(RowPayloadPlan {
        payload_len,
        bitmap_len,
    })
}

pub fn plan_field_payload(meta: &FieldPayloadMeta) -> Option<FieldPayloadPlan> {
    let mut payload_len = FIELD_BASE_FIXED_SIZE;
    for value_len in [
        meta.schema_len,
        meta.table_len,
        meta.org_table_len,
        meta.name_len,
        meta.org_name_len,
    ] {
        checked_add_assign(&mut payload_len, lenenc_bytes_len(value_len)?)?;
    }

    if meta.field_type == MYSQL_TYPE_COMPLEX {
        checked_add_assign(&mut payload_len, lenenc_bytes_len(meta.type_owner_len)?)?;
        checked_add_assign(&mut payload_len, lenenc_bytes_len(meta.type_name_len)?)?;
        checked_add_assign(&mut payload_len, 1)?;
        if meta.type_name_len == 0 {
            checked_add_assign(&mut payload_len, 1)?;
        }
    } else if meta.default_type != MYSQL_TYPE_NOT_DEFINED {
        checked_add_assign(&mut payload_len, 3)?;
    }

    Some(FieldPayloadPlan { payload_len })
}

fn kv_encoded_len(lengths: OkKvLengths) -> Option<usize> {
    let mut len = lenenc_usize_len(lengths.key_len)?;
    checked_add_assign(&mut len, lengths.key_len)?;
    checked_add_assign(&mut len, lenenc_usize_len(lengths.value_len)?)?;
    checked_add_assign(&mut len, lengths.value_len)?;
    Some(len)
}

pub fn plan_ok_payload<FS, FU>(
    meta: &OkPayloadMeta,
    mut system_var_lengths: FS,
    mut user_var_lengths: FU,
) -> Option<OkPayloadPlan>
where
    FS: FnMut(usize) -> Option<OkKvLengths>,
    FU: FnMut(usize) -> Option<OkKvLengths>,
{
    let protocol_41 = meta.capability_flags & CLIENT_PROTOCOL_41 != 0;
    let transactions = meta.capability_flags & CLIENT_TRANSACTIONS != 0;
    if meta.capability_flags == 0 || (!protocol_41 && !transactions) {
        return None;
    }

    let mut standard_vars_len = 0usize;
    let mut legacy_system_vars_len = 0usize;
    for index in 0..meta.system_var_count {
        let kv_len = kv_encoded_len(system_var_lengths(index)?)?;
        checked_add_assign(&mut legacy_system_vars_len, kv_len)?;
        checked_add_assign(&mut standard_vars_len, 1)?;
        checked_add_assign(&mut standard_vars_len, lenenc_usize_len(kv_len)?)?;
        checked_add_assign(&mut standard_vars_len, kv_len)?;
    }

    let mut legacy_user_vars_len = 0usize;
    for index in 0..meta.user_var_count {
        checked_add_assign(
            &mut legacy_user_vars_len,
            kv_encoded_len(user_var_lengths(index)?)?,
        )?;
    }

    let mut legacy_vars_len = legacy_system_vars_len;
    if meta.user_var_count != 0 {
        checked_add_assign(
            &mut legacy_vars_len,
            kv_encoded_len(OkKvLengths {
                key_len: LEGACY_USER_VAR_SEPARATOR.len(),
                value_len: LEGACY_USER_VAR_SEPARATOR.len(),
            })?,
        )?;
        checked_add_assign(&mut legacy_vars_len, legacy_user_vars_len)?;
    }

    let session_track = meta.capability_flags & CLIENT_SESSION_TRACK != 0;
    let status_has_state = meta.status_flags & SERVER_SESSION_STATE_CHANGED != 0;
    let mut state_info_len = 0usize;
    if session_track && status_has_state {
        if meta.system_var_count != 0 || meta.user_var_count != 0 {
            if meta.use_standard_serialize {
                checked_add_assign(&mut state_info_len, standard_vars_len)?;
            } else {
                checked_add_assign(&mut state_info_len, 1)?;
                checked_add_assign(&mut state_info_len, lenenc_usize_len(legacy_vars_len)?)?;
                checked_add_assign(&mut state_info_len, legacy_vars_len)?;
            }
        }

        if meta.schema_changed {
            let mut schema_item_len = lenenc_usize_len(meta.changed_schema_len)?;
            checked_add_assign(&mut schema_item_len, meta.changed_schema_len)?;
            checked_add_assign(&mut state_info_len, 1)?;
            checked_add_assign(&mut state_info_len, lenenc_usize_len(schema_item_len)?)?;
            checked_add_assign(&mut state_info_len, schema_item_len)?;
        }

        if meta.state_changed {
            checked_add_assign(&mut state_info_len, 4)?;
        }
    }

    let mut payload_len = 1usize;
    checked_add_assign(&mut payload_len, lenenc_int_len(meta.affected_rows))?;
    checked_add_assign(&mut payload_len, lenenc_int_len(meta.last_insert_id))?;
    checked_add_assign(&mut payload_len, 2)?;
    if protocol_41 {
        checked_add_assign(&mut payload_len, 2)?;
    }

    if session_track {
        if meta.use_standard_serialize {
            if meta.message_len != 0 || status_has_state {
                checked_add_assign(&mut payload_len, lenenc_usize_len(meta.message_len)?)?;
                checked_add_assign(&mut payload_len, meta.message_len)?;
            }
        } else {
            let legacy_message_len = if meta.message_len == 0 {
                0
            } else {
                meta.message_len.checked_add(1)?
            };
            checked_add_assign(&mut payload_len, lenenc_usize_len(legacy_message_len)?)?;
            checked_add_assign(&mut payload_len, legacy_message_len)?;
        }

        if status_has_state {
            checked_add_assign(&mut payload_len, lenenc_usize_len(state_info_len)?)?;
            checked_add_assign(&mut payload_len, state_info_len)?;
        }
    } else if meta.message_len != 0 {
        if meta.use_standard_serialize {
            checked_add_assign(&mut payload_len, lenenc_usize_len(meta.message_len)?)?;
            checked_add_assign(&mut payload_len, meta.message_len)?;
        } else {
            checked_add_assign(&mut payload_len, 1)?;
            checked_add_assign(&mut payload_len, meta.message_len)?;
        }
    }

    Some(OkPayloadPlan {
        payload_len,
        state_info_len,
        legacy_vars_len,
    })
}

struct PayloadWriter<'a> {
    out: &'a mut [u8],
    pos: usize,
}

impl<'a> PayloadWriter<'a> {
    fn new(out: &'a mut [u8]) -> Self {
        Self { out, pos: 0 }
    }

    fn put_u8(&mut self, value: u8) -> Option<()> {
        *self.out.get_mut(self.pos)? = value;
        self.pos += 1;
        Some(())
    }

    fn put_u16_le(&mut self, value: u16) -> Option<()> {
        self.put_bytes(&value.to_le_bytes())
    }

    fn put_u32_le(&mut self, value: u32) -> Option<()> {
        self.put_bytes(&value.to_le_bytes())
    }

    fn put_u64_le(&mut self, value: u64) -> Option<()> {
        self.put_bytes(&value.to_le_bytes())
    }

    fn put_zeroes(&mut self, len: usize) -> Option<()> {
        let end = self.pos.checked_add(len)?;
        self.out.get_mut(self.pos..end)?.fill(0);
        self.pos = end;
        Some(())
    }

    fn or_u8_at(&mut self, at: usize, value: u8) -> Option<()> {
        *self.out.get_mut(at)? |= value;
        Some(())
    }

    fn put_bytes(&mut self, value: &[u8]) -> Option<()> {
        let end = self.pos.checked_add(value.len())?;
        self.out.get_mut(self.pos..end)?.copy_from_slice(value);
        self.pos = end;
        Some(())
    }

    fn put_lenenc_int(&mut self, value: u64) -> Option<()> {
        if value < 251 {
            self.put_u8(value as u8)
        } else if value < 0x1_0000 {
            self.put_u8(0xfc)?;
            self.put_u16_le(value as u16)
        } else if value < 0x100_0000 {
            self.put_u8(0xfd)?;
            self.put_bytes(&(value as u32).to_le_bytes()[..3])
        } else if value < u64::MAX {
            self.put_u8(0xfe)?;
            self.put_bytes(&value.to_le_bytes())
        } else {
            self.put_u8(0xfb)
        }
    }

    fn put_lenenc_usize(&mut self, value: usize) -> Option<()> {
        self.put_lenenc_int(u64::try_from(value).ok()?)
    }

    fn put_lenenc_bytes(&mut self, value: &[u8]) -> Option<()> {
        self.put_lenenc_usize(value.len())?;
        self.put_bytes(value)
    }

    fn put_kv(&mut self, kv: OkKv<'_>) -> Option<()> {
        self.put_lenenc_bytes(kv.key)?;
        self.put_lenenc_bytes(kv.value)
    }
}

pub(crate) fn encode_field_payload_planned(
    meta: &FieldPayloadMeta,
    plan: FieldPayloadPlan,
    bytes: FieldPayloadBytes<'_>,
    out: &mut [u8],
) -> Option<usize> {
    if out.len() < plan.payload_len {
        return None;
    }
    let mut writer = PayloadWriter::new(&mut out[..plan.payload_len]);
    writer.put_lenenc_bytes(b"def")?;
    writer.put_lenenc_bytes(bytes.schema)?;
    writer.put_lenenc_bytes(bytes.table)?;
    writer.put_lenenc_bytes(bytes.org_table)?;
    writer.put_lenenc_bytes(bytes.name)?;
    writer.put_lenenc_bytes(bytes.org_name)?;
    writer.put_u8(0x0c)?;
    writer.put_u16_le(meta.charset)?;
    writer.put_u32_le(meta.column_length)?;
    writer.put_u8(meta.field_type as u8)?;
    writer.put_u16_le(meta.flags)?;
    writer.put_u8(meta.decimals)?;
    writer.put_u16_le(0)?;

    if meta.field_type == MYSQL_TYPE_COMPLEX {
        writer.put_lenenc_bytes(bytes.type_owner)?;
        writer.put_lenenc_bytes(bytes.type_name)?;
        writer.put_lenenc_int(u64::MAX)?;
        if bytes.type_name.is_empty() {
            writer.put_u8(meta.default_type as u8)?;
        }
    } else if meta.default_type != MYSQL_TYPE_NOT_DEFINED {
        writer.put_u16_le(0)?;
        writer.put_u8(0)?;
    }

    (writer.pos == plan.payload_len).then_some(writer.pos)
}

fn encode_bit_cell(writer: &mut PayloadWriter<'_>, meta: &RowCellMeta) -> Option<()> {
    let bytes_len = bit_bytes_len(meta.bit_len)?;
    writer.put_lenenc_usize(bytes_len)?;
    let bytes = meta.value.to_be_bytes();
    writer.put_bytes(&bytes[bytes.len() - bytes_len..])
}

fn encode_date_cell(writer: &mut PayloadWriter<'_>, meta: &RowCellMeta) -> Option<()> {
    let has_date = meta.year != 0 || meta.month != 0 || meta.day != 0;
    writer.put_u8(if has_date { 4 } else { 0 })?;
    if has_date {
        writer.put_u16_le(meta.year)?;
        writer.put_u8(meta.month)?;
        writer.put_u8(meta.day)?;
    }
    Some(())
}

fn encode_datetime_cell(writer: &mut PayloadWriter<'_>, meta: &RowCellMeta) -> Option<()> {
    let wire_len = datetime_cell_wire_len(meta) - 1;
    writer.put_u8(wire_len as u8)?;
    if wire_len != 0 {
        writer.put_u16_le(meta.year)?;
        writer.put_u8(meta.month)?;
        writer.put_u8(meta.day)?;
    }
    if wire_len > 4 {
        writer.put_u8(meta.hour)?;
        writer.put_u8(meta.minute)?;
        writer.put_u8(meta.second)?;
    }
    if wire_len > 7 {
        writer.put_u32_le(meta.microseconds)?;
    }
    Some(())
}

fn encode_time_cell(writer: &mut PayloadWriter<'_>, meta: &RowCellMeta) -> Option<()> {
    let wire_len = time_cell_wire_len(meta) - 1;
    writer.put_u8(wire_len as u8)?;
    if wire_len != 0 {
        writer.put_u8(u8::from(meta.flags & NIO_MYSQL_CELL_TIME_NEGATIVE != 0))?;
        writer.put_u32_le(meta.days)?;
        writer.put_u8(meta.hour)?;
        writer.put_u8(meta.minute)?;
        writer.put_u8(meta.second)?;
    }
    if wire_len > 8 {
        writer.put_u32_le(meta.microseconds)?;
    }
    Some(())
}

fn encode_row_cell(
    protocol: u32,
    index: usize,
    cell: RowCell<'_>,
    writer: &mut PayloadWriter<'_>,
) -> Option<()> {
    match protocol {
        NIO_MYSQL_ROW_TEXT => match cell.meta.kind {
            NIO_MYSQL_CELL_NULL | NIO_MYSQL_CELL_LEGACY_LENENC_NULL => writer.put_u8(0xfb),
            NIO_MYSQL_CELL_LENENC_BYTES => writer.put_lenenc_bytes(cell.bytes),
            NIO_MYSQL_CELL_BIT => encode_bit_cell(writer, &cell.meta),
            _ => None,
        },
        NIO_MYSQL_ROW_BINARY => match cell.meta.kind {
            NIO_MYSQL_CELL_NULL => {
                let bit = index.checked_add(2)?;
                writer.or_u8_at(
                    1usize.checked_add(bit / 8)?,
                    1u8.checked_shl((bit % 8) as u32)?,
                )
            }
            NIO_MYSQL_CELL_LEGACY_LENENC_NULL => writer.put_u8(0xfb),
            NIO_MYSQL_CELL_LENENC_BYTES => writer.put_lenenc_bytes(cell.bytes),
            NIO_MYSQL_CELL_I8 => writer.put_u8(cell.meta.value as u8),
            NIO_MYSQL_CELL_I16 => writer.put_u16_le(cell.meta.value as u16),
            NIO_MYSQL_CELL_I32 | NIO_MYSQL_CELL_F32_BITS => {
                writer.put_u32_le(cell.meta.value as u32)
            }
            NIO_MYSQL_CELL_I64 | NIO_MYSQL_CELL_F64_BITS => writer.put_u64_le(cell.meta.value),
            NIO_MYSQL_CELL_YEAR => writer.put_u16_le(cell.meta.year),
            NIO_MYSQL_CELL_DATE => encode_date_cell(writer, &cell.meta),
            NIO_MYSQL_CELL_DATETIME => encode_datetime_cell(writer, &cell.meta),
            NIO_MYSQL_CELL_TIME => encode_time_cell(writer, &cell.meta),
            NIO_MYSQL_CELL_BIT => encode_bit_cell(writer, &cell.meta),
            _ => None,
        },
        _ => None,
    }
}

pub(crate) fn encode_row_payload_planned<'a, F>(
    meta: &RowPayloadMeta,
    plan: RowPayloadPlan,
    mut cell: F,
    out: &mut [u8],
) -> Option<usize>
where
    F: FnMut(usize) -> Option<RowCell<'a>>,
{
    if out.len() < plan.payload_len {
        return None;
    }
    let mut writer = PayloadWriter::new(&mut out[..plan.payload_len]);
    if meta.protocol == NIO_MYSQL_ROW_BINARY {
        writer.put_u8(0)?;
        writer.put_zeroes(plan.bitmap_len)?;
    }
    for index in 0..meta.cell_count {
        encode_row_cell(meta.protocol, index, cell(index)?, &mut writer)?;
    }
    (writer.pos == plan.payload_len).then_some(writer.pos)
}

pub fn encode_ok_payload<'a, FS, FU>(
    meta: &OkPayloadMeta,
    plan: OkPayloadPlan,
    message: &'a [u8],
    changed_schema: &'a [u8],
    mut system_var: FS,
    mut user_var: FU,
    out: &mut [u8],
) -> Option<usize>
where
    FS: FnMut(usize) -> Option<OkKv<'a>>,
    FU: FnMut(usize) -> Option<OkKv<'a>>,
{
    if message.len() != meta.message_len
        || changed_schema.len() != meta.changed_schema_len
        || out.len() < plan.payload_len
    {
        return None;
    }

    let protocol_41 = meta.capability_flags & CLIENT_PROTOCOL_41 != 0;
    let session_track = meta.capability_flags & CLIENT_SESSION_TRACK != 0;
    let status_has_state = meta.status_flags & SERVER_SESSION_STATE_CHANGED != 0;
    let mut writer = PayloadWriter::new(&mut out[..plan.payload_len]);

    writer.put_u8(0x00)?;
    writer.put_lenenc_int(meta.affected_rows)?;
    writer.put_lenenc_int(meta.last_insert_id)?;
    writer.put_u16_le(meta.status_flags)?;
    if protocol_41 {
        writer.put_u16_le(meta.warnings)?;
    }

    if session_track {
        if meta.use_standard_serialize {
            if !message.is_empty() || status_has_state {
                writer.put_lenenc_bytes(message)?;
            }
        } else if message.is_empty() {
            writer.put_lenenc_usize(0)?;
        } else {
            writer.put_lenenc_usize(message.len().checked_add(1)?)?;
            writer.put_u8(b' ')?;
            writer.put_bytes(message)?;
        }

        if status_has_state {
            writer.put_lenenc_usize(plan.state_info_len)?;
            if meta.system_var_count != 0 || meta.user_var_count != 0 {
                if meta.use_standard_serialize {
                    for index in 0..meta.system_var_count {
                        let kv = system_var(index)?;
                        let kv_len = kv_encoded_len(OkKvLengths {
                            key_len: kv.key.len(),
                            value_len: kv.value.len(),
                        })?;
                        writer.put_u8(SESSION_TRACK_SYSTEM_VARIABLES)?;
                        writer.put_lenenc_usize(kv_len)?;
                        writer.put_kv(kv)?;
                    }
                } else {
                    writer.put_u8(SESSION_TRACK_SYSTEM_VARIABLES)?;
                    writer.put_lenenc_usize(plan.legacy_vars_len)?;
                    for index in 0..meta.system_var_count {
                        writer.put_kv(system_var(index)?)?;
                    }
                    if meta.user_var_count != 0 {
                        writer.put_kv(OkKv {
                            key: LEGACY_USER_VAR_SEPARATOR,
                            value: LEGACY_USER_VAR_SEPARATOR,
                        })?;
                    }
                    for index in 0..meta.user_var_count {
                        writer.put_kv(user_var(index)?)?;
                    }
                }
            }

            if meta.schema_changed {
                let schema_item_len =
                    lenenc_usize_len(changed_schema.len())?.checked_add(changed_schema.len())?;
                writer.put_u8(SESSION_TRACK_SCHEMA)?;
                writer.put_lenenc_usize(schema_item_len)?;
                writer.put_lenenc_bytes(changed_schema)?;
            }

            if meta.state_changed {
                writer.put_u8(SESSION_TRACK_STATE_CHANGE)?;
                writer.put_lenenc_usize(2)?;
                writer.put_lenenc_bytes(b"1")?;
            }
        }
    } else if !message.is_empty() {
        if meta.use_standard_serialize {
            writer.put_lenenc_bytes(message)?;
        } else {
            writer.put_u8(b' ')?;
            writer.put_bytes(message)?;
        }
    }

    (writer.pos == plan.payload_len).then_some(writer.pos)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FrameLayout {
    pub wire_len: usize,
    pub packet_count: usize,
}

pub fn frame_layout(payload_len: usize) -> Option<FrameLayout> {
    if payload_len > MAX_LOGICAL_PAYLOAD {
        return None;
    }
    let packet_count = payload_len.checked_div(MAX_PAYLOAD)?.checked_add(1)?;
    let header_len = packet_count.checked_mul(HEADER_SIZE)?;
    let wire_len = payload_len.checked_add(header_len)?;
    Some(FrameLayout {
        wire_len,
        packet_count,
    })
}

/// # Safety
///
/// `buffer` must be non-null and point to one writable allocation of at least
/// `layout.wire_len` bytes. Bytes in
/// `buffer[HEADER_SIZE..HEADER_SIZE + payload_len]` must be initialized.
pub unsafe fn frame_payload_in_place(
    buffer: *mut u8,
    payload_len: usize,
    first_seq: u8,
    layout: FrameLayout,
) {
    debug_assert_eq!(frame_layout(payload_len), Some(layout));

    for frame in (1..layout.packet_count).rev() {
        let payload_at = frame * MAX_PAYLOAD;
        let chunk_len = if frame + 1 == layout.packet_count {
            payload_len - payload_at
        } else {
            MAX_PAYLOAD
        };
        if chunk_len > 0 {
            let src = HEADER_SIZE + payload_at;
            let dst = frame * (MAX_PAYLOAD + HEADER_SIZE) + HEADER_SIZE;
            unsafe { ptr::copy(buffer.add(src), buffer.add(dst), chunk_len) };
        }
    }

    for frame in 0..layout.packet_count {
        let payload_at = frame * MAX_PAYLOAD;
        let chunk_len = if frame + 1 == layout.packet_count {
            payload_len - payload_at
        } else {
            MAX_PAYLOAD
        };
        let header = frame * (MAX_PAYLOAD + HEADER_SIZE);
        let seq = first_seq.wrapping_add(frame as u8);
        unsafe {
            buffer.add(header).write((chunk_len & 0xff) as u8);
            buffer
                .add(header + 1)
                .write(((chunk_len >> 8) & 0xff) as u8);
            buffer
                .add(header + 2)
                .write(((chunk_len >> 16) & 0xff) as u8);
            buffer.add(header + 3).write(seq);
        }
    }
}
