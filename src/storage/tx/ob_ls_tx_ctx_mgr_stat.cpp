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

#include "ob_ls_tx_ctx_mgr_stat.h"

namespace oceanbase
{

using namespace common;
using namespace share;

namespace transaction
{

void ObLSTxCtxMgrStat::reset()
{
  addr_.reset();
  ls_id_.reset();
  is_master_ = false;
  is_stopped_ = false;
  state_ = -1;
  total_tx_ctx_count_ = 0;
  mgr_addr_ = 0;
}

//don't valid input arguments

int ObLSTxCtxMgrStat::init(const common::ObAddr &addr, const share::ObLSID &ls_id,
    const bool is_master, const bool is_stopped,
    const int64_t state,
    const int64_t total_tx_ctx_count, const int64_t mgr_addr)
{
  int ret = OB_SUCCESS;

  addr_ = addr;
  ls_id_ = ls_id;
  is_master_ = is_master;
  is_stopped_ = is_stopped;
  state_ = state;
  total_tx_ctx_count_ = total_tx_ctx_count;
  mgr_addr_ = mgr_addr;

  return ret;
}

} // transaction
} // oceanbase
