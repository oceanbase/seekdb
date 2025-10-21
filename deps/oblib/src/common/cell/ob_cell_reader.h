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
#include "common/object/ob_object.h"
#include "lib/ob_define.h"
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
  int get_cell(uint64_t &column_id, ObObj &obj, bool *is_row_finished = NULL, ObString *row = NULL);
  int get_cell(const ObObj *&obj, bool *is_row_finished = NULL, ObString *row = NULL);
  int get_cell(uint64_t &column_id, const ObObj *&obj, bool *is_row_finished = NULL, ObString *row = NULL);
  void set_pos(const int64_t pos) { pos_ = pos; }
  int64_t get_pos() const { return pos_; }
private:
  int read_decimal_int(ObObj &);
  int parse(uint64_t *column_id);
  template<class T>
  int read(const T *&ptr);
  inline int is_es_end_object(const common::ObObj &obj, bool &is_end_obj);
private:
  const char *buf_;
  int64_t buf_size_;
  int64_t pos_;
  uint64_t column_id_;
  ObObj obj_;
  ObCompactStoreType store_type_;
  int64_t row_start_;
  bool is_inited_;
};
}//end namespace common
}//end namespace oceanbase
#endif
