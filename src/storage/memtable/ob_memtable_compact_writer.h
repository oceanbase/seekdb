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

#ifndef OCEANBASE_MEMTABLE_OB_MEMTABLE_COMPACT_WRITER_
#define OCEANBASE_MEMTABLE_OB_MEMTABLE_COMPACT_WRITER_

#include "share/ob_define.h"
#include "lib/allocator/ob_malloc.h"
#include "common/cell/ob_cell_writer.h"

namespace oceanbase
{
namespace memtable
{
class ObMemtableCompactWriter : public common::ObCellWriter
{
public:
  static const int64_t RP_LOCAL_NUM = 0;
  static const int64_t RP_TOTAL_NUM = 128;
  static const int64_t SMALL_BUFFER_SIZE = (1 << 12) - sizeof(common::ObCellWriter) - 16/* sizeof(ObMemtableCompactWriter)*/;
  static const int64_t NORMAL_BUFFER_SIZE = common::OB_MAX_LOG_BUFFER_SIZE;
  static const int64_t BIG_ROW_BUFFER_SIZE = common::OB_MAX_ROW_LENGTH_IN_MEMTABLE;
public:
  ObMemtableCompactWriter();
  ~ObMemtableCompactWriter();
public:
  void set_dense() { common::ObCellWriter::set_store_type(common::DENSE); }
  int64_t get_buf_size() { return buf_size_; }
  virtual bool allow_lob_locator() override { return false; }

private:
  int extend_buf();
private:
  char buf_[SMALL_BUFFER_SIZE];
  char *buffer_;
  int64_t buf_size_;

  DISALLOW_COPY_AND_ASSIGN(ObMemtableCompactWriter);
};
}//namespace memtable
}//namespace memtable
#endif //OCEANBASE_MEMTABLE_OB_MEMTABLE_COMPACT_WRITER_
