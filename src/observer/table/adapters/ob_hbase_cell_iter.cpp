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

#define USING_LOG_PREFIX SERVER

#include "ob_hbase_cell_iter.h"

namespace oceanbase
{   
namespace table
{

ObHbaseCellIter::ObHbaseCellIter()
    : ObHbaseICellIter(),
      allocator_("HtCelIterAloc", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
      tb_row_iter_(),
      tb_ctx_(allocator_),
      is_opened_(false)
{}

int ObHbaseCellIter::open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tb_row_iter_.open())) {
    LOG_WARN("fail to open tb_row_iter", K(ret));
  } else {
    is_opened_ = true;
  }
  return ret;
}

int ObHbaseCellIter::get_next_cell(ObNewRow *&row)
{
  return OB_SUCCESS;
}

int ObHbaseCellIter::rescan(ObHbaseRescanParam &rescan_param)
{
  int ret = OB_SUCCESS;
  tb_ctx_.set_limit(rescan_param.get_limit());
  ObIArray<ObNewRange> &key_ranges = tb_ctx_.get_key_ranges();
  key_ranges.reset();
  tb_ctx_.set_batch_tablet_ids(nullptr);
  if (OB_FAIL(key_ranges.push_back(rescan_param.get_scan_range()))) {
    LOG_WARN("fail to push back scan range", K(ret), K(rescan_param));
  } else if (OB_FAIL(tb_row_iter_.rescan())) {
    LOG_WARN("fail to rescan tb_row_iter", K(ret), K(rescan_param));
  }
  return ret;
}

int ObHbaseCellIter::close()
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    // do nothing
  } else if (OB_FAIL(tb_row_iter_.close())) {
    LOG_WARN("fail to close tb_row_iter", K(ret));
  }
  return ret;
}

}  // end of namespace table
}  // end of namespace oceanbase
