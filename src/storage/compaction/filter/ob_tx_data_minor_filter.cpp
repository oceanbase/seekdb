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

#define USING_LOG_PREFIX STORAGE_COMPACTION

#include "storage/compaction/filter/ob_tx_data_minor_filter.h"
#include "storage/ob_i_store.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
using namespace share;

namespace compaction
{

int ObTxDataMinorFilter::init(const SCN &filter_val, const int64_t filter_col_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!filter_val.is_valid() || filter_val.is_min() || filter_col_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(filter_val), K(filter_col_idx));
  } else if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("is inited", K(ret), K(filter_val), K(filter_col_idx));
  } else {
    filter_val_ = filter_val;
    filter_col_idx_ = filter_col_idx;
    max_filtered_end_scn_.set_min();
    is_inited_ = true;
  }
  return ret;
}

int ObTxDataMinorFilter::filter(
    const blocksstable::ObDatumRow &row,
    ObFilterRet &filter_ret)
{
  int ret = OB_SUCCESS;
  filter_ret = FILTER_RET_MAX;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (row.is_uncommitted_row()
      || !row.is_last_multi_version_row()
      || !row.is_first_multi_version_row()
      || row.count_ <= filter_col_idx_) { // not filter uncommitted row
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("uncommitted row in trans state table or multi version row", K(ret), K(row));
  } else {
    const int64_t trans_end_scn_storage_val = row.storage_datums_[filter_col_idx_].get_int();
    SCN trans_end_scn;
    if (OB_FAIL(trans_end_scn.convert_for_tx(trans_end_scn_storage_val))) {
      LOG_WARN("failed to convert for tx", K(ret), K(trans_end_scn_storage_val));
    } else if (trans_end_scn <= filter_val_) {
      filter_ret = FILTER_RET_REMOVE;
      max_filtered_end_scn_ = SCN::max(max_filtered_end_scn_, trans_end_scn);
      LOG_DEBUG("filter row", K(ret), K(row), K(filter_val_));
    } else {
      filter_ret = FILTER_RET_NOT_CHANGE;
    }
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
