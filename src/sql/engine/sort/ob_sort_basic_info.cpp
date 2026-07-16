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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_sort_basic_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "share/schema/ob_table_schema.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

bool is_task4_op9_fts_ddl_sort(ObExecContext *exec_ctx)
{
  // Task4 Op9：仅为 FTS 辅助表 DDL 开启自适应排序内存，避免影响普通 SQL。
  bool is_fts_ddl_sort = false;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  const ObPhysicalPlan *phy_plan = nullptr;
  const share::schema::ObTableSchema *ddl_table_schema = nullptr;
  if (nullptr == exec_ctx || nullptr == exec_ctx->get_my_session()
      || !exec_ctx->get_my_session()->get_ddl_info().is_ddl()) {
    // Task4 Op9：非 DDL 排序无需启用该策略。
  } else if (nullptr == (plan_ctx = GET_PHY_PLAN_CTX(*exec_ctx))
             || nullptr == (phy_plan = plan_ctx->get_phy_plan())
             || OB_INVALID_ID == phy_plan->get_ddl_table_id()
             || nullptr == exec_ctx->get_sql_ctx()
             || nullptr == exec_ctx->get_sql_ctx()->schema_guard_) {
    LOG_DEBUG("Task4 Op9 cannot resolve FTS DDL sort target");
  } else {
    const int tmp_ret = exec_ctx->get_sql_ctx()->schema_guard_->get_table_schema(
        phy_plan->get_ddl_table_id(), ddl_table_schema);
    if (OB_SUCCESS != tmp_ret || nullptr == ddl_table_schema) {
      LOG_DEBUG("Task4 Op9 failed to get DDL target schema", K(tmp_ret),
                "ddl_table_id", phy_plan->get_ddl_table_id());
    } else {
      is_fts_ddl_sort = ddl_table_schema->is_fts_index_aux()
          || ddl_table_schema->is_fts_doc_word_aux();
    }
  }
  return is_fts_ddl_sort;
}

OB_SERIALIZE_MEMBER(ObSortFieldCollation, field_idx_, cs_type_, is_ascending_, null_pos_, is_not_null_);

} // end namespace sql
} // end namespace oceanbase
