// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Generate wire evidence with production sql-nio encoders, without a fake SQL engine.
#![allow(dead_code, unused_imports)]
use std::ffi::{c_char, c_int, c_void};
use std::io::Read;
#[path = "../../rust/sql-nio/src/capability.rs"] mod capability;
#[path = "../../rust/sql-nio/src/codec.rs"] mod codec;
#[path = "../../rust/sql-nio/src/login.rs"] mod login;
use login::NioLoginAttr;
#[path = "../../rust/sql-nio/src/ffi_types.rs"] mod ffi_types;
pub use ffi_types::*;
#[path = "../../rust/sql-nio/src/packet.rs"] mod packet;
#[path = "../../rust/sql-nio/src/handshake.rs"] mod handshake;
#[path = "../../rust/sql-nio/src/response.rs"] mod response;
use response::*;

fn emit(name: &str, bytes: &[u8]) {
    print!("{name} ");
    for byte in bytes { print!("{byte:02x}"); }
    println!();
}

fn main() {
    if std::env::args().nth(1).as_deref() == Some("parse-login") {
        let mut bytes = Vec::new();
        std::io::stdin().read_to_end(&mut bytes).unwrap();
        let parsed = login::parse_login(&bytes, capability::server_capabilities(false)).unwrap();
        for (name, (offset, length)) in [("user", parsed.user), ("auth", parsed.auth),
                                        ("db", parsed.db), ("plugin", parsed.plugin)] {
            if length >= 0 { emit(name, &bytes[offset as usize..(offset + length) as usize]); }
        }
        emit("capabilities", &parsed.transport_caps.to_le_bytes());
        emit("charset", &[parsed.charset]);
        return;
    }
    let mut scramble = [0u8; 20];
    for (i, value) in scramble.iter_mut().enumerate() { *value = i as u8; }
    scramble[19] = 0; // Both leading and trailing binary zero are significant.
    emit("greeting", &handshake::build_greeting(0xfedcba98, &scramble, b"5.7.25",
                                               capability::server_capabilities(false), 2));
    let mut switch = vec![0; auth_switch_payload_len(21, 20).unwrap()];
    encode_auth_switch_payload(b"mysql_native_password", &scramble, &mut switch).unwrap();
    emit("auth_switch", &switch);
    let mut header = [0; MAX_LENENC_SIZE];
    let size = encode_result_header_payload(3, &mut header);
    emit("header", &header[..size]);
    for (name, field_name, field_type, charset) in [
        ("integer_column", "编号", 8, 63),
        ("binary_column", "bytes", 252, 63),
        ("nullable_column", "optional", 253, 45),
    ] {
        let meta = FieldPayloadMeta {
            schema_len: 4, table_len: 1, org_table_len: 1,
            name_len: field_name.len(), org_name_len: field_name.len(),
            type_owner_len: 0, type_name_len: 0, column_length: 20,
            charset, flags: 32, field_type, default_type: 65535, decimals: 0,
        };
        let plan = plan_field_payload(&meta).unwrap();
        let mut bytes = vec![0; plan.payload_len];
        encode_field_payload_planned(&meta, plan, FieldPayloadBytes {
            schema: b"test", table: b"t", org_table: b"t", name: field_name.as_bytes(),
            org_name: field_name.as_bytes(), type_owner: b"", type_name: b"",
        }, &mut bytes).unwrap();
        emit(name, &bytes);
    }
    let cell = |bytes_len, kind| RowCellMeta {
        bytes_len, kind, value: 0, days: 0, microseconds: 0, year: 0,
        month: 0, day: 0, hour: 0, minute: 0, second: 0, flags: 0, bit_len: 0,
    };
    let cells = [
        RowCell {meta: cell(20, NIO_MYSQL_CELL_LENENC_BYTES), bytes: b"18446744073709551615"},
        RowCell {meta: cell(4, NIO_MYSQL_CELL_LENENC_BYTES), bytes: &[0, 255, 128, 1]},
        RowCell {meta: cell(0, NIO_MYSQL_CELL_NULL), bytes: b""},
    ];
    let meta = RowPayloadMeta {protocol: NIO_MYSQL_ROW_TEXT, cell_count: cells.len()};
    let plan = plan_row_payload(&meta, |i| Some(cells[i].meta)).unwrap();
    let mut row = vec![0; plan.payload_len];
    encode_row_payload_planned(&meta, plan, |i| Some(cells[i]), &mut row).unwrap();
    emit("row", &row);
    emit("eof", &encode_eof_payload(0xfe, 2, 2));
    emit("more_eof", &encode_eof_payload(0xfe, 1, 10));
    let ok = OkPayloadMeta {
        affected_rows: (1u64 << 53) + 1, last_insert_id: u64::MAX - 1,
        capability_flags: CLIENT_PROTOCOL_41 | CLIENT_TRANSACTIONS,
        status_flags: 2, warnings: 3, use_standard_serialize: true,
        schema_changed: false, state_changed: false, message_len: 0,
        changed_schema_len: 0, system_var_count: 0, user_var_count: 0,
    };
    let plan = plan_ok_payload(&ok, |_| None, |_| None).unwrap();
    let mut bytes = vec![0; plan.payload_len];
    encode_ok_payload(&ok, plan, b"", b"", |_| None, |_| None, &mut bytes).unwrap();
    emit("ok", &bytes);
    let message = b"invalid SQL";
    let mut error = vec![0; error_payload_len(message.len()).unwrap()];
    encode_error_payload(1064, b"42000", message, &mut error).unwrap();
    emit("error", &error);
    // Check JS framing against the production encoder, including sequence wrap.
    let mut framed = vec![0; frame_layout(row.len()).unwrap().wire_len];
    framed[4..].copy_from_slice(&row);
    unsafe { frame_payload_in_place(framed.as_mut_ptr(), row.len(), 255, frame_layout(row.len()).unwrap()); }
    emit("framed_row", &framed);
}
