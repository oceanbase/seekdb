/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Compile-time pin of the hand-maintained C ABI in rust/sql-nio/include/nio.h
// (NIO_ABI_VERSION 24): every struct crossing the FFI is asserted by size,
// alignment, and field offset, and every wire-visible constant by value. The
// Rust twin is rust/sql-nio/src/abi_layout.rs; both sides assert the same
// numbers, so a layout edit that reaches only one side fails that side's
// build. This TU intentionally contains no runtime code.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "nio.h"
#include "rpc/obmysql/ob_mysql_packet.h" // ObClientCapabilityPos

#define NIO_ABI_SIZE_ALIGN(T, sz, al)                                          \
  static_assert(sizeof(T) == (sz), #T " size");                               \
  static_assert(alignof(T) == (al), #T " alignment")
#define NIO_ABI_OFFSET(T, field, off)                                          \
  static_assert(offsetof(T, field) == (off), #T "::" #field " offset")

static_assert(NIO_ABI_VERSION == 24U, "ABI version pinned by this TU");
using NioStartFn = nio_reactor *(*)(const char *, uint32_t,
                                    const nio_callbacks *, size_t, size_t,
                                    size_t, const nio_tls_config *, size_t,
                                    int32_t *, int32_t);
static_assert(std::is_same<decltype(&nio_start), NioStartFn>::value,
              "nio_start argument slots drifted");

// The single cross-language capability vocabulary: nio.h's NIO_CLIENT_* bits
// must agree with C++'s ObClientCapabilityPos (Rust's capability.rs is pinned
// against the same values in abi_layout.rs).
#define NIO_ABI_CAP(nio_bit, pos)                                              \
  static_assert((nio_bit) == (1U << oceanbase::obmysql::pos),                  \
                #nio_bit " vs " #pos)
NIO_ABI_CAP(NIO_CLIENT_COMPRESS, OB_CLIENT_COMPRESS_POS);
NIO_ABI_CAP(NIO_CLIENT_PROTOCOL_41, OB_CLIENT_PROTOCOL_41_POS);
NIO_ABI_CAP(NIO_CLIENT_SSL, OB_CLIENT_SSL_POS);
NIO_ABI_CAP(NIO_CLIENT_MULTI_STATEMENTS, OB_CLIENT_MULTI_STATEMENTS_POS);
NIO_ABI_CAP(NIO_CLIENT_MULTI_RESULTS, OB_CLIENT_MULTI_RESULTS_POS);
NIO_ABI_CAP(NIO_CLIENT_SESSION_TRACK, OB_CLIENT_SESSION_TRACK_POS);
#undef NIO_ABI_CAP

// nio_greeting_info: greeting inputs C++ fills during on_connect (ABI 24).
NIO_ABI_SIZE_ALIGN(nio_greeting_info, 104, 8);
NIO_ABI_OFFSET(nio_greeting_info, sessid, 0);
NIO_ABI_OFFSET(nio_greeting_info, scramble, 4);
NIO_ABI_OFFSET(nio_greeting_info, version, 24);
NIO_ABI_OFFSET(nio_greeting_info, version_len, 88);
NIO_ABI_OFFSET(nio_greeting_info, status_flags, 96);
NIO_ABI_OFFSET(nio_greeting_info, reserved, 98);

// nio_auth_outcome replaced nio_next_read at ABI 22.
static_assert(NIO_AUTH_NO_CHANGE == 0 && NIO_AUTH_OK == 1 &&
                  NIO_AUTH_SWITCH_SENT == 2,
              "auth outcome values");
static_assert(NIO_START_OK == 0 && NIO_START_EINVAL == 1 &&
                  NIO_START_EABI == 2 && NIO_START_EADDR == 3 &&
                  NIO_START_ECALLBACKS == 4 && NIO_START_EIO == 5 &&
                  NIO_START_ETLS == 6,
              "nio_start error codes");

// nio_tls_config: three PEM path pointers (ABI 23).
NIO_ABI_SIZE_ALIGN(nio_tls_config, 24, 8);
NIO_ABI_OFFSET(nio_tls_config, ca_file, 0);
NIO_ABI_OFFSET(nio_tls_config, cert_file, 8);
NIO_ABI_OFFSET(nio_tls_config, key_file, 16);

// nio_frame_result / nio_packet_kind / nio_next_read enumerators.
static_assert(NIO_FRAME_ERROR == -1 && NIO_FRAME_OK == 0 && NIO_FRAME_NEED_MORE == 1,
              "frame result values");
static_assert(NIO_PACKET_LOGIN == 1 && NIO_PACKET_COMMAND == 2 &&
                  NIO_PACKET_AUTH_SWITCH_RESPONSE == 3,
              "packet kind values");
// nio_login_field / nio_login_attr / nio_login_view.
NIO_ABI_SIZE_ALIGN(nio_login_field, 8, 4);
NIO_ABI_OFFSET(nio_login_field, off, 0);
NIO_ABI_OFFSET(nio_login_field, len, 4);
NIO_ABI_SIZE_ALIGN(nio_login_attr, 16, 4);
NIO_ABI_OFFSET(nio_login_attr, key_off, 0);
NIO_ABI_OFFSET(nio_login_attr, key_len, 4);
NIO_ABI_OFFSET(nio_login_attr, value_off, 8);
NIO_ABI_OFFSET(nio_login_attr, value_len, 12);
NIO_ABI_SIZE_ALIGN(nio_login_view, 56, 8);
NIO_ABI_OFFSET(nio_login_view, capabilities, 0);
NIO_ABI_OFFSET(nio_login_view, charset, 4);
NIO_ABI_OFFSET(nio_login_view, reserved, 5);
NIO_ABI_OFFSET(nio_login_view, username, 8);
NIO_ABI_OFFSET(nio_login_view, auth_response, 16);
NIO_ABI_OFFSET(nio_login_view, database, 24);
NIO_ABI_OFFSET(nio_login_view, auth_plugin_name, 32);
NIO_ABI_OFFSET(nio_login_view, attr_count, 40);
NIO_ABI_OFFSET(nio_login_view, attrs, 48);

// nio_mysql_command_field / nio_mysql_command_view and the layout codes.
NIO_ABI_SIZE_ALIGN(nio_mysql_command_field, 8, 4);
NIO_ABI_OFFSET(nio_mysql_command_field, off, 0);
NIO_ABI_OFFSET(nio_mysql_command_field, len, 4);
NIO_ABI_SIZE_ALIGN(nio_mysql_command_view, 64, 8);
NIO_ABI_OFFSET(nio_mysql_command_view, command, 0);
NIO_ABI_OFFSET(nio_mysql_command_view, layout, 4);
NIO_ABI_OFFSET(nio_mysql_command_view, scalar0, 8);
NIO_ABI_OFFSET(nio_mysql_command_view, scalar1, 16);
NIO_ABI_OFFSET(nio_mysql_command_view, fields, 24);
NIO_ABI_OFFSET(nio_mysql_command_view, scalar2, 56);
static_assert(NIO_MYSQL_COMMAND_LAYOUT_BYTES == 1U &&
                  NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST == 2U &&
                  NIO_MYSQL_COMMAND_LAYOUT_U32 == 3U &&
                  NIO_MYSQL_COMMAND_LAYOUT_U16 == 4U &&
                  NIO_MYSQL_COMMAND_LAYOUT_FETCH == 5U &&
                  NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA == 6U &&
                  NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER == 7U &&
                  NIO_MYSQL_COMMAND_LAYOUT_EXECUTE == 8U &&
                  NIO_MYSQL_COMMAND_LAYOUT_EMPTY == 9U &&
                  NIO_MYSQL_COMMAND_LAYOUT_U8 == 10U,
              "command layout codes");
static_assert(NIO_MYSQL_CHANGE_USER_HAS_CHARSET == 1U &&
                  NIO_MYSQL_CHANGE_USER_HAS_PLUGIN == 2U &&
                  NIO_MYSQL_CHANGE_USER_HAS_ATTRS == 4U &&
                  NIO_MYSQL_CHANGE_USER_SECURE_AUTH == 8U,
              "change-user flag values");

// COM_STMT_EXECUTE parameter parsing structs and constants.
NIO_ABI_SIZE_ALIGN(nio_mysql_execute_param_meta, 4, 2);
NIO_ABI_OFFSET(nio_mysql_execute_param_meta, mysql_type, 0);
NIO_ABI_OFFSET(nio_mysql_execute_param_meta, type_flags, 2);
NIO_ABI_OFFSET(nio_mysql_execute_param_meta, reserved, 3);
NIO_ABI_SIZE_ALIGN(nio_mysql_execute_param, 40, 8);
NIO_ABI_OFFSET(nio_mysql_execute_param, value, 0);
NIO_ABI_OFFSET(nio_mysql_execute_param, value_off, 8);
NIO_ABI_OFFSET(nio_mysql_execute_param, value_len, 12);
NIO_ABI_OFFSET(nio_mysql_execute_param, days, 16);
NIO_ABI_OFFSET(nio_mysql_execute_param, microseconds, 20);
NIO_ABI_OFFSET(nio_mysql_execute_param, year, 24);
NIO_ABI_OFFSET(nio_mysql_execute_param, mysql_type, 26);
NIO_ABI_OFFSET(nio_mysql_execute_param, type_flags, 28);
NIO_ABI_OFFSET(nio_mysql_execute_param, kind, 29);
NIO_ABI_OFFSET(nio_mysql_execute_param, flags, 30);
NIO_ABI_OFFSET(nio_mysql_execute_param, month, 31);
NIO_ABI_OFFSET(nio_mysql_execute_param, day, 32);
NIO_ABI_OFFSET(nio_mysql_execute_param, hour, 33);
NIO_ABI_OFFSET(nio_mysql_execute_param, minute, 34);
NIO_ABI_OFFSET(nio_mysql_execute_param, second, 35);
NIO_ABI_OFFSET(nio_mysql_execute_param, reserved, 36);
NIO_ABI_SIZE_ALIGN(nio_mysql_execute_parse_result, 8, 1);
NIO_ABI_OFFSET(nio_mysql_execute_parse_result, new_params_bound_flag, 0);
NIO_ABI_OFFSET(nio_mysql_execute_parse_result, reserved, 1);
static_assert(NIO_MYSQL_EXECUTE_PARSE_OK == 0 &&
                  NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT == -1 &&
                  NIO_MYSQL_EXECUTE_PARSE_MALFORMED == -2 &&
                  NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED == -3 &&
                  NIO_MYSQL_EXECUTE_PARSE_CAPACITY == -4,
              "execute parse result codes");
static_assert(NIO_MYSQL_EXECUTE_VALUE_NULL == 1U && NIO_MYSQL_EXECUTE_VALUE_I64 == 2U &&
                  NIO_MYSQL_EXECUTE_VALUE_U64 == 3U &&
                  NIO_MYSQL_EXECUTE_VALUE_F32_BITS == 4U &&
                  NIO_MYSQL_EXECUTE_VALUE_F64_BITS == 5U &&
                  NIO_MYSQL_EXECUTE_VALUE_BYTES == 6U &&
                  NIO_MYSQL_EXECUTE_VALUE_YEAR == 7U &&
                  NIO_MYSQL_EXECUTE_VALUE_DATE == 8U &&
                  NIO_MYSQL_EXECUTE_VALUE_DATETIME == 9U &&
                  NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP == 10U &&
                  NIO_MYSQL_EXECUTE_VALUE_TIME == 11U &&
                  NIO_MYSQL_EXECUTE_VALUE_LONG_DATA == 12U,
              "execute value kinds");
static_assert(NIO_MYSQL_EXECUTE_PARAM_UNSIGNED == 1U &&
                  NIO_MYSQL_EXECUTE_PARAM_NEGATIVE == 2U,
              "execute param flags");

// nio_byte_view / nio_mysql_kv_view / nio_mysql_ok_view.
NIO_ABI_SIZE_ALIGN(nio_byte_view, 16, 8);
NIO_ABI_OFFSET(nio_byte_view, data, 0);
NIO_ABI_OFFSET(nio_byte_view, len, 8);
NIO_ABI_SIZE_ALIGN(nio_mysql_kv_view, 32, 8);
NIO_ABI_OFFSET(nio_mysql_kv_view, key, 0);
NIO_ABI_OFFSET(nio_mysql_kv_view, value, 16);
static_assert(NIO_MYSQL_OK_USE_STANDARD_SERIALIZE == 1U &&
                  NIO_MYSQL_OK_SCHEMA_CHANGED == 2U && NIO_MYSQL_OK_STATE_CHANGED == 4U,
              "ok behavior flags");
NIO_ABI_SIZE_ALIGN(nio_mysql_ok_view, 96, 8);
NIO_ABI_OFFSET(nio_mysql_ok_view, affected_rows, 0);
NIO_ABI_OFFSET(nio_mysql_ok_view, last_insert_id, 8);
NIO_ABI_OFFSET(nio_mysql_ok_view, capability_flags, 16);
NIO_ABI_OFFSET(nio_mysql_ok_view, behavior_flags, 20);
NIO_ABI_OFFSET(nio_mysql_ok_view, status_flags, 24);
NIO_ABI_OFFSET(nio_mysql_ok_view, warnings, 26);
NIO_ABI_OFFSET(nio_mysql_ok_view, reserved, 28);
NIO_ABI_OFFSET(nio_mysql_ok_view, message, 32);
NIO_ABI_OFFSET(nio_mysql_ok_view, changed_schema, 48);
NIO_ABI_OFFSET(nio_mysql_ok_view, system_vars, 64);
NIO_ABI_OFFSET(nio_mysql_ok_view, system_var_count, 72);
NIO_ABI_OFFSET(nio_mysql_ok_view, user_vars, 80);
NIO_ABI_OFFSET(nio_mysql_ok_view, user_var_count, 88);

// nio_mysql_field_view.
NIO_ABI_SIZE_ALIGN(nio_mysql_field_view, 136, 8);
NIO_ABI_OFFSET(nio_mysql_field_view, schema, 0);
NIO_ABI_OFFSET(nio_mysql_field_view, table, 16);
NIO_ABI_OFFSET(nio_mysql_field_view, org_table, 32);
NIO_ABI_OFFSET(nio_mysql_field_view, name, 48);
NIO_ABI_OFFSET(nio_mysql_field_view, org_name, 64);
NIO_ABI_OFFSET(nio_mysql_field_view, type_owner, 80);
NIO_ABI_OFFSET(nio_mysql_field_view, type_name, 96);
NIO_ABI_OFFSET(nio_mysql_field_view, column_length, 112);
NIO_ABI_OFFSET(nio_mysql_field_view, charset, 116);
NIO_ABI_OFFSET(nio_mysql_field_view, flags, 118);
NIO_ABI_OFFSET(nio_mysql_field_view, field_type, 120);
NIO_ABI_OFFSET(nio_mysql_field_view, default_type, 124);
NIO_ABI_OFFSET(nio_mysql_field_view, decimals, 128);
NIO_ABI_OFFSET(nio_mysql_field_view, reserved, 129);

// nio_mysql_cell_view / nio_mysql_row_view and the row/cell kind values.
static_assert(NIO_MYSQL_ROW_TEXT == 0U && NIO_MYSQL_ROW_BINARY == 1U, "row protocols");
static_assert(NIO_MYSQL_CELL_NULL == 0U && NIO_MYSQL_CELL_LENENC_BYTES == 1U &&
                  NIO_MYSQL_CELL_I8 == 2U && NIO_MYSQL_CELL_I16 == 3U &&
                  NIO_MYSQL_CELL_I32 == 4U && NIO_MYSQL_CELL_I64 == 5U &&
                  NIO_MYSQL_CELL_F32_BITS == 6U && NIO_MYSQL_CELL_F64_BITS == 7U &&
                  NIO_MYSQL_CELL_YEAR == 8U && NIO_MYSQL_CELL_DATE == 9U &&
                  NIO_MYSQL_CELL_DATETIME == 10U && NIO_MYSQL_CELL_TIME == 11U &&
                  NIO_MYSQL_CELL_BIT == 12U && NIO_MYSQL_CELL_LEGACY_LENENC_NULL == 13U &&
                  NIO_MYSQL_CELL_TIME_NEGATIVE == 1U,
              "cell kinds");
NIO_ABI_SIZE_ALIGN(nio_mysql_cell_view, 48, 8);
NIO_ABI_OFFSET(nio_mysql_cell_view, bytes, 0);
NIO_ABI_OFFSET(nio_mysql_cell_view, value, 16);
NIO_ABI_OFFSET(nio_mysql_cell_view, days, 24);
NIO_ABI_OFFSET(nio_mysql_cell_view, microseconds, 28);
NIO_ABI_OFFSET(nio_mysql_cell_view, year, 32);
NIO_ABI_OFFSET(nio_mysql_cell_view, month, 34);
NIO_ABI_OFFSET(nio_mysql_cell_view, day, 35);
NIO_ABI_OFFSET(nio_mysql_cell_view, hour, 36);
NIO_ABI_OFFSET(nio_mysql_cell_view, minute, 37);
NIO_ABI_OFFSET(nio_mysql_cell_view, second, 38);
NIO_ABI_OFFSET(nio_mysql_cell_view, kind, 39);
NIO_ABI_OFFSET(nio_mysql_cell_view, flags, 40);
NIO_ABI_OFFSET(nio_mysql_cell_view, bit_len, 41);
NIO_ABI_OFFSET(nio_mysql_cell_view, reserved, 42);
NIO_ABI_SIZE_ALIGN(nio_mysql_row_view, 24, 8);
NIO_ABI_OFFSET(nio_mysql_row_view, cells, 0);
NIO_ABI_OFFSET(nio_mysql_row_view, cell_count, 8);
NIO_ABI_OFFSET(nio_mysql_row_view, protocol, 16);
NIO_ABI_OFFSET(nio_mysql_row_view, reserved, 20);

// nio_callbacks — ctx plus four callback pointers. (on_connect gained the
// greeting out-param at ABI 22; the vtable's shape is unchanged.)
NIO_ABI_SIZE_ALIGN(nio_callbacks, 40, 8);
NIO_ABI_OFFSET(nio_callbacks, ctx, 0);
NIO_ABI_OFFSET(nio_callbacks, on_connect, 8);
NIO_ABI_OFFSET(nio_callbacks, on_readable, 16);
NIO_ABI_OFFSET(nio_callbacks, on_disconnect, 24);
NIO_ABI_OFFSET(nio_callbacks, on_close, 32);

#undef NIO_ABI_OFFSET
#undef NIO_ABI_SIZE_ALIGN
