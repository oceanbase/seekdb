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

#include "ob_mvcc_acc_ctx.h"
#include "storage/memtable/ob_memtable_context.h"
namespace oceanbase
{
using namespace transaction;
namespace memtable
{

int ObMvccMdsFilter::init(ObMvccMdsFilter &mds_filter)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!mds_filter.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(mds_filter));
  } else {
    truncate_part_filter_ = mds_filter.truncate_part_filter_;
    read_info_ = mds_filter.read_info_;
  }
  return ret;
}

int ObMvccAccessCtx::get_write_seq(ObTxSEQ &seq) const
{
  int ret = OB_SUCCESS;
  // for update uk or pk, set branch part to 0, in orer to let tx-callback fall into single list
  if (tx_scn_.support_branch() && write_flag_.is_update_uk()) {
    const int branch = tx_scn_.get_branch();
    if (branch == 0) {
      seq = tx_scn_;
    } else if (OB_UNLIKELY(ObTxDesc::is_alloced_branch_id(branch))) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "external branch not support concurrent update uk / pk", K(ret), KPC(this));
    } else {
      seq = ObTxSEQ(tx_scn_.get_seq(), 0);
    }
  } else {
    seq = tx_scn_;
  }
  return ret;
}
} // memtable
} // oceanbase
