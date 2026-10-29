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

namespace oceanbase {
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql {

// A retry must recheck the current schema: dropped tables stop task retries,
// while a removed tablet requires statement-level retry with a fresh plan.
void ObDASRetryCtrl::tablet_access_retry_proc(ObDASRef &das_ref,
                                                ObIDASTaskOp &task_op,
                                                bool &need_retry)
{
  need_retry = false;
  int ret = OB_SUCCESS;
  ObTableID ref_table_id = task_op.get_ref_table_id();
  ObDASCtx &das_ctx = DAS_CTX(das_ref.get_exec_ctx());
  const DASRuntimeTabletAccess *tablet_access = task_op.get_tablet_access();
  bool tablet_exist = false;
  schema::ObSchemaGetterGuard schema_guard;
  const schema::ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(tablet_access)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    // The runtime schema may not be ready.
    task_op.set_errcode(ret);
  } else if (OB_FAIL(schema_guard.get_table_schema( ref_table_id, table_schema))) {
    task_op.set_errcode(ret);
  } else if (OB_ISNULL(table_schema)) {
    // table could be dropped
    task_op.set_errcode(OB_TABLE_NOT_EXIST);
    LOG_WARN("table not exist,  maybe dropped by DDL, stop das retry", K(ref_table_id));
  } else if (table_schema->is_vir_table()) {
    // Virtual table accesses do not support tablet-level retry.
  } else if (OB_FAIL(table_schema->check_if_tablet_exists(tablet_access->tablet_id_, tablet_exist))) {
  } else if (!tablet_exist) {
    // partition could be dropped or table could be truncated, in this case we return OB_SCHEMA_EAGAIN and
    // attempt statement-level retry
    task_op.set_errcode(OB_SCHEMA_EAGAIN);
    LOG_WARN("partition not exist, maybe dropped by DDL or table was truncated", K(tablet_access), K(ref_table_id));
  } else {
    das_ctx.record_retry_error(task_op.get_errcode());
    need_retry = true;
    const DASTableAccessMeta *access_meta = tablet_access->access_meta_;
    LOG_INFO("[DAS RETRY] tablet still exists, retry DAS task",
             "errcode", task_op.get_errcode(), KPC(access_meta), KPC(tablet_access));
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
