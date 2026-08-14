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

use core::mem::{align_of, offset_of, size_of};

use crate::login::NioLoginAttr;
use crate::stmt_execute::{
    NioMysqlExecuteParam, NioMysqlExecuteParamMeta, NioMysqlExecuteParseResult,
};
use crate::{
    NioByteView, NioCallbacks, NioGreetingInfo, NioLoginField, NioLoginView, NioMysqlCellView,
    NioMysqlCommandField, NioMysqlCommandView, NioMysqlFieldView, NioMysqlKvView, NioMysqlOkView,
    NioMysqlRowView, NioTlsConfig, NioTlsSessionInfo, NioTlsStringView,
};

const _: () = assert!(crate::NIO_ABI_VERSION == 26);
const _: () = assert!(crate::reactor::NIO_TLS_MIN_TLSV1_3 == 4);

const _: () = assert!(
    crate::reactor::NIO_START_OK == 0
        && crate::reactor::NIO_START_EINVAL == 1
        && crate::reactor::NIO_START_EABI == 2
        && crate::reactor::NIO_START_EADDR == 3
        && crate::reactor::NIO_START_ECALLBACKS == 4
        && crate::reactor::NIO_START_EIO == 5
        && crate::reactor::NIO_START_ETLS == 6
);

const _: () = assert!(size_of::<NioTlsConfig>() == 32 && align_of::<NioTlsConfig>() == 8);
const _: () = assert!(offset_of!(NioTlsConfig, ca_file) == 0);
const _: () = assert!(offset_of!(NioTlsConfig, cert_file) == 8);
const _: () = assert!(offset_of!(NioTlsConfig, key_file) == 16);
const _: () = assert!(offset_of!(NioTlsConfig, min_tls_version) == 24);
const _: () = assert!(offset_of!(NioTlsConfig, reserved) == 25);

const _: () = assert!(size_of::<NioTlsStringView>() == 16 && align_of::<NioTlsStringView>() == 8);
const _: () = assert!(offset_of!(NioTlsStringView, data) == 0);
const _: () = assert!(offset_of!(NioTlsStringView, len) == 8);

const _: () = assert!(size_of::<NioTlsSessionInfo>() == 72 && align_of::<NioTlsSessionInfo>() == 8);
const _: () = assert!(offset_of!(NioTlsSessionInfo, tls_active) == 0);
const _: () = assert!(offset_of!(NioTlsSessionInfo, peer_cert_present) == 1);
const _: () = assert!(offset_of!(NioTlsSessionInfo, peer_cert_verified) == 2);
const _: () = assert!(offset_of!(NioTlsSessionInfo, peer_cert_info_valid) == 3);
const _: () = assert!(offset_of!(NioTlsSessionInfo, reserved) == 4);
const _: () = assert!(offset_of!(NioTlsSessionInfo, cipher_name) == 8);
const _: () = assert!(offset_of!(NioTlsSessionInfo, peer_cert_common_name) == 24);
const _: () = assert!(offset_of!(NioTlsSessionInfo, peer_cert_issuer) == 40);
const _: () = assert!(offset_of!(NioTlsSessionInfo, peer_cert_subject) == 56);

const _: () = assert!(size_of::<NioCallbacks>() == 40 && align_of::<NioCallbacks>() == 8);
const _: () = assert!(offset_of!(NioCallbacks, ctx) == 0);
const _: () = assert!(offset_of!(NioCallbacks, on_connect) == 8);
const _: () = assert!(offset_of!(NioCallbacks, on_readable) == 16);
const _: () = assert!(offset_of!(NioCallbacks, on_disconnect) == 24);
const _: () = assert!(offset_of!(NioCallbacks, on_close) == 32);

const _: () = assert!(size_of::<NioGreetingInfo>() == 104 && align_of::<NioGreetingInfo>() == 8);
const _: () = assert!(offset_of!(NioGreetingInfo, sessid) == 0);
const _: () = assert!(offset_of!(NioGreetingInfo, scramble) == 4);
const _: () = assert!(offset_of!(NioGreetingInfo, version) == 24);
const _: () = assert!(offset_of!(NioGreetingInfo, version_len) == 88);
const _: () = assert!(offset_of!(NioGreetingInfo, status_flags) == 96);
const _: () = assert!(offset_of!(NioGreetingInfo, reserved) == 98);

const _: () = assert!(size_of::<NioLoginField>() == 8 && align_of::<NioLoginField>() == 4);
const _: () = assert!(offset_of!(NioLoginField, off) == 0 && offset_of!(NioLoginField, len) == 4);
const _: () = assert!(size_of::<NioLoginAttr>() == 16 && align_of::<NioLoginAttr>() == 4);
const _: () = assert!(offset_of!(NioLoginAttr, key_off) == 0);
const _: () = assert!(offset_of!(NioLoginAttr, key_len) == 4);
const _: () = assert!(offset_of!(NioLoginAttr, value_off) == 8);
const _: () = assert!(offset_of!(NioLoginAttr, value_len) == 12);
const _: () = assert!(size_of::<NioLoginView>() == 56 && align_of::<NioLoginView>() == 8);
const _: () = assert!(offset_of!(NioLoginView, capabilities) == 0);
const _: () = assert!(offset_of!(NioLoginView, charset) == 4);
const _: () = assert!(offset_of!(NioLoginView, reserved) == 5);
const _: () = assert!(offset_of!(NioLoginView, username) == 8);
const _: () = assert!(offset_of!(NioLoginView, auth_response) == 16);
const _: () = assert!(offset_of!(NioLoginView, database) == 24);
const _: () = assert!(offset_of!(NioLoginView, auth_plugin_name) == 32);
const _: () = assert!(offset_of!(NioLoginView, attr_count) == 40);
const _: () = assert!(offset_of!(NioLoginView, attrs) == 48);

const _: () =
    assert!(size_of::<NioMysqlCommandField>() == 8 && align_of::<NioMysqlCommandField>() == 4);
const _: () = assert!(offset_of!(NioMysqlCommandField, off) == 0);
const _: () = assert!(offset_of!(NioMysqlCommandField, len) == 4);
const _: () =
    assert!(size_of::<NioMysqlCommandView>() == 64 && align_of::<NioMysqlCommandView>() == 8);
const _: () = assert!(offset_of!(NioMysqlCommandView, command) == 0);
const _: () = assert!(offset_of!(NioMysqlCommandView, layout) == 4);
const _: () = assert!(offset_of!(NioMysqlCommandView, scalar0) == 8);
const _: () = assert!(offset_of!(NioMysqlCommandView, scalar1) == 16);
const _: () = assert!(offset_of!(NioMysqlCommandView, fields) == 24);
const _: () = assert!(offset_of!(NioMysqlCommandView, scalar2) == 56);

const _: () = assert!(size_of::<NioByteView>() == 16 && align_of::<NioByteView>() == 8);
const _: () = assert!(offset_of!(NioByteView, data) == 0 && offset_of!(NioByteView, len) == 8);
const _: () = assert!(size_of::<NioMysqlKvView>() == 32 && align_of::<NioMysqlKvView>() == 8);
const _: () = assert!(offset_of!(NioMysqlKvView, key) == 0);
const _: () = assert!(offset_of!(NioMysqlKvView, value) == 16);

const _: () = assert!(size_of::<NioMysqlOkView>() == 96 && align_of::<NioMysqlOkView>() == 8);
const _: () = assert!(offset_of!(NioMysqlOkView, affected_rows) == 0);
const _: () = assert!(offset_of!(NioMysqlOkView, last_insert_id) == 8);
const _: () = assert!(offset_of!(NioMysqlOkView, capability_flags) == 16);
const _: () = assert!(offset_of!(NioMysqlOkView, behavior_flags) == 20);
const _: () = assert!(offset_of!(NioMysqlOkView, status_flags) == 24);
const _: () = assert!(offset_of!(NioMysqlOkView, warnings) == 26);
const _: () = assert!(offset_of!(NioMysqlOkView, reserved) == 28);
const _: () = assert!(offset_of!(NioMysqlOkView, message) == 32);
const _: () = assert!(offset_of!(NioMysqlOkView, changed_schema) == 48);
const _: () = assert!(offset_of!(NioMysqlOkView, system_vars) == 64);
const _: () = assert!(offset_of!(NioMysqlOkView, system_var_count) == 72);
const _: () = assert!(offset_of!(NioMysqlOkView, user_vars) == 80);
const _: () = assert!(offset_of!(NioMysqlOkView, user_var_count) == 88);

const _: () =
    assert!(size_of::<NioMysqlFieldView>() == 136 && align_of::<NioMysqlFieldView>() == 8);
const _: () = assert!(offset_of!(NioMysqlFieldView, schema) == 0);
const _: () = assert!(offset_of!(NioMysqlFieldView, table) == 16);
const _: () = assert!(offset_of!(NioMysqlFieldView, org_table) == 32);
const _: () = assert!(offset_of!(NioMysqlFieldView, name) == 48);
const _: () = assert!(offset_of!(NioMysqlFieldView, org_name) == 64);
const _: () = assert!(offset_of!(NioMysqlFieldView, type_owner) == 80);
const _: () = assert!(offset_of!(NioMysqlFieldView, type_name) == 96);
const _: () = assert!(offset_of!(NioMysqlFieldView, column_length) == 112);
const _: () = assert!(offset_of!(NioMysqlFieldView, charset) == 116);
const _: () = assert!(offset_of!(NioMysqlFieldView, flags) == 118);
const _: () = assert!(offset_of!(NioMysqlFieldView, field_type) == 120);
const _: () = assert!(offset_of!(NioMysqlFieldView, default_type) == 124);
const _: () = assert!(offset_of!(NioMysqlFieldView, decimals) == 128);
const _: () = assert!(offset_of!(NioMysqlFieldView, reserved) == 129);

const _: () = assert!(size_of::<NioMysqlCellView>() == 48 && align_of::<NioMysqlCellView>() == 8);
const _: () = assert!(offset_of!(NioMysqlCellView, bytes) == 0);
const _: () = assert!(offset_of!(NioMysqlCellView, value) == 16);
const _: () = assert!(offset_of!(NioMysqlCellView, days) == 24);
const _: () = assert!(offset_of!(NioMysqlCellView, microseconds) == 28);
const _: () = assert!(offset_of!(NioMysqlCellView, year) == 32);
const _: () = assert!(offset_of!(NioMysqlCellView, month) == 34);
const _: () = assert!(offset_of!(NioMysqlCellView, day) == 35);
const _: () = assert!(offset_of!(NioMysqlCellView, hour) == 36);
const _: () = assert!(offset_of!(NioMysqlCellView, minute) == 37);
const _: () = assert!(offset_of!(NioMysqlCellView, second) == 38);
const _: () = assert!(offset_of!(NioMysqlCellView, kind) == 39);
const _: () = assert!(offset_of!(NioMysqlCellView, flags) == 40);
const _: () = assert!(offset_of!(NioMysqlCellView, bit_len) == 41);
const _: () = assert!(offset_of!(NioMysqlCellView, reserved) == 42);
const _: () = assert!(size_of::<NioMysqlRowView>() == 24 && align_of::<NioMysqlRowView>() == 8);
const _: () = assert!(offset_of!(NioMysqlRowView, cells) == 0);
const _: () = assert!(offset_of!(NioMysqlRowView, cell_count) == 8);
const _: () = assert!(offset_of!(NioMysqlRowView, protocol) == 16);
const _: () = assert!(offset_of!(NioMysqlRowView, reserved) == 20);

const _: () = assert!(
    size_of::<NioMysqlExecuteParamMeta>() == 4 && align_of::<NioMysqlExecuteParamMeta>() == 2
);
const _: () = assert!(offset_of!(NioMysqlExecuteParamMeta, mysql_type) == 0);
const _: () = assert!(offset_of!(NioMysqlExecuteParamMeta, type_flags) == 2);
const _: () = assert!(offset_of!(NioMysqlExecuteParamMeta, reserved) == 3);
const _: () =
    assert!(size_of::<NioMysqlExecuteParam>() == 40 && align_of::<NioMysqlExecuteParam>() == 8);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, value) == 0);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, value_off) == 8);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, value_len) == 12);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, days) == 16);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, microseconds) == 20);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, year) == 24);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, mysql_type) == 26);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, type_flags) == 28);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, kind) == 29);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, flags) == 30);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, month) == 31);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, day) == 32);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, hour) == 33);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, minute) == 34);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, second) == 35);
const _: () = assert!(offset_of!(NioMysqlExecuteParam, reserved) == 36);
const _: () = assert!(
    size_of::<NioMysqlExecuteParseResult>() == 8 && align_of::<NioMysqlExecuteParseResult>() == 1
);
const _: () = assert!(offset_of!(NioMysqlExecuteParseResult, new_params_bound_flag) == 0);
const _: () = assert!(offset_of!(NioMysqlExecuteParseResult, reserved) == 1);

const _: () = assert!(
    crate::stmt_execute::NIO_MYSQL_EXECUTE_PARSE_OK == 0
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT == -1
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_PARSE_MALFORMED == -2
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED == -3
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_PARSE_CAPACITY == -4
);
const _: () = assert!(
    crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_NULL == 1
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_I64 == 2
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_U64 == 3
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_F32_BITS == 4
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_F64_BITS == 5
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_BYTES == 6
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_YEAR == 7
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_DATE == 8
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_DATETIME == 9
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP == 10
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_TIME == 11
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_VALUE_LONG_DATA == 12
);
const _: () = assert!(
    crate::stmt_execute::NIO_MYSQL_EXECUTE_PARAM_UNSIGNED == 1
        && crate::stmt_execute::NIO_MYSQL_EXECUTE_PARAM_NEGATIVE == 2
);

const _: () = assert!(
    crate::NIO_MYSQL_COMMAND_LAYOUT_BYTES == 1
        && crate::NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST == 2
        && crate::NIO_MYSQL_COMMAND_LAYOUT_U32 == 3
        && crate::NIO_MYSQL_COMMAND_LAYOUT_U16 == 4
        && crate::NIO_MYSQL_COMMAND_LAYOUT_FETCH == 5
        && crate::NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA == 6
        && crate::NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER == 7
        && crate::NIO_MYSQL_COMMAND_LAYOUT_EXECUTE == 8
        && crate::NIO_MYSQL_COMMAND_LAYOUT_EMPTY == 9
        && crate::NIO_MYSQL_COMMAND_LAYOUT_U8 == 10
);
const _: () = assert!(
    crate::NIO_MYSQL_CHANGE_USER_HAS_CHARSET == 1
        && crate::NIO_MYSQL_CHANGE_USER_HAS_PLUGIN == 2
        && crate::NIO_MYSQL_CHANGE_USER_HAS_ATTRS == 4
        && crate::NIO_MYSQL_CHANGE_USER_SECURE_AUTH == 8
);
const _: () = assert!(
    crate::NIO_MYSQL_OK_USE_STANDARD_SERIALIZE == 1
        && crate::NIO_MYSQL_OK_SCHEMA_CHANGED == 2
        && crate::NIO_MYSQL_OK_STATE_CHANGED == 4
);
const _: () = assert!(crate::NIO_MYSQL_ROW_TEXT == 0 && crate::NIO_MYSQL_ROW_BINARY == 1);
const _: () = assert!(
    crate::NIO_MYSQL_CELL_NULL == 0
        && crate::NIO_MYSQL_CELL_LENENC_BYTES == 1
        && crate::NIO_MYSQL_CELL_I8 == 2
        && crate::NIO_MYSQL_CELL_I16 == 3
        && crate::NIO_MYSQL_CELL_I32 == 4
        && crate::NIO_MYSQL_CELL_I64 == 5
        && crate::NIO_MYSQL_CELL_F32_BITS == 6
        && crate::NIO_MYSQL_CELL_F64_BITS == 7
        && crate::NIO_MYSQL_CELL_YEAR == 8
        && crate::NIO_MYSQL_CELL_DATE == 9
        && crate::NIO_MYSQL_CELL_DATETIME == 10
        && crate::NIO_MYSQL_CELL_TIME == 11
        && crate::NIO_MYSQL_CELL_BIT == 12
        && crate::NIO_MYSQL_CELL_LEGACY_LENENC_NULL == 13
        && crate::NIO_MYSQL_CELL_TIME_NEGATIVE == 1
);
