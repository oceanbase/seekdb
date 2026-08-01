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

#define USING_LOG_PREFIX SQL_EXE

#include "ob_sql_executor_ctx.h"
#include "observer/ob_server.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace sql
{

OB_SERIALIZE_MEMBER(ObSqlExecutorCtx,
                    retry_times_,
                    expected_worker_cnt_,
                    admited_worker_cnt_,
                    query_begin_schema_version_,
                    minimal_worker_cnt_);

ObSqlExecutorCtx::ObSqlExecutorCtx()
    : expected_worker_cnt_(0),
      minimal_worker_cnt_(0),
      admited_worker_cnt_(0),
      retry_times_(0),
      query_begin_schema_version_(-1),
      schema_service_(GCTX.schema_service_)
{
}

ObSqlExecutorCtx::~ObSqlExecutorCtx()
{
}

}/* ns sql*/
}/* ns oceanbase */
