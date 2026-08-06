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

use crate::ffi_check::{checked_array_len, checked_out_range, ranges_overlap};

pub(crate) fn reserve_response_output(
    out: &mut Vec<u8>,
    additional: usize,
) -> Result<(), TryReserveError> {
    out.try_reserve(additional)
}

pub(crate) fn append_response_locked(
    g: &mut ConnInner,
    bytes: &[u8],
) -> Result<(), TryReserveError> {
    g.compact_output_for_append();
    if g.req_compressed {
        compress::frame_into(&mut g.outbuf, bytes, &mut g.next_comp_seq)?;
    } else {
        reserve_response_output(&mut g.outbuf, bytes.len())?;
        g.outbuf.extend_from_slice(bytes);
    }
    Ok(())
}

/// # Safety
/// `connection_handle` is null or is a live handle acquired for this
/// connection and must not be released until this call returns. The semantic
/// inputs accepted by `encode` obey their individual response-view contracts.
pub(crate) unsafe fn append_encoded_response<F>(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    framed_len: *mut i64,
    mut encode: F,
) -> c_int
where
    F: FnMut(*mut c_char, i64, u8, *mut i64, *mut u8) -> c_int,
{
    if framed_len.is_null() {
        return -1;
    }
    let conn = match unsafe { connection_handle.as_ref() } {
        Some(handle) if !handle.conn.close_notified.load(Ordering::Acquire) => &handle.conn,
        None => return -1,
        Some(_) => return -1,
    };
    let mut g = conn.mu.lock().unwrap();
    if !valid_request_generation(conn, generation)
        || !g.response.is_active(generation)
        || g.final_response != FinalResponseState::Idle
        || conn.err.load(Ordering::Acquire)
    {
        return -1;
    }
    unsafe { framed_len.write(0) };

    loop {
        if !g.response.batch.is_empty() && g.response.batch.len() >= RESPONSE_BATCH_TARGET {
            drop(g);
            if !publish_response_batch(conn, generation, false)
                || drain_blocking_response(conn, generation) != 0
            {
                return -1;
            }
            g = conn.mu.lock().unwrap();
            if !valid_request_generation(conn, generation)
                || !g.response.is_active(generation)
                || g.final_response != FinalResponseState::Idle
                || conn.err.load(Ordering::Acquire)
            {
                return -1;
            }
            continue;
        }

        let first_seq = g.response.next_mysql_seq;
        let old_len = g.response.batch.len();
        let batch_room = RESPONSE_BATCH_TARGET.saturating_sub(old_len);
        let headroom = RESPONSE_ENCODE_HEADROOM.min(batch_room);
        if headroom == 0 || reserve_response_output(&mut g.response.batch, headroom).is_err() {
            conn.err.store(true, Ordering::Release);
            g.clear_output();
            drop(g);
            mark_connection_error(conn);
            return -1;
        }
        g.response.batch.resize(old_len + headroom, 0);
        let output = unsafe { g.response.batch.as_mut_ptr().add(old_len).cast::<c_char>() };
        let mut actual = 0i64;
        let mut next_seq = first_seq;
        let encode_rc = encode(
            output,
            headroom as i64,
            first_seq,
            &mut actual,
            &mut next_seq,
        );

        if encode_rc == NIO_FRAME_OK {
            let actual_usize = match usize::try_from(actual) {
                Ok(actual) if actual > 0 && actual <= headroom => actual,
                _ => {
                    g.response.batch.truncate(old_len);
                    return -1;
                }
            };
            g.response.batch.truncate(old_len + actual_usize);
            g.response.next_mysql_seq = next_seq;
            unsafe { framed_len.write(actual) };
            return 0;
        }
        g.response.batch.truncate(old_len);
        if encode_rc != NIO_FRAME_NEED_MORE || actual <= headroom as i64 || next_seq != first_seq {
            return -1;
        }

        let required = match usize::try_from(actual) {
            Ok(required) => required,
            Err(_) => return -1,
        };
        if old_len != 0
            && old_len
                .checked_add(required)
                .is_none_or(|total| total > RESPONSE_BATCH_TARGET)
        {
            drop(g);
            if !publish_response_batch(conn, generation, false)
                || drain_blocking_response(conn, generation) != 0
            {
                return -1;
            }
            g = conn.mu.lock().unwrap();
            if !valid_request_generation(conn, generation)
                || !g.response.is_active(generation)
                || g.final_response != FinalResponseState::Idle
                || conn.err.load(Ordering::Acquire)
            {
                return -1;
            }
            continue;
        }

        if reserve_response_output(&mut g.response.batch, required).is_err() {
            conn.err.store(true, Ordering::Release);
            g.clear_output();
            drop(g);
            mark_connection_error(conn);
            return -1;
        }
        g.response.batch.resize(old_len + required, 0);
        let output = unsafe { g.response.batch.as_mut_ptr().add(old_len).cast::<c_char>() };
        let mut encoded = 0i64;
        next_seq = first_seq;
        let retry_rc = encode(output, actual, first_seq, &mut encoded, &mut next_seq);
        if retry_rc != NIO_FRAME_OK || encoded != actual {
            g.response.batch.truncate(old_len);
            return -1;
        }
        g.response.next_mysql_seq = next_seq;
        unsafe { framed_len.write(encoded) };
        return 0;
    }
}

/// # Safety
/// `connection_handle` follows the response-handle contract and `framed_len`
/// is writable for the active generation.
pub(crate) unsafe fn append_fixed_response(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    payload: &[u8],
    framed_len: *mut i64,
) -> c_int {
    if framed_len.is_null() || payload.len() >= MAX_PAYLOAD {
        return -1;
    }
    let conn = match unsafe { connection_handle.as_ref() } {
        Some(handle) if !handle.conn.close_notified.load(Ordering::Acquire) => &handle.conn,
        None => return -1,
        Some(_) => return -1,
    };
    let frame_len = match HEADER_SIZE.checked_add(payload.len()) {
        Some(frame_len) => frame_len,
        None => return -1,
    };
    let frame_len_i64 = match i64::try_from(frame_len) {
        Ok(frame_len) => frame_len,
        Err(_) => return -1,
    };

    let mut g = conn.mu.lock().unwrap();
    if !valid_request_generation(conn, generation)
        || !g.response.is_active(generation)
        || g.final_response != FinalResponseState::Idle
        || conn.err.load(Ordering::Acquire)
    {
        return -1;
    }
    unsafe { framed_len.write(0) };

    loop {
        let old_len = g.response.batch.len();
        if old_len != 0
            && old_len
                .checked_add(frame_len)
                .is_none_or(|total| total > RESPONSE_BATCH_TARGET)
        {
            drop(g);
            if !publish_response_batch(conn, generation, false)
                || drain_blocking_response(conn, generation) != 0
            {
                return -1;
            }
            g = conn.mu.lock().unwrap();
            if !valid_request_generation(conn, generation)
                || !g.response.is_active(generation)
                || g.final_response != FinalResponseState::Idle
                || conn.err.load(Ordering::Acquire)
            {
                return -1;
            }
            continue;
        }

        if reserve_response_output(&mut g.response.batch, frame_len).is_err() {
            conn.err.store(true, Ordering::Release);
            g.clear_output();
            drop(g);
            mark_connection_error(conn);
            return -1;
        }
        let payload_len = payload.len();
        let seq = g.response.next_mysql_seq;
        g.response.batch.extend_from_slice(&[
            (payload_len & 0xff) as u8,
            ((payload_len >> 8) & 0xff) as u8,
            ((payload_len >> 16) & 0xff) as u8,
            seq,
        ]);
        g.response.batch.extend_from_slice(payload);
        g.response.next_mysql_seq = seq.wrapping_add(1);
        unsafe { framed_len.write(frame_len_i64) };
        return 0;
    }
}

pub(crate) fn encode_metadata_payload_frame(
    out: &mut [u8],
    at: &mut usize,
    next_seq: &mut u8,
    payload: &[u8],
) -> Option<()> {
    let layout = response::frame_layout(payload.len())?;
    let end = at.checked_add(layout.wire_len)?;
    let frame = out.get_mut(*at..end)?;
    frame
        .get_mut(HEADER_SIZE..HEADER_SIZE.checked_add(payload.len())?)?
        .copy_from_slice(payload);
    unsafe {
        response::frame_payload_in_place(frame.as_mut_ptr(), payload.len(), *next_seq, layout)
    };
    *next_seq = next_seq.wrapping_add(layout.packet_count as u8);
    *at = end;
    Some(())
}

pub(crate) unsafe fn encode_metadata_field_frame(
    out: &mut [u8],
    at: &mut usize,
    next_seq: &mut u8,
    field: FfiMysqlFieldPlan,
) -> Option<()> {
    let end = at.checked_add(field.frame.wire_len)?;
    let frame = out.get_mut(*at..end)?;
    let payload_end = HEADER_SIZE.checked_add(field.plan.payload_len)?;
    let payload = frame.get_mut(HEADER_SIZE..payload_end)?;
    if response::encode_field_payload_planned(
        &field.meta,
        field.plan,
        unsafe { ffi_mysql_field_bytes(&field) },
        payload,
    )? != field.plan.payload_len
    {
        return None;
    }
    unsafe {
        response::frame_payload_in_place(
            frame.as_mut_ptr(),
            field.plan.payload_len,
            *next_seq,
            field.frame,
        )
    };
    *next_seq = next_seq.wrapping_add(field.frame.packet_count as u8);
    *at = end;
    Some(())
}

pub(crate) unsafe fn encode_resultset_metadata_run(
    out: &mut [u8],
    include_result_header: bool,
    fields: *const NioMysqlFieldView,
    field_count: usize,
    eof_field_count: u8,
    warnings: u16,
    status_flags: u16,
    first_seq: u8,
) -> Option<u8> {
    let mut at = 0usize;
    let mut next_seq = first_seq;
    if include_result_header {
        let mut payload = [0; response::MAX_LENENC_SIZE];
        let field_count = u64::try_from(field_count).ok()?;
        let payload_len = response::encode_result_header_payload(field_count, &mut payload);
        encode_metadata_payload_frame(out, &mut at, &mut next_seq, &payload[..payload_len])?;
    }
    for index in 0..field_count {
        let field = unsafe { ffi_mysql_field_plan(fields, index) }?;
        unsafe { encode_metadata_field_frame(out, &mut at, &mut next_seq, field) }?;
    }
    let eof = response::encode_eof_payload(eof_field_count, warnings, status_flags);
    encode_metadata_payload_frame(out, &mut at, &mut next_seq, &eof)?;
    (at == out.len()).then_some(next_seq)
}

/// # Safety
///
/// `fields` points to one readable descriptor whose nested byte views remain
/// readable until return. Both output pointers are writable and non-overlapping.
#[inline(never)]
pub(crate) unsafe fn append_single_field_metadata(
    response_writer: &mut ResponseWriter,
    include_result_header: bool,
    fields: *const NioMysqlFieldView,
    eof_field_count: u8,
    warnings: u16,
    status_flags: u16,
    semantic_packet_count: i64,
    packet_count: *mut i64,
    framed_len: *mut i64,
) -> c_int {
    let field = match unsafe { ffi_mysql_field_plan(fields, 0) } {
        Some(field) => field,
        None => return -1,
    };
    let mut wire_len = field.frame.wire_len;
    let mut header_payload = [0; response::MAX_LENENC_SIZE];
    let header_payload_len = if include_result_header {
        let payload_len = response::encode_result_header_payload(1, &mut header_payload);
        let layout = match response::frame_layout(payload_len) {
            Some(layout) => layout,
            None => return -1,
        };
        wire_len = match wire_len.checked_add(layout.wire_len) {
            Some(len) => len,
            None => return -1,
        };
        payload_len
    } else {
        0
    };
    let eof = response::encode_eof_payload(eof_field_count, warnings, status_flags);
    wire_len = match response::frame_layout(eof.len())
        .and_then(|layout| wire_len.checked_add(layout.wire_len))
        .filter(|len| *len <= isize::MAX as usize)
    {
        Some(len) => len,
        None => return -1,
    };
    let framed_len_i64 = match i64::try_from(wire_len) {
        Ok(len) => len,
        Err(_) => return -1,
    };

    let old_len = response_writer.batch.len();
    let new_len = match old_len.checked_add(wire_len) {
        Some(len) if len <= isize::MAX as usize => len,
        _ => return -1,
    };
    if reserve_response_output(&mut response_writer.batch, wire_len).is_err() {
        return -1;
    }
    let first_seq = response_writer.next_mysql_seq;
    response_writer.batch.resize(new_len, 0);
    let encoded = (|| {
        let out = &mut response_writer.batch[old_len..new_len];
        let mut at = 0usize;
        let mut next_seq = first_seq;
        if include_result_header {
            encode_metadata_payload_frame(
                out,
                &mut at,
                &mut next_seq,
                &header_payload[..header_payload_len],
            )?;
        }
        unsafe { encode_metadata_field_frame(out, &mut at, &mut next_seq, field) }?;
        encode_metadata_payload_frame(out, &mut at, &mut next_seq, &eof)?;
        (at == out.len()).then_some(next_seq)
    })();
    let next_seq = match encoded {
        Some(next_seq) => next_seq,
        None => {
            response_writer.batch.truncate(old_len);
            return -1;
        }
    };
    response_writer.next_mysql_seq = next_seq;
    unsafe {
        packet_count.write(semantic_packet_count);
        framed_len.write(framed_len_i64);
    }
    0
}

/// # Safety
///
/// `connection_handle` must remain live for this call. `fields` and every
/// non-empty nested byte view must remain readable and immutable until return.
/// Both output pointers must be writable, mutually non-overlapping, and must
/// not overlap any input descriptor or byte view.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_resultset_metadata(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    include_result_header: c_int,
    fields: *const NioMysqlFieldView,
    field_count: i64,
    eof_field_count: u8,
    warnings: u16,
    status_flags: u16,
    packet_count: *mut i64,
    framed_len: *mut i64,
) -> c_int {
    let packet_count_range = match checked_out_range(packet_count) {
        Some(range) => range,
        None => return -1,
    };
    let _framed_len_range = match checked_out_range(framed_len) {
        Some(range) if !ranges_overlap(packet_count_range, range) => range,
        _ => return -1,
    };
    let conn = match unsafe { connection_handle.as_ref() } {
        Some(handle) if !handle.conn.close_notified.load(Ordering::Acquire) => &handle.conn,
        None => return -1,
        Some(_) => return -1,
    };
    let mut g = conn.mu.lock().unwrap();
    if !valid_request_generation(conn, generation)
        || !g.response.is_active(generation)
        || g.final_response != FinalResponseState::Idle
        || conn.err.load(Ordering::Acquire)
    {
        return -1;
    }
    unsafe {
        packet_count.write(0);
        framed_len.write(0);
    }

    let include_result_header = match include_result_header {
        0 => false,
        1 => true,
        _ => return -1,
    };
    let field_count = match checked_array_len(fields, field_count) {
        Some(field_count) => field_count,
        None => return -1,
    };
    let result_header_field_count = match u64::try_from(field_count) {
        Ok(field_count) => field_count,
        Err(_) => return -1,
    };
    let semantic_packet_count = match field_count
        .checked_add(1)
        .and_then(|count| count.checked_add(usize::from(include_result_header)))
        .and_then(|count| i64::try_from(count).ok())
    {
        Some(count) => count,
        None => return -1,
    };
    if field_count == 1 {
        return unsafe {
            append_single_field_metadata(
                &mut g.response,
                include_result_header,
                fields,
                eof_field_count,
                warnings,
                status_flags,
                semantic_packet_count,
                packet_count,
                framed_len,
            )
        };
    }

    let mut wire_len = 0usize;
    if include_result_header {
        let mut payload = [0; response::MAX_LENENC_SIZE];
        let payload_len =
            response::encode_result_header_payload(result_header_field_count, &mut payload);
        let layout = match response::frame_layout(payload_len) {
            Some(layout) => layout,
            None => return -1,
        };
        wire_len = match wire_len.checked_add(layout.wire_len) {
            Some(len) => len,
            None => return -1,
        };
    }
    for index in 0..field_count {
        let field = match unsafe { ffi_mysql_field_plan(fields, index) } {
            Some(field) => field,
            None => return -1,
        };
        wire_len = match wire_len.checked_add(field.frame.wire_len) {
            Some(len) => len,
            None => return -1,
        };
    }
    let eof = response::encode_eof_payload(eof_field_count, warnings, status_flags);
    wire_len = match response::frame_layout(eof.len())
        .and_then(|layout| wire_len.checked_add(layout.wire_len))
        .filter(|len| *len <= isize::MAX as usize)
    {
        Some(len) => len,
        None => return -1,
    };
    let framed_len_i64 = match i64::try_from(wire_len) {
        Ok(len) => len,
        Err(_) => return -1,
    };

    let old_len = g.response.batch.len();
    let new_len = match old_len.checked_add(wire_len) {
        Some(len) if len <= isize::MAX as usize => len,
        _ => return -1,
    };
    if reserve_response_output(&mut g.response.batch, wire_len).is_err() {
        return -1;
    }
    let first_seq = g.response.next_mysql_seq;
    g.response.batch.resize(new_len, 0);
    let next_seq = unsafe {
        encode_resultset_metadata_run(
            &mut g.response.batch[old_len..new_len],
            include_result_header,
            fields,
            field_count,
            eof_field_count,
            warnings,
            status_flags,
            first_seq,
        )
    };
    let next_seq = match next_seq {
        Some(next_seq) => next_seq,
        None => {
            g.response.batch.truncate(old_len);
            return -1;
        }
    };
    g.response.next_mysql_seq = next_seq;
    unsafe {
        packet_count.write(semantic_packet_count);
        framed_len.write(framed_len_i64);
    }
    0
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_result_header(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    field_count: u64,
    framed_len: *mut i64,
) -> c_int {
    let mut payload = [0; response::MAX_LENENC_SIZE];
    let payload_len = response::encode_result_header_payload(field_count, &mut payload);
    unsafe {
        append_fixed_response(
            connection_handle,
            generation,
            &payload[..payload_len],
            framed_len,
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_eof(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    field_count: u8,
    warnings: u16,
    status_flags: u16,
    framed_len: *mut i64,
) -> c_int {
    let payload = response::encode_eof_payload(field_count, warnings, status_flags);
    unsafe { append_fixed_response(connection_handle, generation, &payload, framed_len) }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_prepare_ok(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    statement_id: u32,
    column_count: u16,
    parameter_count: u16,
    warnings: u16,
    framed_len: *mut i64,
) -> c_int {
    let payload =
        response::encode_prepare_ok_payload(statement_id, column_count, parameter_count, warnings);
    unsafe { append_fixed_response(connection_handle, generation, &payload, framed_len) }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `sql_state` and `message` have
/// `sql_state_len` / `message_len` readable bytes for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_error(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    error_code: u16,
    sql_state: *const c_char,
    sql_state_len: i64,
    message: *const c_char,
    message_len: i64,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| {
                encode_mysql_error(
                    buffer,
                    len,
                    error_code,
                    sql_state,
                    sql_state_len,
                    message,
                    message_len,
                    seq,
                    used,
                    next,
                )
            },
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `plugin_name` and `scramble` have
/// `plugin_name_len` / `scramble_len` readable bytes for the duration of the
/// call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_auth_switch(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    plugin_name: *const c_char,
    plugin_name_len: i64,
    scramble: *const c_char,
    scramble_len: i64,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| {
                encode_mysql_auth_switch(
                    buffer,
                    len,
                    plugin_name,
                    plugin_name_len,
                    scramble,
                    scramble_len,
                    seq,
                    used,
                    next,
                )
            },
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `filename` has `filename_len` readable
/// bytes for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_local_infile(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    filename: *const c_char,
    filename_len: i64,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| {
                encode_mysql_local_infile(buffer, len, filename, filename_len, seq, used, next)
            },
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `value` has `value_len` readable bytes
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_string(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    value: *const c_char,
    value_len: i64,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| {
                encode_mysql_string(buffer, len, value, value_len, seq, used, next)
            },
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `ok_view` is null or points to a valid
/// view whose byte views and kv arrays stay readable for the duration of the
/// call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_ok(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    ok_view: *const NioMysqlOkView,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| encode_mysql_ok(buffer, len, ok_view, seq, used, next),
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `field_view` is null or points to a
/// valid view whose byte views stay readable for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_field(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    field_view: *const NioMysqlFieldView,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| {
                encode_mysql_field(buffer, len, field_view, seq, used, next)
            },
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `row_view` is null or points to a valid
/// view whose `cells` array (`cell_count` entries) and cell byte views stay
/// readable for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_row(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    row_view: *const NioMysqlRowView,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| encode_mysql_row(buffer, len, row_view, seq, used, next),
        )
    }
}

/// # Safety
/// `connection_handle` is null or a live handle from
/// `nio_connection_handle_acquire` with no concurrent release; `framed_len` is
/// null or valid for one `i64` write; `blob` has `blob_len` readable bytes for
/// the duration of the call (a blob produced by
/// `nio_encode_mysql_packed_row_blob`).
#[no_mangle]
pub unsafe extern "C" fn nio_response_append_packed_row_blob(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    blob: *const c_char,
    blob_len: i64,
    framed_len: *mut i64,
) -> c_int {
    unsafe {
        append_encoded_response(
            connection_handle,
            generation,
            framed_len,
            |buffer, len, seq, used, next| {
                frame_mysql_packed_row_blob(buffer, len, blob, blob_len, seq, used, next)
            },
        )
    }
}

pub(crate) fn publish_response_batch(conn: &Arc<Conn>, generation: u64, is_final: bool) -> bool {
    let mut g = conn.mu.lock().unwrap();
    debug_assert!(
        g.tls.as_ref().is_none_or(|t| !t.conn.is_handshaking()),
        "response staged during a TLS handshake"
    );
    if !valid_request_generation(conn, generation)
        || !g.response.is_active(generation)
        || g.final_response != FinalResponseState::Idle
        || conn.err.load(Ordering::Acquire)
    {
        return false;
    }
    let mut batch = std::mem::take(&mut g.response.batch);
    let append_result: Result<(), TryReserveError> = if batch.is_empty() {
        Ok(())
    } else if !g.req_compressed && g.output_is_empty() {
        g.clear_transport_output();
        std::mem::swap(&mut g.outbuf, &mut batch);
        Ok(())
    } else {
        append_response_locked(&mut g, &batch)
    };
    batch.clear();
    if batch.capacity() > RETAINED_RESPONSE_CAPACITY.saturating_mul(4) {
        batch.shrink_to(RETAINED_RESPONSE_CAPACITY);
    }
    g.response.batch = batch;
    if append_result.is_err() {
        conn.err.store(true, Ordering::Release);
        g.clear_output();
        drop(g);
        mark_connection_error(conn);
        return false;
    }
    if is_final {
        g.final_response = FinalResponseState::Staged { generation };
    }
    true
}

pub(crate) fn drain_blocking_response(conn: &Arc<Conn>, generation: u64) -> c_int {
    match flush_request(conn, generation) {
        Some(true) => {
            return if conn.err.load(Ordering::Acquire) {
                -1
            } else {
                0
            };
        }
        Some(false) => {}
        None => return -1,
    }
    if !arm_writable(conn, Some(generation)) {
        return -1;
    }
    let _ = conn.waker.wake();
    let mut g = conn.mu.lock().unwrap();
    let registered_waiter = valid_request_generation(conn, generation)
        && !g.transport_is_idle()
        && !conn.err.load(Ordering::Acquire);
    if registered_waiter {
        g.write_waiters += 1;
    }
    while registered_waiter
        && valid_request_generation(conn, generation)
        && !g.transport_is_idle()
        && !conn.err.load(Ordering::Acquire)
    {
        let (next, _timeout) = conn
            .write_done
            .wait_timeout(g, Duration::from_millis(1000))
            .unwrap();
        g = next;
    }
    if registered_waiter {
        debug_assert!(g.write_waiters > 0);
        g.write_waiters -= 1;
    }
    if conn.err.load(Ordering::Acquire) || !valid_request_generation(conn, generation) {
        -1
    } else {
        0
    }
}

/// # Safety
/// `connection_handle` is null or is a live acquired handle and must not be
/// released until this call returns.
#[no_mangle]
pub unsafe extern "C" fn nio_response_flush(
    connection_handle: *mut NioConnectionHandle,
    generation: u64,
    is_final: c_int,
) -> c_int {
    if is_final != 0 && is_final != 1 {
        return -1;
    }
    let conn = match unsafe { connection_handle.as_ref() } {
        Some(handle) if !handle.conn.close_notified.load(Ordering::Acquire) => &handle.conn,
        None => return -1,
        Some(_) => return -1,
    };
    if !publish_response_batch(conn, generation, is_final != 0) {
        return -1;
    }
    if is_final != 0 {
        0
    } else {
        drain_blocking_response(conn, generation)
    }
}
