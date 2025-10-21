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

#include "observer/table_load/ob_table_load_exec_ctx.h"
#include "observer/table_load/ob_table_load_service.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace sql;
using namespace transaction;

/**
 * ObTableLoadExecCtx
 */
ObIAllocator *ObTableLoadExecCtx::get_allocator()
{
  ObIAllocator *allocator = nullptr;
  if (nullptr != exec_ctx_) {
    allocator = &exec_ctx_->get_allocator();
  }
  return allocator;
}

ObSQLSessionInfo *ObTableLoadExecCtx::get_session_info()
{
  ObSQLSessionInfo *session_info = nullptr;
  if (nullptr != exec_ctx_) {
    session_info = exec_ctx_->get_my_session();
  }
  return session_info;
}

ObSchemaGetterGuard *ObTableLoadExecCtx::get_schema_guard()
{
  ObSchemaGetterGuard *schema_guard = nullptr;
  if (nullptr != exec_ctx_ && nullptr != exec_ctx_->get_sql_ctx()) {
    schema_guard = exec_ctx_->get_sql_ctx()->schema_guard_;
  }
  return schema_guard;
}

int ObTableLoadExecCtx::check_status()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(SS_STOPPING == GCTX.status_ || SS_STOPPED == GCTX.status_)) {
    ret = OB_SERVER_IS_STOPPING;
    LOG_WARN("observer is stopped", KR(ret), K(GCTX.status_));
  } else if (OB_FAIL(ObTableLoadService::check_tenant())) {
    LOG_WARN("fail to check tenant", KR(ret));
  } else if (nullptr != tx_desc_ && OB_UNLIKELY(tx_desc_->is_tx_timeout())) {
    ret = OB_TRANS_TIMEOUT;
    LOG_WARN("trans timeout", KR(ret), KPC(tx_desc_));
  } else if (OB_ISNULL(exec_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec_ctx_ is null", KR(ret));
  } else if (OB_FAIL(exec_ctx_->check_status())) {
    LOG_WARN("fail to check exec ctx status", KR(ret));
  }
  return ret;
}

/**
 * ObTableLoadClientExecCtx
 */

int ObTableLoadClientExecCtx::check_status()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableLoadExecCtx::check_status())) {
    LOG_WARN("fail to check status", KR(ret));
  } else if (OB_UNLIKELY(last_heartbeat_time_ + heartbeat_timeout_us_ <
                         ObTimeUtil::current_time())) {
    ret = OB_TIMEOUT;
    LOG_WARN("heartbeat is timeout", KR(ret), K(last_heartbeat_time_), K(heartbeat_timeout_us_));
  }
  return ret;
}

void ObTableLoadClientExecCtx::init_heart_beat(const int64_t heartbeat_timeout_us)
{
  heartbeat_timeout_us_ = heartbeat_timeout_us;
  last_heartbeat_time_ = ObTimeUtil::current_time();
}

void ObTableLoadClientExecCtx::heart_beat()
{
  last_heartbeat_time_ = ObTimeUtil::current_time();
}

} // namespace observer
} // namespace oceanbase
