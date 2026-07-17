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

#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block.h"

namespace oceanbase
{
namespace storage
{

struct ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem
{
public:
  ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem()
    : index_(nullptr), iter_idx_(0), equal_with_next_(false)
  {
  }
  ~ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem() = default;
  void reset()
  {
    index_ = nullptr;
    iter_idx_ = 0;
    equal_with_next_ = false;
  }
  TO_STRING_KV(KPC_(index), K_(iter_idx), K_(equal_with_next));
public:
  const ObDirectLoadMultipleHeapTableTabletIndex *index_;
  int64_t iter_idx_;
  bool equal_with_next_; // for simple row merger
};

class ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare
{
public:
  ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare();
  ~ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare();
  int cmp(const ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem &lhs,
          const ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem &rhs,
          int64_t &cmp_ret);
};

} // namespace storage
} // namespace oceanbase
