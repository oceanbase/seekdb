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

#include "ob_trans_stat.h"

namespace oceanbase
{
using namespace common;

namespace transaction
{

void ObTransStat::reset()
{
  is_inited_ = false;
  addr_.reset();
  trans_id_.reset();
  tenant_id_ = OB_INVALID_TENANT_ID;
  is_exiting_ = false;
  is_readonly_ = false;
  has_decided_ = false;
  is_dirty_ = false;
  active_memstore_version_.reset();
  trans_param_.reset();
  ctx_create_time_ = -1;
  expired_time_ = -1;
  refer_ = -1;
  sql_no_ = 0;
  state_ = static_cast<int64_t>(ObTxState::UNKNOWN);
  session_id_ = 0;
  proxy_session_id_ = 0;
  trans_type_ = TransType::UNKNOWN_TRANS;
  part_trans_action_ = ObPartTransAction::UNKNOWN;
  lock_for_read_retry_count_ = 0;
  ctx_addr_ = 0;
  prev_trans_arr_.reset();
  next_trans_arr_.reset();
  prev_trans_commit_count_ = 0;
  ctx_id_ = 0;
  pending_log_size_ = 0;
  flushed_log_size_ = 0;
}



void ObTransLockStat::reset()
{
  is_inited_ = false;
  addr_.reset();
  tenant_id_ = 0;
  memtable_key_.reset();
  session_id_ = 0;
  proxy_session_id_ = 0;
  trans_id_.reset();
  ctx_create_time_ = 0;
  expired_time_ = 0;
}

} // transaction
} // oceanbase
