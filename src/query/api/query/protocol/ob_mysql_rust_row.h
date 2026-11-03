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
#include "rust/sql_nio.h"
#include "rpc/obmysql/ob_mysql_row.h"

#include <cstddef>
#include <type_traits>

namespace oceanbase {
namespace query {

static_assert(sizeof(oceanbase::nio::NioMysqlCellView) == 48,
              "oceanbase::nio::NioMysqlCellView ABI mismatch");
static_assert(std::is_standard_layout<oceanbase::nio::NioMysqlCellView>::value,
              "oceanbase::nio::NioMysqlCellView must have standard layout");
static_assert(std::is_trivially_copyable<oceanbase::nio::NioMysqlCellView>::value,
              "oceanbase::nio::NioMysqlCellView must be trivially copyable");
static_assert(offsetof(oceanbase::nio::NioMysqlCellView, value) == 16,
              "oceanbase::nio::NioMysqlCellView.value ABI mismatch");
static_assert(offsetof(oceanbase::nio::NioMysqlCellView, year) == 32,
              "oceanbase::nio::NioMysqlCellView.year ABI mismatch");
static_assert(offsetof(oceanbase::nio::NioMysqlCellView, kind) == 39,
              "oceanbase::nio::NioMysqlCellView.kind ABI mismatch");
static_assert(offsetof(oceanbase::nio::NioMysqlCellView, reserved) == 42,
              "oceanbase::nio::NioMysqlCellView.reserved ABI mismatch");
static_assert(sizeof(oceanbase::nio::NioMysqlRowView) == 24,
              "oceanbase::nio::NioMysqlRowView ABI mismatch");
static_assert(offsetof(oceanbase::nio::NioMysqlRowView, cells) == 0,
              "oceanbase::nio::NioMysqlRowView.cells ABI mismatch");
static_assert(offsetof(oceanbase::nio::NioMysqlRowView, protocol) == 16,
              "oceanbase::nio::NioMysqlRowView.protocol ABI mismatch");

static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::NULL_VALUE) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::Null),
              "Rust Row NULL kind mismatch");
static_assert(
    static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::LENENC_BYTES) ==
        static_cast<uint8_t>(oceanbase::nio::CellKind::LenencBytes),
    "Rust Row byte kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I8) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::I8),
              "Rust Row I8 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I16) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::I16),
              "Rust Row I16 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I32) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::I32),
              "Rust Row I32 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::I64) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::I64),
              "Rust Row I64 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::F32) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::F32Bits),
              "Rust Row F32 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::F64) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::F64Bits),
              "Rust Row F64 kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::YEAR) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::Year),
              "Rust Row YEAR kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::DATE) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::Date),
              "Rust Row DATE kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::DATETIME) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::Datetime),
              "Rust Row DATETIME kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::TIME) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::Time),
              "Rust Row TIME kind mismatch");
static_assert(static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::BIT) ==
                  static_cast<uint8_t>(oceanbase::nio::CellKind::Bit),
              "Rust Row BIT kind mismatch");
static_assert(
    static_cast<uint8_t>(obmysql::ObMySQLCellValueKind::LEGACY_LENENC_NULL) ==
        static_cast<uint8_t>(oceanbase::nio::CellKind::LegacyLenencNull),
    "Rust Row legacy-NULL kind mismatch");

inline int build_nio_mysql_cell_view(const obmysql::ObMySQLCellValue &value,
                                     oceanbase::nio::NioMysqlCellView &view) {
  int ret = common::OB_SUCCESS;
  const int64_t bytes_len = value.get_bytes_len();
  if (bytes_len < 0 ||
      (value.uses_local_buffer() &&
       bytes_len > obmysql::ObMySQLCellValue::LOCAL_BUFFER_SIZE) ||
      (bytes_len > 0 && NULL == value.get_bytes()) || value.bit_len_ < 0 ||
      value.bit_len_ > 255 || value.is_negative_ > 1) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    view = oceanbase::nio::NioMysqlCellView{};
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
      view.flags |= static_cast<uint8_t>(oceanbase::nio::CellFlag::Negative);
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
    nio_protocol = static_cast<uint32_t>(oceanbase::nio::RowProtocol::Text);
    break;
  case obmysql::MYSQL_PROTOCOL_TYPE::BINARY:
    nio_protocol = static_cast<uint32_t>(oceanbase::nio::RowProtocol::Binary);
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
