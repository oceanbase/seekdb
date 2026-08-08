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

#ifndef OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_RUST_ROW_H_
#define OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_RUST_ROW_H_

#include "lib/ob_errno.h"
#include "nio.h"
#include "rpc/obmysql/ob_mysql_row.h"

#include <cstddef>
#include <type_traits>

namespace oceanbase {
namespace query {

static_assert(sizeof(nio_mysql_cell_view) == 48,
              "nio_mysql_cell_view ABI mismatch");
static_assert(std::is_standard_layout<nio_mysql_cell_view>::value,
              "nio_mysql_cell_view must have standard layout");
static_assert(std::is_trivially_copyable<nio_mysql_cell_view>::value,
              "nio_mysql_cell_view must be trivially copyable");
static_assert(offsetof(nio_mysql_cell_view, value) == 16,
              "nio_mysql_cell_view.value ABI mismatch");
static_assert(offsetof(nio_mysql_cell_view, year) == 32,
              "nio_mysql_cell_view.year ABI mismatch");
static_assert(offsetof(nio_mysql_cell_view, kind) == 39,
              "nio_mysql_cell_view.kind ABI mismatch");
static_assert(offsetof(nio_mysql_cell_view, reserved) == 42,
              "nio_mysql_cell_view.reserved ABI mismatch");
static_assert(sizeof(nio_mysql_row_view) == 24,
              "nio_mysql_row_view ABI mismatch");
static_assert(offsetof(nio_mysql_row_view, cells) == 0,
              "nio_mysql_row_view.cells ABI mismatch");
static_assert(offsetof(nio_mysql_row_view, protocol) == 16,
              "nio_mysql_row_view.protocol ABI mismatch");

static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::NULL_VALUE) ==
                  NIO_MYSQL_CELL_NULL,
              "Rust Row NULL kind mismatch");
static_assert(
    static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::LENENC_BYTES) ==
        NIO_MYSQL_CELL_LENENC_BYTES,
    "Rust Row byte kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I8) ==
                  NIO_MYSQL_CELL_I8,
              "Rust Row I8 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I16) ==
                  NIO_MYSQL_CELL_I16,
              "Rust Row I16 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I32) ==
                  NIO_MYSQL_CELL_I32,
              "Rust Row I32 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I64) ==
                  NIO_MYSQL_CELL_I64,
              "Rust Row I64 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::F32) ==
                  NIO_MYSQL_CELL_F32_BITS,
              "Rust Row F32 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::F64) ==
                  NIO_MYSQL_CELL_F64_BITS,
              "Rust Row F64 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::YEAR) ==
                  NIO_MYSQL_CELL_YEAR,
              "Rust Row YEAR kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::DATE) ==
                  NIO_MYSQL_CELL_DATE,
              "Rust Row DATE kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::DATETIME) ==
                  NIO_MYSQL_CELL_DATETIME,
              "Rust Row DATETIME kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::TIME) ==
                  NIO_MYSQL_CELL_TIME,
              "Rust Row TIME kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::BIT) ==
                  NIO_MYSQL_CELL_BIT,
              "Rust Row BIT kind mismatch");
static_assert(
    static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::LEGACY_LENENC_NULL) ==
        NIO_MYSQL_CELL_LEGACY_LENENC_NULL,
    "Rust Row legacy-NULL kind mismatch");

inline int build_nio_mysql_cell_view(const obmysql::ObMySQLCellValue &value,
                                     nio_mysql_cell_view &view) {
  int ret = common::OB_SUCCESS;
  const int64_t bytes_len = value.get_bytes_len();
  if (bytes_len < 0 ||
      (value.uses_local_buffer() &&
       bytes_len > obmysql::ObMySQLCellValue::LOCAL_BUFFER_SIZE) ||
      (bytes_len > 0 && NULL == value.get_bytes()) || value.bit_len_ < 0 ||
      value.bit_len_ > 255 || value.is_negative_ > 1) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    view = nio_mysql_cell_view{};
    view.bytes.data = value.get_bytes();
    view.bytes.len = bytes_len;
    view.value = value.value_;
    view.days = value.days_;
    view.microseconds = value.microseconds_;
    view.year = value.year_;
    view.month = value.month_;
    view.day = value.day_;
    view.hour = value.hour_;
    view.minute = value.minute_;
    view.second = value.second_;
    view.kind = static_cast<uint8_t>(value.kind_);
    if (value.is_negative_ != 0) {
      view.flags |= NIO_MYSQL_CELL_TIME_NEGATIVE;
    }
    view.bit_len = static_cast<uint8_t>(value.bit_len_);
  }
  return ret;
}

inline int
get_nio_mysql_row_protocol(const obmysql::MYSQL_PROTOCOL_TYPE protocol,
                           uint32_t &nio_protocol) {
  int ret = common::OB_SUCCESS;
  switch (protocol) {
  case obmysql::MYSQL_PROTOCOL_TYPE::TEXT:
    nio_protocol = NIO_MYSQL_ROW_TEXT;
    break;
  case obmysql::MYSQL_PROTOCOL_TYPE::BINARY:
    nio_protocol = NIO_MYSQL_ROW_BINARY;
    break;
  default:
    ret = common::OB_INVALID_ARGUMENT;
    break;
  }
  return ret;
}

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_RUST_ROW_H_
