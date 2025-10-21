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

#pragma once

#include "share/vector/ob_i_vector.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"

namespace oceanbase
{
namespace share
{
class ObTabletCacheInterval;
} // namespace share
namespace storage
{
class ObDirectLoadDatumRow;

class ObDirectLoadIStoreRowIterator
{
public:
  ObDirectLoadIStoreRowIterator() : row_flag_(), column_count_(0), is_batch_result_(false) {}
  virtual ~ObDirectLoadIStoreRowIterator() = default;
  virtual int get_next_row(const ObDirectLoadDatumRow *&datum_row) = 0;
  virtual int get_next_batch(const IVectorPtrs *&vectors, int64_t &batch_size)
  {
    return OB_NOT_SUPPORTED;
  }
  ObDirectLoadRowFlag &get_row_flag() { return row_flag_; }
  const ObDirectLoadRowFlag &get_row_flag() const { return row_flag_; }
  int64_t get_column_count() const { return column_count_; }
  bool is_batch_result() const { return is_batch_result_; }
  virtual share::ObTabletCacheInterval *get_hide_pk_interval() const { return nullptr; }
  virtual bool is_valid() const
  {
    return (!row_flag_.uncontain_hidden_pk_ || nullptr != get_hide_pk_interval()) &&
           column_count_ > 0;
  }
  VIRTUAL_TO_STRING_KV(K_(row_flag),
                       K_(column_count),
                       K_(is_batch_result));
protected:
  ObDirectLoadRowFlag row_flag_;
  int64_t column_count_;
  bool is_batch_result_;
};

} // namespace storage
} // namespace oceanbase
