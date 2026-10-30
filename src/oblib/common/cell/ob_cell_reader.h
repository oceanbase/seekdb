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

#ifndef OCEANBASE_COMMON_OB_CELL_READER_H_
#define OCEANBASE_COMMON_OB_CELL_READER_H_
#include "common/cell/ob_cell_writer.h"
#include "common/datum/datum_value_buffer.h"
#include "lib/ob_define.h"
#include <type_traits>
namespace oceanbase
{
namespace common
{
class ObCellReader
{
public:
  ObCellReader();
  virtual ~ObCellReader() {}
  int init(const char *buf, int64_t buf_size, ObCompactStoreType store_type);
  int next_cell();
  int get_cell(uint64_t &column_id, DatumValue &obj, bool *is_row_finished = NULL, ObString *row = NULL);
  int get_cell(const DatumValue *&obj, bool *is_row_finished = NULL, ObString *row = NULL);
  int get_cell(uint64_t &column_id, const DatumValue *&obj, bool *is_row_finished = NULL, ObString *row = NULL);
  void set_pos(const int64_t pos) { pos_ = pos; }
  int64_t get_pos() const { return pos_; }
private:
  int read_decimal_int(DatumValue &);
  template<typename T> void set_scalar(DatumValue &output, T value, void (SqlTypeMeta::*setter)())
  {
    (output.meta_.*setter)();
    MEMSET(scalar_payload_, 0, sizeof(scalar_payload_));
    const auto mapping = ObDatum::get_sql_value_datum_map_type(output.get_type());
    int64_t bytes = ObDatum::get_reserved_size(mapping);
    if constexpr (std::is_floating_point<T>::value) {
      MEMCPY(scalar_payload_, &value, sizeof(value));
    } else if (bytes == sizeof(uint64_t)) {
      const uint64_t extended = static_cast<uint64_t>(value); MEMCPY(scalar_payload_, &extended, sizeof(extended));
    } else { MEMCPY(scalar_payload_, &value, bytes); }
    output.datum_.set_string(scalar_payload_, bytes);
  }
  void set_string_view(DatumValue &output, const ObString &value, void (SqlTypeMeta::*setter)())
  {
    (output.meta_.*setter)();
    if (output.get_type() == ObVarcharType || output.get_type() == ObCharType) { output.meta_.set_collation_level(CS_LEVEL_IMPLICIT); }
    output.datum_.set_string(value);
  }
  void set_extend_value(DatumValue &output, int64_t value)
  {
    output.meta_.set_ext(); output.datum_.ptr_ = scalar_payload_;
    output.datum_.set_extend(value, 0, 0, output.meta_.get_cs_level(), output.meta_.get_cs_type());
  }
  int parse(uint64_t *column_id);
  template<class T>
  int read(const T *&ptr);
  inline int is_es_end_object(const common::DatumValue &obj, bool &is_end_obj);
private:
  const char *buf_;
  int64_t buf_size_;
  int64_t pos_;
  uint64_t column_id_;
  DatumValue obj_;
  char scalar_payload_[16];
  ObCompactStoreType store_type_;
  int64_t row_start_;
  bool is_inited_;
};
}//end namespace common
}//end namespace oceanbase
#endif
