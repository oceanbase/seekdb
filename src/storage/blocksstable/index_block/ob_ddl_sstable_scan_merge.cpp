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
#include "storage/blocksstable/index_block/ob_ddl_sstable_scan_merge.h"
namespace oceanbase
{
namespace blocksstable
{

/******************             ObDDLSSTableMergeLoserTreeCompare              **********************/
ObDDLSSTableMergeLoserTreeCompare::ObDDLSSTableMergeLoserTreeCompare()
  : reverse_scan_(false),
    datum_utils_(nullptr)
{
}

ObDDLSSTableMergeLoserTreeCompare::~ObDDLSSTableMergeLoserTreeCompare()
{
  reset();
}

void ObDDLSSTableMergeLoserTreeCompare::reset()
{
  reverse_scan_ = false;
  datum_utils_ = nullptr;
}

int ObDDLSSTableMergeLoserTreeCompare::cmp(const ObDDLSSTableMergeLoserTreeItem &lhs,
                                           const ObDDLSSTableMergeLoserTreeItem &rhs,
                                           int64_t &cmp_ret)
{
  int ret = OB_SUCCESS;
  int tmp_cmp_ret = 0;
  if (OB_UNLIKELY(nullptr == datum_utils_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSSTableScanMergeLoserTreeCompare not init", K(ret), KP(this));
  } else if (OB_UNLIKELY(!lhs.end_key_.is_valid() || !rhs.end_key_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(lhs), K(rhs));
  } else if (OB_FAIL(lhs.end_key_.compare(rhs.end_key_, *datum_utils_, tmp_cmp_ret))) {
    LOG_WARN("fail to compare rowkey", K(ret), K(lhs), K(rhs), KPC(datum_utils_));
  } else {
    cmp_ret = tmp_cmp_ret * (reverse_scan_ ? -1 : 1);
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase
