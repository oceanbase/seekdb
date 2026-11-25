/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include "sql/ob_sql_context.h"
#include "sql/resolver/ob_resolver_define.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase::sql
{
template <typename Statement>
int create_resolved_statement(ObResolverParams &params, Statement *&stmt)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(params.stmt_factory_->create_stmt(stmt)) || OB_ISNULL(stmt)) {
    ret = common::OB_SQL_RESOLVER_NO_MEMORY;
    LOG_WARN("failed to create statement", K(ret));
  } else {
    stmt->set_query_ctx(params.query_ctx_);
    params.query_ctx_->set_is_prepare_stmt(params.is_prepare_protocol_ && params.is_prepare_stage_);
    params.query_ctx_->set_timezone_info(params.session_info_->get_timezone_info());
    params.query_ctx_->set_sql_stmt_coll_type(params.session_info_->create_obj_print_params().cs_type_);
    if (OB_FAIL(stmt->set_stmt_id())) {
      LOG_WARN("failed to set statement id", K(ret));
    }
  }
  return ret;
}

}
