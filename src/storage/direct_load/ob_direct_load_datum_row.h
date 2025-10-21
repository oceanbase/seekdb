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
#include "storage/blocksstable/ob_storage_datum.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDatumRow
{
public:
  ObDirectLoadDatumRow();
  ~ObDirectLoadDatumRow();
  void reset();
  int init(const int64_t count, ObIAllocator *allocator = nullptr);
  bool is_valid() const { return count_ > 0 && nullptr != storage_datums_ && seq_no_.is_valid(); }
  int64_t get_column_count() const { return count_; }

  DECLARE_TO_STRING;

public:
  common::ObArenaAllocator allocator_;
  int64_t count_;
  blocksstable::ObStorageDatum *storage_datums_;
  table::ObTableLoadSequenceNo seq_no_;
  bool is_delete_;
  bool is_ack_;
};

} // namespace storage
} // namespace oceanbase
