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

#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_scan_merge_loser_tree.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

/**
 * ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare
 */

ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare::
  ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare()
{
}

ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare::
  ~ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare()
{
}

int ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeCompare::cmp(
  const ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem &lhs,
  const ObDirectLoadMultipleHeapTableIndexScanMergeLoserTreeItem &rhs,
  int64_t &cmp_ret)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == lhs.index_ || nullptr == rhs.index_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(lhs), K(rhs));
  } else {
    cmp_ret = lhs.index_->tablet_id_.compare(rhs.index_->tablet_id_);
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
