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

#include "ob_table_meta_processor.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::table;

ObTableMetaP::ObTableMetaP(const ObGlobalContext &gctx):
  ParentType(gctx)
{
}


int ObTableMetaP::try_process()
{
  int ret = OB_SUCCESS;
  exec_ctx_.set_timeout_ts(get_timeout_ts());
  exec_ctx_.get_sess_guard().get_sess_info().set_query_start_time(ObTimeUtility::current_time());
  ObTableMetaRequest &request = arg_;
  ObTableMetaResponse &result = result_;
  ObTableMetaHandlerGuard handler_guard(allocator_);
  ObITableMetaHandler *handler = nullptr;
  uint64_t tenant_id = MTL_ID();
  uint64_t data_version = 0;
  if (OB_FAIL(handler_guard.get_handler(request.meta_type_, handler))) {
    LOG_WARN("failed to get table meta handler", K(ret), K(request.meta_type_));
  } else if (OB_ISNULL(handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null table meta handler", K(ret));
  } else if (OB_FAIL(handler->pre_check())) {
    LOG_WARN("failed to pre_check", K(ret));
  } else if (OB_FAIL(handler->parse(request))) {
    LOG_WARN("failed to parse table meta request", K(ret), K(request));
  } else if (OB_FAIL(handler->handle(exec_ctx_, result))) {
    LOG_WARN("failed to handle table meta request", K(ret), K(request));
  }
  return ret;
}
