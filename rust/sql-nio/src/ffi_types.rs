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

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioTlsConfig {
    pub ca_file: *const c_char,
    pub cert_file: *const c_char,
    pub key_file: *const c_char,
    pub min_tls_version: u8,
    pub reserved: [u8; 7],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioTlsStringView {
    pub data: *const c_char,
    pub len: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioTlsSessionInfo {
    pub tls_active: u8,
    pub peer_cert_present: u8,
    pub peer_cert_verified: u8,
    pub peer_cert_info_valid: u8,
    pub reserved: [u8; 4],
    pub cipher_name: NioTlsStringView,
    pub peer_cert_common_name: NioTlsStringView,
    pub peer_cert_issuer: NioTlsStringView,
    pub peer_cert_subject: NioTlsStringView,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioCallbacks {
    pub ctx: *mut c_void,
    pub on_connect: Option<
        extern "C" fn(
            ctx: *mut c_void,
            sess: *mut c_void,
            fd: c_int,
            is_unix: c_int,
            greeting: *mut NioGreetingInfo,
        ) -> c_int,
    >,
    pub on_readable: Option<
        extern "C" fn(
            ctx: *mut c_void,
            sess: *mut c_void,
            body: *mut c_char,
            body_len: i64,
            wire_bytes: u64,
            packet_kind: c_int,
            command_view: *const NioMysqlCommandView,
            generation: u64,
        ) -> c_int,
    >,
    pub on_disconnect: Option<extern "C" fn(ctx: *mut c_void, sess: *mut c_void)>,
    pub on_close: Option<extern "C" fn(ctx: *mut c_void, sess: *mut c_void, err: c_int)>,
}
unsafe impl Send for NioCallbacks {}
unsafe impl Sync for NioCallbacks {}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioGreetingInfo {
    pub sessid: u32,
    pub scramble: [u8; 20],
    pub version: [u8; 64],
    pub version_len: i64,
    pub status_flags: u16,
    pub reserved: [u8; 6],
}

impl NioGreetingInfo {
    pub(crate) fn zeroed() -> Self {
        Self {
            sessid: 0,
            scramble: [0; 20],
            version: [0; 64],
            version_len: 0,
            status_flags: 0,
            reserved: [0; 6],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioLoginField {
    pub off: i32,
    pub len: i32,
}

impl From<(i32, i32)> for NioLoginField {
    fn from((off, len): (i32, i32)) -> Self {
        Self { off, len }
    }
}

#[repr(C)]
pub struct NioLoginView {
    pub capabilities: u32,
    pub charset: u8,
    pub reserved: [u8; 3],
    pub username: NioLoginField,
    pub auth_response: NioLoginField,
    pub database: NioLoginField,
    pub auth_plugin_name: NioLoginField,
    pub attr_count: i32,
    pub attrs: *const NioLoginAttr,
}

pub const NIO_MYSQL_COMMAND_LAYOUT_BYTES: u32 = 1;
pub const NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST: u32 = 2;
pub const NIO_MYSQL_COMMAND_LAYOUT_U32: u32 = 3;
pub const NIO_MYSQL_COMMAND_LAYOUT_U16: u32 = 4;
pub const NIO_MYSQL_COMMAND_LAYOUT_FETCH: u32 = 5;
pub const NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA: u32 = 6;
pub const NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER: u32 = 7;
pub const NIO_MYSQL_COMMAND_LAYOUT_EXECUTE: u32 = 8;
pub const NIO_MYSQL_COMMAND_LAYOUT_EMPTY: u32 = 9;
pub const NIO_MYSQL_COMMAND_LAYOUT_U8: u32 = 10;

pub const NIO_MYSQL_CHANGE_USER_HAS_CHARSET: i64 = 1 << 0;
pub const NIO_MYSQL_CHANGE_USER_HAS_PLUGIN: i64 = 1 << 1;
pub const NIO_MYSQL_CHANGE_USER_HAS_ATTRS: i64 = 1 << 2;
pub const NIO_MYSQL_CHANGE_USER_SECURE_AUTH: i64 = 1 << 3;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct NioMysqlCommandField {
    pub off: i32,
    pub len: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NioMysqlCommandView {
    pub command: u32,
    pub layout: u32,
    pub scalar0: i64,
    pub scalar1: i64,
    pub fields: [NioMysqlCommandField; 4],
    pub scalar2: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioByteView {
    pub data: *const c_char,
    pub len: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioMysqlKvView {
    pub key: NioByteView,
    pub value: NioByteView,
}

pub const NIO_MYSQL_OK_USE_STANDARD_SERIALIZE: u32 = 1 << 0;
pub const NIO_MYSQL_OK_SCHEMA_CHANGED: u32 = 1 << 1;
pub const NIO_MYSQL_OK_STATE_CHANGED: u32 = 1 << 2;
pub(crate) const NIO_MYSQL_OK_KNOWN_BEHAVIOR_FLAGS: u32 =
    NIO_MYSQL_OK_USE_STANDARD_SERIALIZE | NIO_MYSQL_OK_SCHEMA_CHANGED | NIO_MYSQL_OK_STATE_CHANGED;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioMysqlOkView {
    pub affected_rows: u64,
    pub last_insert_id: u64,
    pub capability_flags: u32,
    pub behavior_flags: u32,
    pub status_flags: u16,
    pub warnings: u16,
    pub reserved: u32,
    pub message: NioByteView,
    pub changed_schema: NioByteView,
    pub system_vars: *const NioMysqlKvView,
    pub system_var_count: i64,
    pub user_vars: *const NioMysqlKvView,
    pub user_var_count: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioMysqlFieldView {
    pub schema: NioByteView,
    pub table: NioByteView,
    pub org_table: NioByteView,
    pub name: NioByteView,
    pub org_name: NioByteView,
    pub type_owner: NioByteView,
    pub type_name: NioByteView,
    pub column_length: u32,
    pub charset: u16,
    pub flags: u16,
    pub field_type: u32,
    pub default_type: u32,
    pub decimals: u8,
    pub reserved: [u8; 7],
}

pub const NIO_MYSQL_ROW_TEXT: u32 = 0;
pub const NIO_MYSQL_ROW_BINARY: u32 = 1;

pub const NIO_MYSQL_CELL_NULL: u8 = 0;
pub const NIO_MYSQL_CELL_LENENC_BYTES: u8 = 1;
pub const NIO_MYSQL_CELL_I8: u8 = 2;
pub const NIO_MYSQL_CELL_I16: u8 = 3;
pub const NIO_MYSQL_CELL_I32: u8 = 4;
pub const NIO_MYSQL_CELL_I64: u8 = 5;
pub const NIO_MYSQL_CELL_F32_BITS: u8 = 6;
pub const NIO_MYSQL_CELL_F64_BITS: u8 = 7;
pub const NIO_MYSQL_CELL_YEAR: u8 = 8;
pub const NIO_MYSQL_CELL_DATE: u8 = 9;
pub const NIO_MYSQL_CELL_DATETIME: u8 = 10;
pub const NIO_MYSQL_CELL_TIME: u8 = 11;
pub const NIO_MYSQL_CELL_BIT: u8 = 12;
pub const NIO_MYSQL_CELL_LEGACY_LENENC_NULL: u8 = 13;

pub const NIO_MYSQL_CELL_TIME_NEGATIVE: u8 = 1 << 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioMysqlCellView {
    pub bytes: NioByteView,
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
    pub reserved: [u8; 6],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NioMysqlRowView {
    pub cells: *const NioMysqlCellView,
    pub cell_count: i64,
    pub protocol: u32,
    pub reserved: u32,
}
