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

#ifndef _OB_MYSQL_ROW_H_
#define _OB_MYSQL_ROW_H_

#include "rpc/obmysql/ob_mysql_util.h"


namespace oceanbase
{
namespace common {
class ObIAllocator;
}
namespace obmysql
{

enum class ObMySQLCellValueKind : uint8_t {
  NULL_VALUE = 0,
  LENENC_BYTES = 1,
  I8 = 2,
  I16 = 3,
  I32 = 4,
  I64 = 5,
  F32 = 6,
  F64 = 7,
  YEAR = 8,
  DATE = 9,
  DATETIME = 10,
  TIME = 11,
  BIT = 12,
  LEGACY_LENENC_NULL = 13,
};

/**
 * A protocol-neutral MySQL cell value.  The Rust encoder owns wire details
 * such as length encodings, null bitmaps and byte order.
 *
 * local_len_ deliberately selects local_buf_ without storing a pointer to it,
 * so copying this value never leaves a pointer referring to another object's
 * local scratch buffer.
 */
struct ObMySQLCellValue {
  static const int64_t LOCAL_BUFFER_SIZE = 64;

  ObMySQLCellValue() { reset(); }
  ObMySQLCellValue(const ObMySQLCellValue &other) { assign(other); }
  ObMySQLCellValue &operator=(const ObMySQLCellValue &other) {
    if (this != &other) {
      assign(other);
    }
    return *this;
  }

  void reset() {
    kind_ = ObMySQLCellValueKind::NULL_VALUE;
    value_ = 0;
    days_ = 0;
    microseconds_ = 0;
    bit_len_ = 0;
    year_ = 0;
    month_ = 0;
    day_ = 0;
    hour_ = 0;
    minute_ = 0;
    second_ = 0;
    is_negative_ = 0;
    external_data_ = NULL;
    external_len_ = 0;
    local_len_ = -1;
  }

  void set_borrowed_bytes(const ObMySQLCellValueKind kind, const char *data,
                          const int64_t len) {
    kind_ = kind;
    external_data_ = data;
    external_len_ = len;
    local_len_ = -1;
  }

  char *get_local_buffer() { return local_buf_; }
  const char *get_bytes() const {
    return local_len_ >= 0 ? local_buf_ : external_data_;
  }
  int64_t get_bytes_len() const {
    return local_len_ >= 0 ? local_len_ : external_len_;
  }
  int set_local_bytes(const ObMySQLCellValueKind kind, const int32_t len) {
    int ret = common::OB_SUCCESS;
    if (len < 0 || len > LOCAL_BUFFER_SIZE) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      kind_ = kind;
      external_data_ = NULL;
      external_len_ = 0;
      local_len_ = len;
    }
    return ret;
  }
  bool uses_local_buffer() const { return local_len_ >= 0; }
  // ObSEArray instantiates its diagnostic printer for the element type.
  int64_t to_string(char *, const int64_t) const { return 0; }

  ObMySQLCellValueKind kind_;
  uint64_t value_;
  uint32_t days_;
  uint32_t microseconds_;
  int32_t bit_len_;
  uint16_t year_;
  uint8_t month_;
  uint8_t day_;
  uint8_t hour_;
  uint8_t minute_;
  uint8_t second_;
  uint8_t is_negative_;

private:
  void assign(const ObMySQLCellValue &other) {
    if (other.local_len_ < -1 || other.local_len_ > LOCAL_BUFFER_SIZE) {
      reset();
      return;
    }
    kind_ = other.kind_;
    value_ = other.value_;
    days_ = other.days_;
    microseconds_ = other.microseconds_;
    bit_len_ = other.bit_len_;
    year_ = other.year_;
    month_ = other.month_;
    day_ = other.day_;
    hour_ = other.hour_;
    minute_ = other.minute_;
    second_ = other.second_;
    is_negative_ = other.is_negative_;
    external_data_ = other.external_data_;
    external_len_ = other.external_len_;
    local_len_ = other.local_len_;
    if (local_len_ > 0 && local_len_ <= LOCAL_BUFFER_SIZE) {
      MEMCPY(local_buf_, other.local_buf_, local_len_);
    }
  }

  const char *external_data_;
  int64_t external_len_;
  int32_t local_len_;
  char local_buf_[LOCAL_BUFFER_SIZE];
};

class ObMySQLRow
{
public:
  explicit ObMySQLRow(MYSQL_PROTOCOL_TYPE type) : type_(type), is_packed_(false) {}

public:
  MYSQL_PROTOCOL_TYPE get_protocol_type() const { return type_; }
  int64_t get_cells_count() const { return get_cells_cnt(); }
  virtual int build_cell_value(int64_t idx,
                               common::ObIAllocator &scratch_allocator,
                               ObMySQLCellValue &out) const = 0;
  virtual int get_packed_row_blob(const char *&data, int64_t &len) const = 0;
  bool is_packed() const { return is_packed_; }
  void set_packed(const bool is_packed) { is_packed_ = is_packed; }
protected:
  virtual int64_t get_cells_cnt() const = 0;

protected:
  const MYSQL_PROTOCOL_TYPE type_;
  //parallel encoding of output_expr in advance to speed up packet response
  bool is_packed_;
}; // end class ObMySQLRow

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_ROW_H_ */
