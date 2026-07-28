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
#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_retry_ctrl.h"
#include "sql/engine/ob_exec_context.h"
#include "observer/mysql/ob_query_retry_ctrl.h"

namespace oceanbase {
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql {

/**
 *
 * DAS cannot unconditionally retry for the error of tablet_location or ls_location, like -4725, -4721,
 * and needs to determine whether the real cause of the error is due to DDL operations or stale location cache.
 * 1. When the table or partition was dropped by DDL, the DAS task cannot be retried.
 * 2. When tablet location cache is stale, tablet location cache should be updated and das task needs to be retried.
 *
 **/
void ObDASRetryCtrl::tablet_location_retry_proc(ObDASRef &das_ref,
                                                ObIDASTaskOp &task_op,
                                                bool &need_retry)
{
  need_retry = false;
  int ret = OB_SUCCESS;
  ObTableID ref_table_id = task_op.get_ref_table_id();
  ObDASLocationRouter &loc_router = DAS_CTX(das_ref.get_exec_ctx()).get_location_router();
  const ObDASTabletLoc *tablet_loc = task_op.get_tablet_loc();
  bool tablet_exist = false;
  schema::ObSchemaGetterGuard schema_guard;
  const schema::ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(tablet_loc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet loc is nullptr", K(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema service", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    // The runtime schema may not be ready.
    task_op.set_errcode(ret);
    LOG_WARN("get runtime schema guard fail", KR(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( ref_table_id, table_schema))) {
    task_op.set_errcode(ret);
    LOG_WARN("failed to get table schema", KR(ret), K(ref_table_id));
  } else if (OB_ISNULL(table_schema)) {
    // table could be dropped
    task_op.set_errcode(OB_TABLE_NOT_EXIST);
    LOG_WARN("table not exist,  maybe dropped by DDL, stop das retry", K(ref_table_id));
  } else if (table_schema->is_vir_table()) {
    // the location of the virtual table can't be refreshed,
    // so when a location exception occurs, virtual table is not retryable
  } else if (OB_FAIL(table_schema->check_if_tablet_exists(tablet_loc->tablet_id_, tablet_exist))) {
    LOG_WARN("failed to check if tablet exists", K(ret), K(tablet_loc), K(ref_table_id));
  } else if (!tablet_exist) {
    // partition could be dropped or table could be truncated, in this case we return OB_SCHEMA_EAGAIN and
    // attempt statement-level retry
    task_op.set_errcode(OB_SCHEMA_EAGAIN);
    LOG_WARN("partition not exist, maybe dropped by DDL or table was truncated", K(tablet_loc), K(ref_table_id));
  } else {
    loc_router.force_refresh_location_cache(true, task_op.get_errcode());
    need_retry = true;
    const ObDASTableLocMeta *loc_meta = tablet_loc->loc_meta_;
    LOG_INFO("[DAS RETRY] refresh tablet location cache and retry DAS task",
             "errcode", task_op.get_errcode(), KPC(loc_meta), KPC(tablet_loc));
  }
}

void ObDASRetryCtrl::tablet_nothing_readable_proc(ObDASRef &das_ref, ObIDASTaskOp &task_op, bool &need_retry)
{
  if (is_virtual_table(task_op.get_ref_table_id())) {
    need_retry = false;
  } else {
    need_retry = true;
  }
}

}  // namespace sql
}  // namespace oceanbase
