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

#include "storage/direct_load/ob_direct_load_multiple_heap_table.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadMultipleHeapTableIndexBlockReader;

class ObDirectLoadMultipleSSTableIndexEntryCompare
{
  typedef ObDirectLoadMultipleHeapTableIndexBlockReader IndexBlockReader;
public:
  ObDirectLoadMultipleSSTableIndexEntryCompare(int &ret, ObDirectLoadMultipleHeapTable *heap_table,
                                               IndexBlockReader &index_block_reader);
  // for upper_bound
  // for lower_bound
  bool operator()(const ObDirectLoadMultipleHeapTable::IndexEntryIterator &iter,
                  const common::ObTabletID &tablet_id);
private:
  int &ret_;
  ObDirectLoadMultipleHeapTable *heap_table_;
  IndexBlockReader &index_block_reader_;
  const int64_t entries_per_block_;
};

} // namespace storage
} // namespace oceanbase
