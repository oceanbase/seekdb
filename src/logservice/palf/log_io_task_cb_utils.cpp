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

#include "log_io_task_cb_utils.h"

namespace oceanbase
{
using namespace share;
namespace palf
{
FlushLogCbCtx::FlushLogCbCtx()
    : log_id_(OB_INVALID_LOG_ID),
      scn_(),
      lsn_(),
      total_len_(0),
      begin_ts_(OB_INVALID_TIMESTAMP)
{
}

FlushLogCbCtx::~FlushLogCbCtx()
{
  reset();
}

void FlushLogCbCtx::reset()
{
  log_id_ = OB_INVALID_LOG_ID;
  scn_.reset();
  lsn_.reset();
  total_len_ = 0;
  begin_ts_ = OB_INVALID_TIMESTAMP;
}

FlushLogCbCtx& FlushLogCbCtx::operator=(const FlushLogCbCtx &arg)
{
  log_id_ = arg.log_id_;
  scn_ = arg.scn_;
  lsn_ = arg.lsn_;
  total_len_ = arg.total_len_;
  begin_ts_ = arg.begin_ts_;
  return *this;
}

TruncatePrefixBlocksCbCtx::TruncatePrefixBlocksCbCtx(const LSN &lsn) : lsn_(lsn)
{
}

TruncatePrefixBlocksCbCtx::TruncatePrefixBlocksCbCtx() : lsn_()
{
}

TruncatePrefixBlocksCbCtx::~TruncatePrefixBlocksCbCtx()
{
}

void TruncatePrefixBlocksCbCtx::reset()
{
  lsn_.reset();
}

TruncatePrefixBlocksCbCtx& TruncatePrefixBlocksCbCtx::operator=(const TruncatePrefixBlocksCbCtx& truncate_prefix_blocks_ctx)
{
  lsn_ = truncate_prefix_blocks_ctx.lsn_;
  return *this;
}


} // end of logservice
} // end of oceanbase
