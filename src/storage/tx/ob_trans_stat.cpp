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
