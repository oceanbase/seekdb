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

use crate::*;

pub(crate) const NIO_FRAME_ERROR: c_int = -1;
pub(crate) const NIO_FRAME_OK: c_int = 0;
pub(crate) const NIO_FRAME_NEED_MORE: c_int = 1;

pub(crate) unsafe fn clear_framed_len(framed_len: *mut i64) {
    if !framed_len.is_null() {
        unsafe { framed_len.write(0) };
    }
}

use crate::ffi_check::{checked_array_len, checked_bytes_len};

#[derive(Clone, Copy)]
pub(crate) struct FfiMysqlFieldPlan {
    pub(crate) view: NioMysqlFieldView,
    pub(crate) meta: response::FieldPayloadMeta,
    pub(crate) plan: response::FieldPayloadPlan,
    pub(crate) frame: response::FrameLayout,
}

pub(crate) fn mysql_field_plan(view: NioMysqlFieldView) -> Option<FfiMysqlFieldPlan> {
    if view.reserved.iter().any(|byte| *byte != 0) {
        return None;
    }
    let meta = response::FieldPayloadMeta {
        schema_len: checked_bytes_len(view.schema.data, view.schema.len)?,
        table_len: checked_bytes_len(view.table.data, view.table.len)?,
        org_table_len: checked_bytes_len(view.org_table.data, view.org_table.len)?,
        name_len: checked_bytes_len(view.name.data, view.name.len)?,
        org_name_len: checked_bytes_len(view.org_name.data, view.org_name.len)?,
        type_owner_len: checked_bytes_len(view.type_owner.data, view.type_owner.len)?,
        type_name_len: checked_bytes_len(view.type_name.data, view.type_name.len)?,
        column_length: view.column_length,
        charset: view.charset,
        flags: view.flags,
        field_type: view.field_type,
        default_type: view.default_type,
        decimals: view.decimals,
    };
    let plan = response::plan_field_payload(&meta)?;
    let frame = response::frame_layout(plan.payload_len)?;
    Some(FfiMysqlFieldPlan {
        view,
        meta,
        plan,
        frame,
    })
}

pub(crate) unsafe fn ffi_mysql_field_plan(
    data: *const NioMysqlFieldView,
    index: usize,
) -> Option<FfiMysqlFieldPlan> {
    mysql_field_plan(unsafe { data.add(index).read() })
}

pub(crate) unsafe fn ffi_mysql_field_bytes<'a>(
    field: &FfiMysqlFieldPlan,
) -> response::FieldPayloadBytes<'a> {
    let type_bytes = if field.meta.field_type == response::MYSQL_TYPE_COMPLEX {
        unsafe {
            (
                ffi_bytes(field.view.type_owner.data, field.meta.type_owner_len),
                ffi_bytes(field.view.type_name.data, field.meta.type_name_len),
            )
        }
    } else {
        (&[][..], &[][..])
    };
    unsafe {
        response::FieldPayloadBytes {
            schema: ffi_bytes(field.view.schema.data, field.meta.schema_len),
            table: ffi_bytes(field.view.table.data, field.meta.table_len),
            org_table: ffi_bytes(field.view.org_table.data, field.meta.org_table_len),
            name: ffi_bytes(field.view.name.data, field.meta.name_len),
            org_name: ffi_bytes(field.view.org_name.data, field.meta.org_name_len),
            type_owner: type_bytes.0,
            type_name: type_bytes.1,
        }
    }
}

/// # Safety
///
/// A non-empty view must point to `len` readable bytes for the returned
/// lifetime.
pub(crate) unsafe fn ffi_bytes<'a>(data: *const c_char, len: usize) -> &'a [u8] {
    if len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(data.cast::<u8>(), len) }
    }
}

pub(crate) unsafe fn ffi_mysql_kv_lengths(
    data: *const NioMysqlKvView,
    index: usize,
) -> Option<response::OkKvLengths> {
    let kv = unsafe { data.add(index).read() };
    Some(response::OkKvLengths {
        key_len: checked_bytes_len(kv.key.data, kv.key.len)?,
        value_len: checked_bytes_len(kv.value.data, kv.value.len)?,
    })
}

pub(crate) unsafe fn ffi_mysql_kv<'a>(
    data: *const NioMysqlKvView,
    index: usize,
) -> Option<response::OkKv<'a>> {
    let kv = unsafe { data.add(index).read() };
    let key_len = checked_bytes_len(kv.key.data, kv.key.len)?;
    let value_len = checked_bytes_len(kv.value.data, kv.value.len)?;
    Some(response::OkKv {
        key: unsafe { ffi_bytes(kv.key.data, key_len) },
        value: unsafe { ffi_bytes(kv.value.data, value_len) },
    })
}

pub(crate) fn mysql_row_cell_meta(view: NioMysqlCellView) -> Option<response::RowCellMeta> {
    if view.reserved.iter().any(|byte| *byte != 0) {
        return None;
    }
    Some(response::RowCellMeta {
        bytes_len: checked_bytes_len(view.bytes.data, view.bytes.len)?,
        value: view.value,
        days: view.days,
        microseconds: view.microseconds,
        year: view.year,
        month: view.month,
        day: view.day,
        hour: view.hour,
        minute: view.minute,
        second: view.second,
        kind: view.kind,
        flags: view.flags,
        bit_len: view.bit_len,
    })
}

pub(crate) unsafe fn ffi_mysql_row_cell_meta(
    data: *const NioMysqlCellView,
    index: usize,
) -> Option<response::RowCellMeta> {
    mysql_row_cell_meta(unsafe { data.add(index).read() })
}

pub(crate) unsafe fn ffi_mysql_row_cell<'a>(
    data: *const NioMysqlCellView,
    index: usize,
) -> Option<response::RowCell<'a>> {
    let view = unsafe { data.add(index).read() };
    let meta = mysql_row_cell_meta(view)?;
    Some(response::RowCell {
        meta,
        bytes: unsafe { ffi_bytes(view.bytes.data, meta.bytes_len) },
    })
}

pub(crate) unsafe fn ffi_mysql_row_plan(
    row_view: *const NioMysqlRowView,
) -> Option<(
    NioMysqlRowView,
    response::RowPayloadMeta,
    response::RowPayloadPlan,
)> {
    let view = unsafe { row_view.read() };
    if view.reserved != 0 {
        return None;
    }
    let cell_count = checked_array_len(view.cells, view.cell_count)?;
    let meta = response::RowPayloadMeta {
        protocol: view.protocol,
        cell_count,
    };
    let plan = response::plan_row_payload(&meta, |index| unsafe {
        ffi_mysql_row_cell_meta(view.cells, index)
    })?;
    Some((view, meta, plan))
}

/// # Safety
///
/// Output pointers follow the public response-encoder FFI contract.
pub(crate) unsafe fn prepare_mysql_payload(
    buffer: *mut c_char,
    buffer_len: i64,
    payload_len: usize,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> Result<response::FrameLayout, c_int> {
    unsafe { clear_framed_len(framed_len) };
    if buffer.is_null() || framed_len.is_null() || next_seq.is_null() || buffer_len < 0 {
        return Err(NIO_FRAME_ERROR);
    }

    let buffer_len = usize::try_from(buffer_len).map_err(|_| NIO_FRAME_ERROR)?;
    let layout = response::frame_layout(payload_len).ok_or(NIO_FRAME_ERROR)?;
    let wire_len = i64::try_from(layout.wire_len).map_err(|_| NIO_FRAME_ERROR)?;
    unsafe { framed_len.write(wire_len) };
    if buffer_len < layout.wire_len {
        return Err(NIO_FRAME_NEED_MORE);
    }
    Ok(layout)
}

/// # Safety
///
/// `buffer` and `layout` must have passed [`prepare_mysql_payload`], and the
/// complete payload must be initialized.
pub(crate) unsafe fn finish_mysql_payload(
    buffer: *mut c_char,
    payload_len: usize,
    first_seq: u8,
    layout: response::FrameLayout,
    next_seq: *mut u8,
) {
    unsafe {
        response::frame_payload_in_place(buffer.cast::<u8>(), payload_len, first_seq, layout);
        next_seq.write(first_seq.wrapping_add(layout.packet_count as u8));
    }
}

/// # Safety
///
/// Output pointers follow the public response-encoder FFI contract. Any input
/// slices captured by `encode` must not overlap `buffer`.
pub(crate) unsafe fn encode_mysql_generated_payload<F>(
    buffer: *mut c_char,
    buffer_len: i64,
    payload_len: usize,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
    encode: F,
) -> c_int
where
    F: FnOnce(&mut [u8]) -> Option<usize>,
{
    let layout = match unsafe {
        prepare_mysql_payload(buffer, buffer_len, payload_len, framed_len, next_seq)
    } {
        Ok(layout) => layout,
        Err(rc) => return rc,
    };

    let payload = unsafe {
        std::slice::from_raw_parts_mut(buffer.cast::<u8>().add(HEADER_SIZE), payload_len)
    };
    if encode(payload) != Some(payload_len) {
        return NIO_FRAME_ERROR;
    }
    unsafe { finish_mysql_payload(buffer, payload_len, first_seq, layout, next_seq) };
    NIO_FRAME_OK
}

pub(crate) unsafe fn encode_mysql_generated_payload_only<F>(
    buffer: *mut c_char,
    buffer_len: i64,
    required_len: usize,
    payload_len: *mut i64,
    encode: F,
) -> c_int
where
    F: FnOnce(&mut [u8]) -> Option<usize>,
{
    unsafe { clear_framed_len(payload_len) };
    if buffer.is_null() || payload_len.is_null() || buffer_len < 0 {
        return NIO_FRAME_ERROR;
    }
    let buffer_len = match usize::try_from(buffer_len) {
        Ok(buffer_len) => buffer_len,
        Err(_) => return NIO_FRAME_ERROR,
    };
    let required_len_i64 = match i64::try_from(required_len) {
        Ok(required_len) => required_len,
        Err(_) => return NIO_FRAME_ERROR,
    };
    unsafe { payload_len.write(required_len_i64) };
    if buffer_len < required_len {
        return NIO_FRAME_NEED_MORE;
    }

    let output = unsafe { std::slice::from_raw_parts_mut(buffer.cast::<u8>(), required_len) };
    if encode(output) != Some(required_len) {
        return NIO_FRAME_ERROR;
    }
    NIO_FRAME_OK
}

pub(crate) fn write_packed_row_blob_header(out: &mut [u8], payload_len: usize) -> Option<()> {
    if out.len() < PACKED_ROW_BLOB_HEADER_LEN || payload_len > MAX_LOGICAL_PAYLOAD {
        return None;
    }
    out[..4].copy_from_slice(&PACKED_ROW_BLOB_MAGIC);
    out[4] = PACKED_ROW_BLOB_VERSION;
    out[5] = 0;
    out[6..8].copy_from_slice(&(PACKED_ROW_BLOB_HEADER_LEN as u16).to_le_bytes());
    out[8..16].copy_from_slice(&u64::try_from(payload_len).ok()?.to_le_bytes());
    Some(())
}

pub(crate) fn parse_packed_row_blob(blob: &[u8]) -> Option<&[u8]> {
    if blob.len() < PACKED_ROW_BLOB_HEADER_LEN
        || blob[..4] != PACKED_ROW_BLOB_MAGIC
        || blob[4] != PACKED_ROW_BLOB_VERSION
        || blob[5] != 0
        || u16::from_le_bytes(blob[6..8].try_into().ok()?) as usize != PACKED_ROW_BLOB_HEADER_LEN
    {
        return None;
    }
    let payload_len = usize::try_from(u64::from_le_bytes(blob[8..16].try_into().ok()?)).ok()?;
    if payload_len == 0 || payload_len > MAX_LOGICAL_PAYLOAD {
        return None;
    }
    let blob_len = PACKED_ROW_BLOB_HEADER_LEN.checked_add(payload_len)?;
    if blob_len != blob.len() {
        return None;
    }
    Some(&blob[PACKED_ROW_BLOB_HEADER_LEN..])
}

/// # Safety
///
/// The output pointer contract is the standard response-encoder contract.
/// Non-empty input views must be readable for the duration of this call and
/// must not overlap `buffer`.
pub(crate) unsafe fn encode_mysql_error(
    buffer: *mut c_char,
    buffer_len: i64,
    error_code: u16,
    sql_state: *const c_char,
    sql_state_len: i64,
    message: *const c_char,
    message_len: i64,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    let sql_state_len = match checked_bytes_len(sql_state, sql_state_len) {
        Some(sql_state_len) => sql_state_len,
        None => return NIO_FRAME_ERROR,
    };
    if sql_state_len != 5 {
        return NIO_FRAME_ERROR;
    }
    let message_len = match checked_bytes_len(message, message_len) {
        Some(message_len) => message_len,
        None => return NIO_FRAME_ERROR,
    };
    let payload_len = match response::error_payload_len(message_len) {
        Some(payload_len) => payload_len,
        None => return NIO_FRAME_ERROR,
    };
    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            payload_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                let sql_state = ffi_bytes(sql_state, sql_state_len);
                let sql_state: &[u8; 5] = sql_state.try_into().ok()?;
                let message = ffi_bytes(message, message_len);
                response::encode_error_payload(error_code, sql_state, message, out)
            },
        )
    }
}

/// # Safety
///
/// The pointer contract is the same as [`encode_mysql_error`].
pub(crate) unsafe fn encode_mysql_auth_switch(
    buffer: *mut c_char,
    buffer_len: i64,
    plugin_name: *const c_char,
    plugin_name_len: i64,
    scramble: *const c_char,
    scramble_len: i64,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    let plugin_name_len = match checked_bytes_len(plugin_name, plugin_name_len) {
        Some(plugin_name_len) => plugin_name_len,
        None => return NIO_FRAME_ERROR,
    };
    let scramble_len = match checked_bytes_len(scramble, scramble_len) {
        Some(scramble_len) => scramble_len,
        None => return NIO_FRAME_ERROR,
    };
    let payload_len = match response::auth_switch_payload_len(plugin_name_len, scramble_len) {
        Some(payload_len) => payload_len,
        None => return NIO_FRAME_ERROR,
    };
    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            payload_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                let plugin_name = ffi_bytes(plugin_name, plugin_name_len);
                let scramble = ffi_bytes(scramble, scramble_len);
                response::encode_auth_switch_payload(plugin_name, scramble, out)
            },
        )
    }
}

/// # Safety
///
/// The pointer contract is the same as [`encode_mysql_error`].
pub(crate) unsafe fn encode_mysql_local_infile(
    buffer: *mut c_char,
    buffer_len: i64,
    filename: *const c_char,
    filename_len: i64,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    let filename_len = match checked_bytes_len(filename, filename_len) {
        Some(filename_len) => filename_len,
        None => return NIO_FRAME_ERROR,
    };
    let payload_len = match response::local_infile_payload_len(filename_len) {
        Some(payload_len) => payload_len,
        None => return NIO_FRAME_ERROR,
    };
    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            payload_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                let filename = ffi_bytes(filename, filename_len);
                response::encode_local_infile_payload(filename, out)
            },
        )
    }
}

/// # Safety
///
/// The pointer contract is the same as [`encode_mysql_error`].
pub(crate) unsafe fn encode_mysql_string(
    buffer: *mut c_char,
    buffer_len: i64,
    value: *const c_char,
    value_len: i64,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    let value_len = match checked_bytes_len(value, value_len) {
        Some(value_len) => value_len,
        None => return NIO_FRAME_ERROR,
    };
    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            value_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                let value = ffi_bytes(value, value_len);
                out.copy_from_slice(value);
                Some(value_len)
            },
        )
    }
}

/// # Safety
///
/// `field_view` and all non-empty byte views must remain readable and immutable
/// for this call. None may overlap `buffer`. Output pointers follow the same
/// contract as the other standard response encoders.
pub(crate) unsafe fn encode_mysql_field(
    buffer: *mut c_char,
    buffer_len: i64,
    field_view: *const NioMysqlFieldView,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    if buffer.is_null()
        || framed_len.is_null()
        || next_seq.is_null()
        || buffer_len < 0
        || field_view.is_null()
        || !(field_view as usize).is_multiple_of(std::mem::align_of::<NioMysqlFieldView>())
    {
        return NIO_FRAME_ERROR;
    }

    let field = match mysql_field_plan(unsafe { field_view.read() }) {
        Some(field) => field,
        None => return NIO_FRAME_ERROR,
    };

    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            field.plan.payload_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                response::encode_field_payload_planned(
                    &field.meta,
                    field.plan,
                    ffi_mysql_field_bytes(&field),
                    out,
                )
            },
        )
    }
}

/// # Safety
///
/// `row_view`, its cell array, and all non-empty byte views must remain
/// readable and immutable for this call. None may overlap `buffer`.
#[no_mangle]
pub unsafe extern "C" fn nio_encode_mysql_packed_row_blob(
    buffer: *mut c_char,
    buffer_len: i64,
    row_view: *const NioMysqlRowView,
    blob_len: *mut i64,
) -> c_int {
    unsafe { clear_framed_len(blob_len) };
    if buffer.is_null() || blob_len.is_null() || buffer_len < 0 || row_view.is_null() {
        return NIO_FRAME_ERROR;
    }
    let (view, meta, plan) = match unsafe { ffi_mysql_row_plan(row_view) } {
        Some(row) => row,
        None => return NIO_FRAME_ERROR,
    };
    if plan.payload_len == 0 || plan.payload_len > MAX_LOGICAL_PAYLOAD {
        return NIO_FRAME_ERROR;
    }

    let required_len = match PACKED_ROW_BLOB_HEADER_LEN.checked_add(plan.payload_len) {
        Some(required_len) => required_len,
        None => return NIO_FRAME_ERROR,
    };
    unsafe {
        encode_mysql_generated_payload_only(buffer, buffer_len, required_len, blob_len, |out| {
            let (_, payload) = out.split_at_mut(PACKED_ROW_BLOB_HEADER_LEN);
            if response::encode_row_payload_planned(
                &meta,
                plan,
                |index| ffi_mysql_row_cell(view.cells, index),
                payload,
            ) != Some(plan.payload_len)
            {
                return None;
            }
            write_packed_row_blob_header(out, plan.payload_len)?;
            Some(required_len)
        })
    }
}

/// # Safety
///
/// The row input contract is the same as [`nio_encode_mysql_packed_row_blob`].
/// Output pointers follow the standard response-encoder contract.
pub(crate) unsafe fn encode_mysql_row(
    buffer: *mut c_char,
    buffer_len: i64,
    row_view: *const NioMysqlRowView,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    if buffer.is_null()
        || framed_len.is_null()
        || next_seq.is_null()
        || buffer_len < 0
        || row_view.is_null()
    {
        return NIO_FRAME_ERROR;
    }
    let (view, meta, plan) = match unsafe { ffi_mysql_row_plan(row_view) } {
        Some(row) => row,
        None => return NIO_FRAME_ERROR,
    };

    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            plan.payload_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                response::encode_row_payload_planned(
                    &meta,
                    plan,
                    |index| ffi_mysql_row_cell(view.cells, index),
                    out,
                )
            },
        )
    }
}

/// # Safety
///
/// A non-empty `blob` must be readable for this call and must not overlap
/// `buffer`. Output pointers follow the standard response-encoder contract.
pub(crate) unsafe fn frame_mysql_packed_row_blob(
    buffer: *mut c_char,
    buffer_len: i64,
    blob: *const c_char,
    blob_len: i64,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    let blob_len = match checked_bytes_len(blob, blob_len) {
        Some(blob_len) => blob_len,
        _ => return NIO_FRAME_ERROR,
    };
    let blob = unsafe { ffi_bytes(blob, blob_len) };
    let payload = match parse_packed_row_blob(blob) {
        Some(payload) => payload,
        None => return NIO_FRAME_ERROR,
    };
    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            payload.len(),
            first_seq,
            framed_len,
            next_seq,
            |out| {
                out.copy_from_slice(payload);
                Some(payload.len())
            },
        )
    }
}

/// # Safety
///
/// `ok_view`, its descriptor arrays, and all non-empty byte views must remain
/// readable and immutable for this call. None may overlap `buffer`. Output
/// pointers follow the standard response-encoder contract.
pub(crate) unsafe fn encode_mysql_ok(
    buffer: *mut c_char,
    buffer_len: i64,
    ok_view: *const NioMysqlOkView,
    first_seq: u8,
    framed_len: *mut i64,
    next_seq: *mut u8,
) -> c_int {
    unsafe { clear_framed_len(framed_len) };
    if buffer.is_null()
        || framed_len.is_null()
        || next_seq.is_null()
        || buffer_len < 0
        || ok_view.is_null()
    {
        return NIO_FRAME_ERROR;
    }

    let view = unsafe { ok_view.read() };
    if view.reserved != 0 || view.behavior_flags & !NIO_MYSQL_OK_KNOWN_BEHAVIOR_FLAGS != 0 {
        return NIO_FRAME_ERROR;
    }
    let message_len = match checked_bytes_len(view.message.data, view.message.len) {
        Some(len) => len,
        None => return NIO_FRAME_ERROR,
    };
    let changed_schema_len =
        match checked_bytes_len(view.changed_schema.data, view.changed_schema.len) {
            Some(len) => len,
            None => return NIO_FRAME_ERROR,
        };
    let system_var_count = match checked_array_len(view.system_vars, view.system_var_count) {
        Some(count) => count,
        None => return NIO_FRAME_ERROR,
    };
    let user_var_count = match checked_array_len(view.user_vars, view.user_var_count) {
        Some(count) => count,
        None => return NIO_FRAME_ERROR,
    };

    let meta = response::OkPayloadMeta {
        affected_rows: view.affected_rows,
        last_insert_id: view.last_insert_id,
        capability_flags: view.capability_flags,
        status_flags: view.status_flags,
        warnings: view.warnings,
        use_standard_serialize: view.behavior_flags & NIO_MYSQL_OK_USE_STANDARD_SERIALIZE != 0,
        schema_changed: view.behavior_flags & NIO_MYSQL_OK_SCHEMA_CHANGED != 0,
        state_changed: view.behavior_flags & NIO_MYSQL_OK_STATE_CHANGED != 0,
        message_len,
        changed_schema_len,
        system_var_count,
        user_var_count,
    };
    let plan = match response::plan_ok_payload(
        &meta,
        |index| unsafe { ffi_mysql_kv_lengths(view.system_vars, index) },
        |index| unsafe { ffi_mysql_kv_lengths(view.user_vars, index) },
    ) {
        Some(plan) => plan,
        None => return NIO_FRAME_ERROR,
    };

    unsafe {
        encode_mysql_generated_payload(
            buffer,
            buffer_len,
            plan.payload_len,
            first_seq,
            framed_len,
            next_seq,
            |out| {
                let message = ffi_bytes(view.message.data, message_len);
                let changed_schema = ffi_bytes(view.changed_schema.data, changed_schema_len);
                response::encode_ok_payload(
                    &meta,
                    plan,
                    message,
                    changed_schema,
                    |index| ffi_mysql_kv(view.system_vars, index),
                    |index| ffi_mysql_kv(view.user_vars, index),
                    out,
                )
            },
        )
    }
}
