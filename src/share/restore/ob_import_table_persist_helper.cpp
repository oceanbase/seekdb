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

#define USING_LOG_PREFIX SHARE

#include "ob_import_table_persist_helper.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

using namespace oceanbase;
using namespace share;
ObImportTableJobPersistHelper::ObImportTableJobPersistHelper()
  : is_inited_(false), tenant_id_(), table_op_()
{
}

int ObImportTableJobPersistHelper::init(const uint64_t tenant_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObImportTableJobPersistHelper::insert_import_table_job(
    common::ObISQLClient &proxy, const ObImportTableJob &job) const
{
  int ret = OB_SUCCESS;
  int64_t affect_rows = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRecoverTablePersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.insert_or_update_row(proxy, job, affect_rows))) {
    LOG_WARN("failed to insert or update row", K(ret), K(job));
  }
  return ret;
}

int ObImportTableJobPersistHelper::get_import_table_job(
    common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id, ObImportTableJob &job) const
{
  int ret = OB_SUCCESS;
  ObImportTableJob::Key key;
  key.tenant_id_ = tenant_id;
  key.job_id_ = job_id;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObImportTableJobPersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.get_row(proxy, false, key, job))) {
    LOG_WARN("failed to get row", KR(ret), K(key));
  }
  return ret;
}

int ObImportTableJobPersistHelper::get_all_import_table_jobs(
    common::ObISQLClient &proxy,  common::ObIArray<ObImportTableJob> &jobs) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
  
int ObImportTableJobPersistHelper::advance_status(
    common::ObISQLClient &proxy, const ObImportTableJob &job, const ObImportTableJobStatus &next_status) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableJobPersistHelper::force_cancel_import_job(common::ObISQLClient &proxy) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableJobPersistHelper::move_import_job_to_history(
    common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableJobPersistHelper::get_import_table_job_history_by_initiator(common::ObISQLClient &proxy, 
    const uint64_t initiator_tenant_id, const uint64_t initiator_job_id, ObImportTableJob &job) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableJobPersistHelper::get_import_table_job_by_initiator(common::ObISQLClient &proxy, 
    const uint64_t initiator_tenant_id, const uint64_t initiator_job_id, ObImportTableJob &job) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableJobPersistHelper::report_import_job_statistics(
    common::ObISQLClient &proxy, const ObImportTableJob &job) const
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObImportTableJobPersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.update_row(proxy, job, affected_rows))) {
    LOG_WARN("failed to compare and swap status", K(ret), K(job));
  }
  return ret;
}

int ObImportTableJobPersistHelper::report_statistics(common::ObISQLClient &proxy, const ObImportTableJob &job) const
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObImportTableTaskPersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.update_row(proxy, job, affected_rows))) {
    LOG_WARN("failed to update row", K(ret), K(job));
  }
  return ret;
}

ObImportTableTaskPersistHelper::ObImportTableTaskPersistHelper()
  : is_inited_(false), tenant_id_(), table_op_()
{
}

int ObImportTableTaskPersistHelper::init(const uint64_t tenant_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObImportTableTaskPersistHelper::insert_import_table_task(
    common::ObISQLClient &proxy, const ObImportTableTask &task) const
{
  int ret = OB_SUCCESS;
  int64_t affect_rows = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRecoverTablePersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.insert_or_update_row(proxy, task, affect_rows))) {
    LOG_WARN("failed to insert or update row", K(ret), K(task));
  }
  return ret;
}

int ObImportTableTaskPersistHelper::get_recover_table_task(
    common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t task_id, ObImportTableTask &task) const
{
  int ret = OB_SUCCESS;
  ObImportTableTask::Key key;
  key.tenant_id_ = tenant_id;
  key.task_id_ = task_id;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObImportTableTaskPersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.get_row(proxy, false, key, task))) {
    LOG_WARN("failed to get row", KR(ret), K(key));
  }
  return ret;
}

int ObImportTableTaskPersistHelper::advance_status(
    common::ObISQLClient &proxy, const ObImportTableTask &task, const ObImportTableTaskStatus &next_status) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableTaskPersistHelper::get_all_import_table_tasks_by_initiator(common::ObISQLClient &proxy, 
    const ObImportTableJob &job, common::ObIArray<ObImportTableTask> &tasks) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableTaskPersistHelper::get_one_batch_unfinish_tasks(common::ObISQLClient &proxy,
    const ObImportTableJob &job, const int64_t k, common::ObIArray<ObImportTableTask> &tasks) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableTaskPersistHelper::move_import_task_to_history(
    common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObImportTableTaskPersistHelper::report_import_task_statistics(
    common::ObISQLClient &proxy, const ObImportTableTask &task) const
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObImportTableTaskPersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.update_row(proxy, task, affected_rows))) {
    LOG_WARN("failed to compare and swap status", K(ret), K(task));
  }
  return ret;
}
