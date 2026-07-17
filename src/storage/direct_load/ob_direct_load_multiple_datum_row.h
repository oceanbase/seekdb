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

#include "share/table/ob_table_load_define.h"
#include "storage/direct_load/ob_direct_load_multiple_datum_rowkey.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDatumRow;

class ObDirectLoadMultipleDatumRow
{
  OB_UNIS_VERSION(1);
public:
  ObDirectLoadMultipleDatumRow();
  ~ObDirectLoadMultipleDatumRow();
  void reset();
  void reuse();
  int64_t get_deep_copy_size() const;
  // not deep copy
  int from_datum_row(const ObTabletID &tablet_id,
                     const ObDirectLoadDatumRow &datum_row,
                     const int64_t rowkey_column_count);
  int to_datum_row(ObDirectLoadDatumRow &datum_row) const;
  OB_INLINE bool is_valid() const
  {
    return rowkey_.is_valid() && seq_no_.is_valid() && (buf_size_ == 0 || nullptr != buf_);
  }
  TO_STRING_KV(K_(rowkey),
               K_(seq_no),
               K_(is_delete),
               K_(is_ack),
               K_(buf_size),
               KP_(buf));

public:
  common::ObArenaAllocator allocator_;
  ObDirectLoadMultipleDatumRowkey rowkey_;
  table::ObTableLoadSequenceNo seq_no_;
  bool is_delete_;
  bool is_ack_;
  int64_t buf_size_;
  const char *buf_;
};

} // namespace storage
} // namespace oceanbase
