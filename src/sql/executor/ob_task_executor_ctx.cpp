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

#include "ob_task_executor_ctx.h"
#include "observer/ob_server.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace sql
{

int ObTaskExecutorCtx::CalcVirtualPartitionIdParams::init(uint64_t ref_table_id)
{
  int ret = common::OB_SUCCESS;
  if (true == inited_) {
    ret = common::OB_INIT_TWICE;
    LOG_ERROR("init twice", K(ret), K(inited_), K(ref_table_id));
  } else {
    inited_ = true;
    ref_table_id_ = ref_table_id;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTaskExecutorCtx,
                    retry_times_,
                    expected_worker_cnt_,
                    admited_worker_cnt_,
                    query_begin_schema_version_,
                    minimal_worker_cnt_);

ObTaskExecutorCtx::ObTaskExecutorCtx(ObExecContext &exec_context)
    : exec_ctx_(&exec_context),
      expected_worker_cnt_(0),
      minimal_worker_cnt_(0),
      admited_worker_cnt_(0),
      retry_times_(0),
      sys_job_id_(-1),
      query_begin_schema_version_(-1),
      schema_service_(GCTX.schema_service_)
{
}

ObTaskExecutorCtx::~ObTaskExecutorCtx()
{
}

}/* ns sql*/
}/* ns oceanbase */
