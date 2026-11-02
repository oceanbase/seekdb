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
#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_entry_compare.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block_reader.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

/**
 * ObDirectLoadMultipleSSTableIndexEntryCompare
 */

ObDirectLoadMultipleSSTableIndexEntryCompare::ObDirectLoadMultipleSSTableIndexEntryCompare(
  int &ret, ObDirectLoadMultipleHeapTable *heap_table, IndexBlockReader &index_block_reader)
  : ret_(ret),
    heap_table_(heap_table),
    index_block_reader_(index_block_reader),
    entries_per_block_(ObDirectLoadMultipleHeapTableIndexBlock::get_entries_per_block(
      heap_table->get_meta().index_block_size_))
{
}


bool ObDirectLoadMultipleSSTableIndexEntryCompare::operator()(
  const ObDirectLoadMultipleHeapTable::IndexEntryIterator &iter, const ObTabletID &tablet_id)
{
  int &ret = ret_;
  int cmp_ret = 0;
  const int64_t index_block_idx = iter.index_entry_idx_ / entries_per_block_;
  const int64_t index_block_offset = heap_table_->get_meta().index_block_size_ * index_block_idx;
  const int64_t index_entry_idx = iter.index_entry_idx_ % entries_per_block_;
  const ObDirectLoadMultipleHeapTableTabletIndex *entry = nullptr;
  index_block_reader_.reuse();
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(index_block_reader_.open(heap_table_->get_index_file_handle(),
                                              index_block_offset,
                                              heap_table_->get_meta().index_block_size_))) {
    LOG_WARN("fail to open index file", KR(ret), K(index_block_offset));
  } else if (OB_FAIL(index_block_reader_.get_index(index_entry_idx, entry))) {
    LOG_WARN("fail to get entry", KR(ret));
  } else {
    cmp_ret = entry->tablet_id_.compare(tablet_id);
  }
  return cmp_ret < 0;
}

} // namespace storage
} // namespace oceanbase
