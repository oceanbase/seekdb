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

#include "ob_recover_table_persist_helper.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

using namespace oceanbase;
using namespace share;
ObRecoverTablePersistHelper::ObRecoverTablePersistHelper()
  : is_inited_(false), tenant_id_() 
{
}

int ObRecoverTablePersistHelper::init(const uint64_t tenant_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRecoverTablePersistHelper::insert_recover_table_job(
    common::ObISQLClient &proxy, const ObRecoverTableJob &job) const
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

int ObRecoverTablePersistHelper::get_recover_table_job(
    common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id,
    ObRecoverTableJob &job) const
{
  int ret = OB_SUCCESS;
  ObRecoverTableJob::Key key;
  key.tenant_id_ = tenant_id;
  key.job_id_ = job_id;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRecoverTablePersistHelper not init", K(ret));
  } else if (OB_FAIL(table_op_.get_row(proxy, false, key, job))) {
    LOG_WARN("failed to get row", KR(ret), K(key));
  }
  return ret;
}

int ObRecoverTablePersistHelper::is_recover_table_job_exist(
    common::ObISQLClient &proxy, 
    const uint64_t target_tenant_id, 
    bool &is_exist) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
  
int ObRecoverTablePersistHelper::advance_status(
    common::ObISQLClient &proxy, const ObRecoverTableJob &job, const ObRecoverTableStatus &next_status) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRecoverTablePersistHelper::force_cancel_recover_job(common::ObISQLClient &proxy) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRecoverTablePersistHelper::get_all_recover_table_job(
    common::ObISQLClient &proxy,  common::ObIArray<ObRecoverTableJob> &jobs) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRecoverTablePersistHelper::get_recover_table_job_by_initiator(common::ObISQLClient &proxy, 
    const ObRecoverTableJob &initiator_job, ObRecoverTableJob &target_job) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRecoverTablePersistHelper::delete_recover_table_job(
    common::ObISQLClient &proxy, const ObRecoverTableJob &job) const
{
  int ret = OB_SUCCESS;
  int64_t affect_rows = 0;
  ObRecoverTableJob::Key key;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRecoverTablePersistHelper not init", K(ret));
  } else if (OB_FAIL(job.get_pkey(key))) {
    LOG_WARN("failed to get pkey", K(ret));
  } else if (OB_FAIL(table_op_.delete_row(proxy, key, affect_rows))) {
    LOG_WARN("failed to delete row", K(ret), K(job));
  }
  return ret;
}

int ObRecoverTablePersistHelper::insert_recover_table_job_history(
    common::ObISQLClient &proxy, const ObRecoverTableJob &job) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRecoverTablePersistHelper::get_recover_table_job_history_by_initiator(common::ObISQLClient &proxy, 
    const ObRecoverTableJob &initiator_job, ObRecoverTableJob &target_job) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
