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

#include "lib/allocator/page_arena.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDatumRow;

class ObDirectLoadMultipleExternalRow
{
  OB_UNIS_VERSION(1);
public:
  ObDirectLoadMultipleExternalRow();
  void reset();
  void reuse();
  int64_t get_deep_copy_size() const;
  int from_datum_row(const common::ObTabletID &tablet_id, const ObDirectLoadDatumRow &datum_row);
  int to_datum_row(ObDirectLoadDatumRow &datum_row) const;
  OB_INLINE bool is_valid() const
  {
    return tablet_id_.is_valid() && seq_no_.is_valid() && buf_size_ > 0 && nullptr != buf_;
  }
  TO_STRING_KV(K_(tablet_id), K_(seq_no), K_(is_delete), K_(is_ack), K_(buf_size), KP_(buf));

public:
  common::ObArenaAllocator allocator_;
  common::ObTabletID tablet_id_;
  table::ObTableLoadSequenceNo seq_no_;
  bool is_delete_;
  bool is_ack_;
  int64_t buf_size_;
  const char *buf_;
};

} // namespace storage
} // namespace oceanbase
