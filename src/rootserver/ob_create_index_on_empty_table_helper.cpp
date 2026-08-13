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

#define USING_LOG_PREFIX RS
#include "rootserver/ob_create_index_on_empty_table_helper.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "rootserver/ob_ddl_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "common/ob_timeout_ctx.h"
#include "share/ob_share_util.h"
#include "share/scn.h"
namespace oceanbase
{
using namespace share;
using namespace share::schema;
namespace rootserver
{
int ObCreateIndexOnEmptyTableHelper::check_create_index_on_empty_table_opt(
    rootserver::ObDDLService &ddl_service,
    ObMySQLTransaction &trans,
    const share::schema::ObSysVariableSchema &sys_var_schema,
    const ObString &database_name,
    const share::schema::ObTableSchema &table_schema,
    ObIndexType index_type,
    const uint64_t executor_data_version,
    const ObSQLMode sql_mode,
    bool &is_create_index_on_empty_table_opt) {
  int ret = OB_SUCCESS;
  is_create_index_on_empty_table_opt = false;
  if (!share::schema::is_index_support_empty_table_opt(index_type) && index_type != ObIndexType::INDEX_TYPE_IS_NOT) {
  } else if (OB_FAIL(ObDDLTaskUtil::check_table_empty(sys_var_schema, database_name,
                                                  table_schema,
                                                  sql_mode,
                                                  is_create_index_on_empty_table_opt))) {
  } else if (!is_create_index_on_empty_table_opt) {
  } else if (OB_FAIL(ddl_service.lock_table(trans, table_schema))) {
    if (OB_TRY_LOCK_ROW_CONFLICT == ret || OB_ERR_EXCLUSIVE_LOCK_CONFLICT == ret || OB_EAGAIN == ret) {
      ret = OB_SUCCESS;
      is_create_index_on_empty_table_opt = false;
    } else {
      LOG_WARN("failed to lock table", KR(ret), K(table_schema));
    }
  } else if (OB_FAIL(ObDDLTaskUtil::check_table_empty(sys_var_schema, database_name,
                                                  table_schema,
                                                  sql_mode,
                                                  is_create_index_on_empty_table_opt))) {
  }
  LOG_TRACE("check_create_index_on_empty_table_opt", K(ret), K(is_create_index_on_empty_table_opt),
    "name_case_mode", sys_var_schema.get_name_case_mode(),
    K(database_name), "table_name", table_schema.get_table_name_str());
  return ret;
}

int ObCreateIndexOnEmptyTableHelper::get_major_frozen_scn(share::SCN &major_frozen_scn)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.rpc_timeout))) {
  } else if (OB_FAIL(OB_TS_MGR.get_gts_sync(ctx.get_timeout(), major_frozen_scn))) {
  }
  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
