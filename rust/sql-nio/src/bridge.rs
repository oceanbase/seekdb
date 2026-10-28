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

//! CXX interface for the reactor and protocol codecs. Request buffers and
//! connection handles retain their explicit generation/close lifetimes.

use crate::conn::{nio_bind_sql_session, nio_release_sql_session, nio_set_shutdown, nio_shutdown};
#[cfg(feature = "memory-transport")]
use crate::memory_transport::{
    memory_close, memory_connect, memory_read, memory_write, MemoryClient,
};
use crate::pump::{nio_get_login_view, nio_get_tls_session_info};
use crate::reactor::{
    nio_get_bound_tcp_port, nio_start, nio_stop, nio_update_tcp_keepalive_params, nio_wait_destroy,
};
use crate::registry::{nio_connection_handle_acquire, nio_connection_handle_release};
use crate::request::{
    nio_commit_request, nio_interrupt_read, nio_prepare_commit, nio_release_read_packet,
    nio_wait_one_packet,
};
use crate::response_api::{
    nio_response_append_auth_switch, nio_response_append_eof, nio_response_append_error,
    nio_response_append_field, nio_response_append_local_infile, nio_response_append_ok,
    nio_response_append_packed_row_blob, nio_response_append_prepare_ok,
    nio_response_append_result_header, nio_response_append_resultset_metadata,
    nio_response_append_row, nio_response_append_string, nio_response_flush,
};
use crate::row_encode::nio_encode_mysql_packed_row_blob;
use crate::stmt_execute::nio_parse_mysql_execute_params;
use crate::{reactor::NioReactor, registry::NioConnectionHandle};

#[cxx::bridge(namespace = "oceanbase::nio")]
pub mod ffi {
    #[repr(i32)]
    enum ExecuteStatus {
        Ok = 0,
        InvalidArgument = -1,
        Malformed = -2,
        Unsupported = -3,
        Capacity = -4,
    }

    #[repr(u8)]
    enum ExecuteKind {
        Null = 1,
        I64 = 2,
        U64 = 3,
        F32Bits = 4,
        F64Bits = 5,
        Bytes = 6,
        Year = 7,
        Date = 8,
        Datetime = 9,
        Timestamp = 10,
        Time = 11,
        LongData = 12,
    }

    #[repr(u8)]
    enum ExecuteFlag {
        Unsigned = 1,
        Negative = 2,
    }

    #[repr(i32)]
    enum FrameStatus {
        Error = -1,
        Ok = 0,
        NeedMore = 1,
    }

    #[repr(i32)]
    enum AuthStatus {
        NoChange = 0,
        Ok = 1,
        SwitchSent = 2,
    }

    #[repr(u32)]
    enum CommandLayout {
        Bytes = 1,
        FieldList = 2,
        U32 = 3,
        U16 = 4,
        Fetch = 5,
        LongData = 6,
        ChangeUser = 7,
        Execute = 8,
        Empty = 9,
        U8 = 10,
    }

    #[repr(i64)]
    enum ChangeUserFlag {
        HasCharset = 1,
        HasPlugin = 2,
        HasAttrs = 4,
        SecureAuth = 8,
    }

    #[repr(u32)]
    enum OkFlag {
        UseStandardSerialize = 1,
        SchemaChanged = 2,
        StateChanged = 4,
    }

    #[repr(u32)]
    enum RowProtocol {
        Text = 0,
        Binary = 1,
    }

    #[repr(u8)]
    enum CellKind {
        Null = 0,
        LenencBytes = 1,
        I8 = 2,
        I16 = 3,
        I32 = 4,
        I64 = 5,
        F32Bits = 6,
        F64Bits = 7,
        Year = 8,
        Date = 9,
        Datetime = 10,
        Time = 11,
        Bit = 12,
        LegacyLenencNull = 13,
    }

    #[repr(u8)]
    enum CellFlag {
        Negative = 1,
    }

    #[repr(i32)]
    enum StartStatus {
        Ok = 0,
        Einval = 1,
        Eaddr = 3,
        Ecallbacks = 4,
        Eio = 5,
        Etls = 6,
    }

    #[repr(i32)]
    enum PacketKind {
        Login = 1,
        Command = 2,
        AuthSwitchResponse = 3,
    }

    #[repr(u8)]
    enum TlsVersion {
        None = 0,
        Tlsv1 = 1,
        Tlsv11 = 2,
        Tlsv12 = 3,
        Tlsv13 = 4,
    }

    #[derive(Clone, Copy)]
    struct NioTlsConfig {
        ca_file: *const c_char,
        cert_file: *const c_char,
        key_file: *const c_char,
        min_tls_version: u8,
        reserved: [u8; 7],
    }

    #[derive(Clone, Copy)]
    struct NioTlsStringView {
        data: *const c_char,
        len: i64,
    }

    #[derive(Clone, Copy)]
    struct NioTlsSessionInfo {
        tls_active: u8,
        peer_cert_present: u8,
        peer_cert_verified: u8,
        peer_cert_info_valid: u8,
        reserved: [u8; 4],
        cipher_name: NioTlsStringView,
        peer_cert_common_name: NioTlsStringView,
        peer_cert_issuer: NioTlsStringView,
        peer_cert_subject: NioTlsStringView,
    }

    #[derive(Clone, Copy)]
    struct NioGreetingInfo {
        sessid: u32,
        scramble: [u8; 20],
        version: [u8; 64],
        version_len: i64,
        status_flags: u16,
        reserved: [u8; 6],
    }

    #[derive(Clone, Copy)]
    struct NioLoginField {
        off: i32,
        len: i32,
    }

    struct NioLoginView {
        capabilities: u32,
        charset: u8,
        reserved: [u8; 3],
        username: NioLoginField,
        auth_response: NioLoginField,
        database: NioLoginField,
        auth_plugin_name: NioLoginField,
        attr_count: i32,
        attrs: *const NioLoginAttr,
    }

    #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
    struct NioMysqlCommandField {
        off: i32,
        len: i32,
    }

    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    struct NioMysqlCommandView {
        command: u32,
        layout: u32,
        scalar0: i64,
        scalar1: i64,
        fields: [NioMysqlCommandField; 4],
        scalar2: i64,
    }

    #[derive(Clone, Copy)]
    struct NioByteView {
        data: *const c_char,
        len: i64,
    }

    #[derive(Clone, Copy)]
    struct NioMysqlKvView {
        key: NioByteView,
        value: NioByteView,
    }

    #[derive(Clone, Copy)]
    struct NioMysqlOkView {
        affected_rows: u64,
        last_insert_id: u64,
        capability_flags: u32,
        behavior_flags: u32,
        status_flags: u16,
        warnings: u16,
        reserved: u32,
        message: NioByteView,
        changed_schema: NioByteView,
        system_vars: *const NioMysqlKvView,
        system_var_count: i64,
        user_vars: *const NioMysqlKvView,
        user_var_count: i64,
    }

    #[derive(Clone, Copy)]
    struct NioMysqlFieldView {
        schema: NioByteView,
        table: NioByteView,
        org_table: NioByteView,
        name: NioByteView,
        org_name: NioByteView,
        type_owner: NioByteView,
        type_name: NioByteView,
        column_length: u32,
        charset: u16,
        flags: u16,
        field_type: u32,
        default_type: u32,
        decimals: u8,
        reserved: [u8; 7],
    }

    #[derive(Clone, Copy)]
    struct NioMysqlCellView {
        bytes: NioByteView,
        value: u64,
        days: u32,
        microseconds: u32,
        year: u16,
        month: u8,
        day: u8,
        hour: u8,
        minute: u8,
        second: u8,
        kind: u8,
        flags: u8,
        bit_len: u8,
        reserved: [u8; 6],
    }

    #[derive(Clone, Copy)]
    struct NioMysqlRowView {
        cells: *const NioMysqlCellView,
        cell_count: i64,
        protocol: u32,
        reserved: u32,
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    struct NioLoginAttr {
        key_off: i32,
        key_len: i32,
        value_off: i32,
        value_len: i32,
    }

    #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
    struct NioMysqlExecuteParamMeta {
        mysql_type: u16,
        type_flags: u8,
        reserved: u8,
    }

    #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
    struct NioMysqlExecuteParam {
        value: u64,
        value_off: i32,
        value_len: i32,
        days: u32,
        microseconds: u32,
        year: u16,
        mysql_type: u16,
        type_flags: u8,
        kind: u8,
        flags: u8,
        month: u8,
        day: u8,
        hour: u8,
        minute: u8,
        second: u8,
        reserved: [u8; 4],
    }

    #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
    struct NioMysqlExecuteParseResult {
        new_params_bound_flag: u8,
        reserved: [u8; 7],
    }

    #[namespace = "oceanbase::obmysql"]
    unsafe extern "C++" {
        include!("rust/sql-nio/include/callbacks.h");
        type ObSqlSockHandler;
        type ObSqlSockSession;

        unsafe fn ob_sql_sock_handler_on_connect(
            handler: *mut ObSqlSockHandler,
            sess: *mut ObSqlSockSession,
            fd: i32,
            is_unix: i32,
            greeting: *mut NioGreetingInfo,
        ) -> i32;
        unsafe fn ob_sql_sock_handler_on_readable(
            handler: *mut ObSqlSockHandler,
            sess: *mut ObSqlSockSession,
            body: *mut c_char,
            body_len: i64,
            wire_bytes: u64,
            packet_kind: i32,
            command_view: *const NioMysqlCommandView,
            generation: u64,
        ) -> i32;
        unsafe fn ob_sql_sock_handler_on_disconnect(
            handler: *mut ObSqlSockHandler,
            sess: *mut ObSqlSockSession,
        );
        unsafe fn ob_sql_sock_handler_on_close(
            handler: *mut ObSqlSockHandler,
            sess: *mut ObSqlSockSession,
            err: i32,
        );
    }

    extern "Rust" {
        type NioReactor;
        type NioConnectionHandle;

        #[cfg(feature = "memory-transport")]
        type MemoryClient;
        /// Capacity bounds each direction to 1 byte..16 MiB. Null rejects admission.
        #[cfg(feature = "memory-transport")]
        unsafe fn memory_connect(reactor: *const NioReactor, capacity: usize) -> *mut MemoryClient;
        /// Copies bytes; returns a count, -2 for would-block, or -1 for an error.
        #[cfg(feature = "memory-transport")]
        unsafe fn memory_write(
            client: *const MemoryClient,
            buffer: *const c_char,
            length: i64,
        ) -> i64;
        /// Copies bytes; returns a count, 0 for EOF, -2 for would-block, or -1 for an error.
        #[cfg(feature = "memory-transport")]
        unsafe fn memory_read(client: *const MemoryClient, buffer: *mut c_char, length: i64)
            -> i64;
        /// Release once without concurrent calls. A client can outlive its reactor.
        #[cfg(feature = "memory-transport")]
        unsafe fn memory_close(client: *mut MemoryClient);

        #[cxx_name = "get_login_view"]
        unsafe fn nio_get_login_view(
            sess: *mut ObSqlSockSession,
            generation: u64,
            out: *mut NioLoginView,
        ) -> i32;
        #[cxx_name = "get_tls_session_info"]
        unsafe fn nio_get_tls_session_info(
            sess: *mut ObSqlSockSession,
            generation: u64,
            out: *mut NioTlsSessionInfo,
        ) -> i32;
        #[cxx_name = "connection_handle_acquire"]
        unsafe fn nio_connection_handle_acquire(
            sess: *mut ObSqlSockSession,
        ) -> *mut NioConnectionHandle;
        #[cxx_name = "connection_handle_release"]
        unsafe fn nio_connection_handle_release(connection_handle: *mut NioConnectionHandle);
        #[cxx_name = "parse_mysql_execute_params"]
        unsafe fn nio_parse_mysql_execute_params(
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
        ) -> i32;
        #[cxx_name = "response_append_resultset_metadata"]
        unsafe fn nio_response_append_resultset_metadata(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            include_result_header: i32,
            fields: *const NioMysqlFieldView,
            field_count: i64,
            eof_field_count: u8,
            warnings: u16,
            status_flags: u16,
            packet_count: *mut i64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_result_header"]
        unsafe fn nio_response_append_result_header(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            field_count: u64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_eof"]
        unsafe fn nio_response_append_eof(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            field_count: u8,
            warnings: u16,
            status_flags: u16,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_prepare_ok"]
        unsafe fn nio_response_append_prepare_ok(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            statement_id: u32,
            column_count: u16,
            parameter_count: u16,
            warnings: u16,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_error"]
        unsafe fn nio_response_append_error(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            error_code: u16,
            sql_state: *const c_char,
            sql_state_len: i64,
            message: *const c_char,
            message_len: i64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_auth_switch"]
        unsafe fn nio_response_append_auth_switch(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            plugin_name: *const c_char,
            plugin_name_len: i64,
            scramble: *const c_char,
            scramble_len: i64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_local_infile"]
        unsafe fn nio_response_append_local_infile(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            filename: *const c_char,
            filename_len: i64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_string"]
        unsafe fn nio_response_append_string(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            value: *const c_char,
            value_len: i64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_ok"]
        unsafe fn nio_response_append_ok(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            ok_view: *const NioMysqlOkView,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_field"]
        unsafe fn nio_response_append_field(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            field_view: *const NioMysqlFieldView,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_row"]
        unsafe fn nio_response_append_row(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            row_view: *const NioMysqlRowView,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_append_packed_row_blob"]
        unsafe fn nio_response_append_packed_row_blob(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            blob: *const c_char,
            blob_len: i64,
            framed_len: *mut i64,
        ) -> i32;
        #[cxx_name = "response_flush"]
        unsafe fn nio_response_flush(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            is_final: i32,
        ) -> i32;
        #[cxx_name = "encode_mysql_packed_row_blob"]
        unsafe fn nio_encode_mysql_packed_row_blob(
            buffer: *mut c_char,
            buffer_len: i64,
            row_view: *const NioMysqlRowView,
            blob_len: *mut i64,
        ) -> i32;
        #[cxx_name = "wait_one_packet"]
        unsafe fn nio_wait_one_packet(
            sess: *mut ObSqlSockSession,
            generation: u64,
            timeout_us: i64,
            body: *mut *mut c_char,
            body_len: *mut i64,
            packet_lease: *mut u64,
        ) -> i32;
        #[cxx_name = "interrupt_read"]
        unsafe fn nio_interrupt_read(sess: *mut ObSqlSockSession, generation: u64) -> i32;
        #[cxx_name = "release_read_packet"]
        unsafe fn nio_release_read_packet(
            sess: *mut ObSqlSockSession,
            generation: u64,
            packet_lease: u64,
        ) -> i32;
        #[cxx_name = "prepare_commit"]
        unsafe fn nio_prepare_commit(
            connection_handle: *mut NioConnectionHandle,
            generation: u64,
            auth_outcome: i32,
        ) -> i32;
        #[cxx_name = "commit_request"]
        unsafe fn nio_commit_request(connection_handle: *mut NioConnectionHandle, generation: u64);
        #[cxx_name = "set_shutdown"]
        unsafe fn nio_set_shutdown(sess: *mut ObSqlSockSession, generation: u64) -> i32;
        #[cxx_name = "shutdown"]
        unsafe fn nio_shutdown(sess: *mut ObSqlSockSession);
        #[cxx_name = "bind_sql_session"]
        unsafe fn nio_bind_sql_session(sess: *mut ObSqlSockSession);
        #[cxx_name = "release_sql_session"]
        unsafe fn nio_release_sql_session(sess: *mut ObSqlSockSession) -> i32;
        #[cxx_name = "start"]
        unsafe fn nio_start(
            addr: *const c_char,
            handler: *mut ObSqlSockHandler,
            session_size: usize,
            thread_count: usize,
            tls: *const NioTlsConfig,
            out_err: *mut i32,
            disable_tcp: i32,
        ) -> *mut NioReactor;
        #[cxx_name = "get_bound_tcp_port"]
        unsafe fn nio_get_bound_tcp_port(reactor: *const NioReactor) -> u32;
        #[cxx_name = "update_tcp_keepalive_params"]
        unsafe fn nio_update_tcp_keepalive_params(
            reactor: *mut NioReactor,
            enabled: i32,
            idle: u32,
            interval: u32,
            count: u32,
        ) -> i32;
        #[cxx_name = "stop"]
        unsafe fn nio_stop(reactor: *mut NioReactor);
        #[cxx_name = "wait_destroy"]
        unsafe fn nio_wait_destroy(reactor: *mut NioReactor);
    }
}

pub(crate) const NIO_MYSQL_EXECUTE_PARSE_OK: i32 = ffi::ExecuteStatus::Ok.repr;
pub(crate) const NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT: i32 =
    ffi::ExecuteStatus::InvalidArgument.repr;
pub(crate) const NIO_MYSQL_EXECUTE_PARSE_MALFORMED: i32 = ffi::ExecuteStatus::Malformed.repr;
pub(crate) const NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED: i32 = ffi::ExecuteStatus::Unsupported.repr;
pub(crate) const NIO_MYSQL_EXECUTE_PARSE_CAPACITY: i32 = ffi::ExecuteStatus::Capacity.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_NULL: u8 = ffi::ExecuteKind::Null.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_I64: u8 = ffi::ExecuteKind::I64.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_U64: u8 = ffi::ExecuteKind::U64.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_F32_BITS: u8 = ffi::ExecuteKind::F32Bits.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_F64_BITS: u8 = ffi::ExecuteKind::F64Bits.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_BYTES: u8 = ffi::ExecuteKind::Bytes.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_YEAR: u8 = ffi::ExecuteKind::Year.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_DATE: u8 = ffi::ExecuteKind::Date.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_DATETIME: u8 = ffi::ExecuteKind::Datetime.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP: u8 = ffi::ExecuteKind::Timestamp.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_TIME: u8 = ffi::ExecuteKind::Time.repr;
pub(crate) const NIO_MYSQL_EXECUTE_VALUE_LONG_DATA: u8 = ffi::ExecuteKind::LongData.repr;
pub(crate) const NIO_MYSQL_EXECUTE_PARAM_UNSIGNED: u8 = ffi::ExecuteFlag::Unsigned.repr;
pub(crate) const NIO_MYSQL_EXECUTE_PARAM_NEGATIVE: u8 = ffi::ExecuteFlag::Negative.repr;
pub(crate) const NIO_FRAME_ERROR: i32 = ffi::FrameStatus::Error.repr;
pub(crate) const NIO_FRAME_OK: i32 = ffi::FrameStatus::Ok.repr;
pub(crate) const NIO_FRAME_NEED_MORE: i32 = ffi::FrameStatus::NeedMore.repr;
pub(crate) const NIO_AUTH_NO_CHANGE: i32 = ffi::AuthStatus::NoChange.repr;
pub(crate) const NIO_AUTH_OK: i32 = ffi::AuthStatus::Ok.repr;
pub(crate) const NIO_AUTH_SWITCH_SENT: i32 = ffi::AuthStatus::SwitchSent.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_BYTES: u32 = ffi::CommandLayout::Bytes.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST: u32 = ffi::CommandLayout::FieldList.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_U32: u32 = ffi::CommandLayout::U32.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_U16: u32 = ffi::CommandLayout::U16.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_FETCH: u32 = ffi::CommandLayout::Fetch.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA: u32 = ffi::CommandLayout::LongData.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER: u32 = ffi::CommandLayout::ChangeUser.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_EXECUTE: u32 = ffi::CommandLayout::Execute.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_EMPTY: u32 = ffi::CommandLayout::Empty.repr;
pub(crate) const NIO_MYSQL_COMMAND_LAYOUT_U8: u32 = ffi::CommandLayout::U8.repr;
pub(crate) const NIO_MYSQL_CHANGE_USER_HAS_CHARSET: i64 = ffi::ChangeUserFlag::HasCharset.repr;
pub(crate) const NIO_MYSQL_CHANGE_USER_HAS_PLUGIN: i64 = ffi::ChangeUserFlag::HasPlugin.repr;
pub(crate) const NIO_MYSQL_CHANGE_USER_HAS_ATTRS: i64 = ffi::ChangeUserFlag::HasAttrs.repr;
pub(crate) const NIO_MYSQL_CHANGE_USER_SECURE_AUTH: i64 = ffi::ChangeUserFlag::SecureAuth.repr;
pub(crate) const NIO_MYSQL_OK_USE_STANDARD_SERIALIZE: u32 = ffi::OkFlag::UseStandardSerialize.repr;
pub(crate) const NIO_MYSQL_OK_SCHEMA_CHANGED: u32 = ffi::OkFlag::SchemaChanged.repr;
pub(crate) const NIO_MYSQL_OK_STATE_CHANGED: u32 = ffi::OkFlag::StateChanged.repr;
pub(crate) const NIO_MYSQL_ROW_TEXT: u32 = ffi::RowProtocol::Text.repr;
pub(crate) const NIO_MYSQL_ROW_BINARY: u32 = ffi::RowProtocol::Binary.repr;
pub(crate) const NIO_MYSQL_CELL_NULL: u8 = ffi::CellKind::Null.repr;
pub(crate) const NIO_MYSQL_CELL_LENENC_BYTES: u8 = ffi::CellKind::LenencBytes.repr;
pub(crate) const NIO_MYSQL_CELL_I8: u8 = ffi::CellKind::I8.repr;
pub(crate) const NIO_MYSQL_CELL_I16: u8 = ffi::CellKind::I16.repr;
pub(crate) const NIO_MYSQL_CELL_I32: u8 = ffi::CellKind::I32.repr;
pub(crate) const NIO_MYSQL_CELL_I64: u8 = ffi::CellKind::I64.repr;
pub(crate) const NIO_MYSQL_CELL_F32_BITS: u8 = ffi::CellKind::F32Bits.repr;
pub(crate) const NIO_MYSQL_CELL_F64_BITS: u8 = ffi::CellKind::F64Bits.repr;
pub(crate) const NIO_MYSQL_CELL_YEAR: u8 = ffi::CellKind::Year.repr;
pub(crate) const NIO_MYSQL_CELL_DATE: u8 = ffi::CellKind::Date.repr;
pub(crate) const NIO_MYSQL_CELL_DATETIME: u8 = ffi::CellKind::Datetime.repr;
pub(crate) const NIO_MYSQL_CELL_TIME: u8 = ffi::CellKind::Time.repr;
pub(crate) const NIO_MYSQL_CELL_BIT: u8 = ffi::CellKind::Bit.repr;
pub(crate) const NIO_MYSQL_CELL_LEGACY_LENENC_NULL: u8 = ffi::CellKind::LegacyLenencNull.repr;
pub(crate) const NIO_MYSQL_CELL_TIME_NEGATIVE: u8 = ffi::CellFlag::Negative.repr;
pub(crate) const NIO_START_OK: i32 = ffi::StartStatus::Ok.repr;
pub(crate) const NIO_START_EINVAL: i32 = ffi::StartStatus::Einval.repr;
pub(crate) const NIO_START_EADDR: i32 = ffi::StartStatus::Eaddr.repr;
pub(crate) const NIO_START_ECALLBACKS: i32 = ffi::StartStatus::Ecallbacks.repr;
pub(crate) const NIO_START_EIO: i32 = ffi::StartStatus::Eio.repr;
pub(crate) const NIO_START_ETLS: i32 = ffi::StartStatus::Etls.repr;
pub(crate) const NIO_PACKET_LOGIN: i32 = ffi::PacketKind::Login.repr;
pub(crate) const NIO_PACKET_COMMAND: i32 = ffi::PacketKind::Command.repr;
pub(crate) const NIO_PACKET_AUTH_SWITCH_RESPONSE: i32 = ffi::PacketKind::AuthSwitchResponse.repr;
#[cfg(feature = "native-network")]
pub(crate) const NIO_TLS_MIN_TLSV1_3: u8 = ffi::TlsVersion::Tlsv13.repr;

#[derive(Clone, Copy)]
pub(crate) struct Handler(pub(crate) *mut ffi::ObSqlSockHandler);
unsafe impl Send for Handler {}
unsafe impl Sync for Handler {}

impl ffi::NioGreetingInfo {
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

impl From<(i32, i32)> for ffi::NioLoginField {
    fn from((off, len): (i32, i32)) -> Self {
        Self { off, len }
    }
}

pub(crate) const NIO_MYSQL_OK_KNOWN_BEHAVIOR_FLAGS: u32 =
    NIO_MYSQL_OK_USE_STANDARD_SERIALIZE | NIO_MYSQL_OK_SCHEMA_CHANGED | NIO_MYSQL_OK_STATE_CHANGED;
