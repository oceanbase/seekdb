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

#define USING_LOG_PREFIX PL
#include "ob_dbms_index_manager.h"
#include "query/change_stream/ob_change_stream_service.h"
#include "lib/time/ob_time_utility.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::pl;
using namespace oceanbase::sql;

int ObDBMSIndexManager::refresh(
    ObPLExecCtx &ctx,
    sql::ParamStore &params,
    common::ObObj &result,
    const int64_t abs_timeout_us)
{
  LOG_INFO("call dbms_index_manager.refresh");
  int ret = OB_SUCCESS;
  UNUSED(params);
  UNUSED(result);

  query::ObIChangeStreamService *change_stream =
      OB_ISNULL(ctx.exec_ctx_)
          ? nullptr
          : ctx.exec_ctx_->get_change_stream_service();

  if (ObTimeUtility::current_time() >= abs_timeout_us) {
    ret = OB_TIMEOUT;
  } else if (OB_ISNULL(ctx.exec_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(change_stream)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(change_stream->wait_until_refreshed(
                 abs_timeout_us))) {
  }
  return ret;
}
