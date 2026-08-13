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

#include "storage/ddl/ob_ddl_dag_thread_pool.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ddl/ob_ddl_insert_dag.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;

int ObDDLDagThreadPool::init(
    const int64_t thread_count,
    ObDDLIndependentDag *ddl_dag,
    data_plane::ObIDirectInsertWorkerContext &worker_context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(thread_count <= 0 || nullptr == ddl_dag)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(thread_count), KP(ddl_dag));
  } else if (OB_FAIL(set_thread_count(thread_count))) {
  } else {
    set_run_wrapper(share::server_runtime());
    ddl_dag_ = ddl_dag;
    worker_context_ = &worker_context;
    is_inited_ = true;
  }
  return ret;
}

void ObDDLDagThreadPool::run1()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else {
    worker_context_->bind_current_thread();
    char thread_name[OB_THREAD_NAME_BUF_LEN] = { 0 };
    snprintf(thread_name, OB_THREAD_NAME_BUF_LEN, "DDL_%ld", ddl_dag_->get_ddl_task_param().ddl_task_id_);
    lib::set_thread_name(thread_name);
    ObCurTraceId::set(ddl_dag_->get_dag_id());
    FLOG_INFO("ddl dag thread start", "thread_idx", get_thread_idx(), KPC(ddl_dag_));
    IGNORE_RETURN ddl_dag_->process();
    FLOG_INFO("ddl dag thread stop", "thread_idx", get_thread_idx(), KPC(ddl_dag_));
  }
}
