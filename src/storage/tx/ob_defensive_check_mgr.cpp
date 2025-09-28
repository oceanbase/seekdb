/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX STORAGE

#include "ob_defensive_check_mgr.h"

namespace oceanbase
{
using namespace common;
using namespace blocksstable;

namespace transaction
{

void SingleRowDefensiveRecord::reset()
{
  generate_ts_ = 0;
  rowkey_.reset();
  row_.reset();
  tablet_id_.reset();
  allocator_.reset();
}

int SingleRowDefensiveRecord::deep_copy(const blocksstable::ObDatumRow &row,
                                        const blocksstable::ObDatumRowkey &rowkey,
                                        const ObDefensiveCheckRecordExtend &extend_info,
                                        const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  
  if (OB_FAIL(rowkey.deep_copy(rowkey_, allocator_))) {
    TRANS_LOG(WARN, "rowkey deep copy error", K(ret), K(rowkey));
  } else if (OB_FAIL(row_.init(row.count_))) {
    TRANS_LOG(WARN, "datum row init error", K(ret), K(row));
  } else if (OB_FAIL(row_.deep_copy(row, allocator_))) {
    TRANS_LOG(WARN, "datum row deep copy error", K(ret), K(row));
  } else {
    extend_info_ = extend_info;
    tablet_id_ = tablet_id;
    generate_ts_ = ObTimeUtility::current_time();
  }

  return ret;
}

void ObSingleTabletDefensiveCheckInfo::reset()
{
  tx_id_.reset();
  for (int64_t i = 0; i < record_arr_.count(); ++i) {
    if (NULL != record_arr_.at(i)) {
      op_free(record_arr_.at(i));
      record_arr_.at(i) = NULL;
    }
  }
}

int ObSingleTabletDefensiveCheckInfo::init(const ObTransID &tx_id)
{
  int ret = OB_SUCCESS;

  if (!tx_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(tx_id));
  } else {
    tx_id_ = tx_id;
  }

  return ret;
}

int ObSingleTabletDefensiveCheckInfo::add_record(SingleRowDefensiveRecord *record)
{
  int ret = OB_SUCCESS;

  if (NULL == record) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(record));
  } else if (OB_FAIL(record_arr_.push_back(record))) {
    TRANS_LOG(WARN, "record arr push back error", K(ret), K(*record));
  } else {
    // do nothing
  }

  return ret;
}

int64_t ObDefensiveCheckMgr::max_record_cnt_ = 128;


void ObDefensiveCheckMgr::reset()
{
  if (is_inited_) {
    map_.reset();
    is_inited_ = false;
  }
}





} /* namespace transaction*/
} /* namespace oceanbase */
