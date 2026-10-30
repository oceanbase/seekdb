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
#include "ob_kill_session_arg.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/code_generator/ob_column_index_provider.h"
#include "sql/resolver/cmd/ob_kill_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "share/datum/datum_value_cast.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(ObKillSessionArg,
                    sess_id_,
                    user_id_,
                    is_query_,
                    has_user_super_privilege_);

int ObKillSessionArg::init(ObExecContext &ctx, const ObKillStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  share::schema::ObSchemaGetterGuard *guard = NULL;
  share::schema::ObSessionPrivInfo session_priv;
  share::schema::ObStmtNeedPrivs required(ctx.get_allocator());
  share::schema::ObNeedPriv need_priv;
  need_priv.priv_level_ = share::schema::OB_PRIV_USER_LEVEL;
  need_priv.priv_set_ = OB_PRIV_SUPER;
  if (OB_ISNULL(session) || OB_ISNULL(ctx.get_sql_ctx())
      || OB_ISNULL(guard = ctx.get_sql_ctx()->schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(calculate_sessid(ctx, stmt))) {
  } else if (OB_FAIL(guard->get_session_priv_info(
                        session->get_priv_user_id(), ObString(), session_priv))) {
  } else if (OB_FAIL(required.need_privs_.init(1))) {
  } else if (OB_FAIL(required.need_privs_.push_back(need_priv))) {
  } else {
    // Use the current authorization identity and enabled roles, including SUPER
    // granted through a role. Connection ownership still uses the login user.
    const int priv_ret = guard->check_priv(session_priv, session->get_enable_role_array(), required);
    if (OB_SUCCESS == priv_ret) {
      has_user_super_privilege_ = true;
    } else if (OB_ERR_NO_PRIVILEGE == priv_ret) {
      has_user_super_privilege_ = false;
    } else {
      ret = priv_ret;
    }
    if (OB_SUCC(ret)) {
      user_id_ = session->get_user_id();
      is_query_ = stmt.is_query();
    }
  }
  return ret;
}

int ObKillSessionArg::calculate_sessid(ObExecContext &ctx, const ObKillStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
  HEAP_VAR(ObPhysicalPlan, phy_plan) {
    ObPhysicalPlanCtx phy_plan_ctx(ctx.get_allocator());
    ObExecContext *exec_ctx = NULL;//because ObExecContext is bigger than 10K, can't use it as local variable
    ObExprCtx expr_ctx;
    RowDesc row_desc;
    ObDatum value_datum;
    ObRawExpr *value_expr = NULL;
    void *tmp_ptr = NULL;
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(my_session) || OB_ISNULL(plan_ctx) || OB_ISNULL(ctx.get_sql_ctx())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObArenaAllocator allocator(common::ObModIds::OB_SQL_EXPR_CALC,
                                 OB_MALLOC_NORMAL_BLOCK_SIZE);
      ObSqlExpression sql_expr(allocator, 0);
      const int64_t cur_time = plan_ctx->has_cur_time() ?
          plan_ctx->get_cur_time().get_timestamp() : ObTimeUtility::current_time();
      phy_plan_ctx.set_cur_time(cur_time, *my_session);

      if (OB_UNLIKELY(NULL == (tmp_ptr = allocator.alloc(sizeof(ObExecContext))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to alloc ObExecContext", K(ret));
      } else {
        exec_ctx = new (tmp_ptr) ObExecContext(allocator);
        phy_plan_ctx.set_phy_plan(&phy_plan);
        expr_ctx.phy_plan_ctx_ = &phy_plan_ctx;
        expr_ctx.my_session_ = my_session;
        expr_ctx.exec_ctx_ = exec_ctx;
        expr_ctx.calc_buf_ = &allocator;
        ObTempExpr *temp_expr = NULL;
        if (OB_ISNULL(value_expr = stmt.get_value_expr())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ObStaticEngineExprCG::gen_expr_with_row_desc(value_expr,
             row_desc, ctx.get_allocator(), ctx.get_my_session(),
             ctx.get_sql_ctx()->schema_guard_, temp_expr))) {
        } else if (OB_ISNULL(temp_expr)) {
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(temp_expr->eval_datum(ctx, allocator, value_datum))) {
        } else {
          const ObExpr &result_expr = temp_expr->rt_exprs_.at(temp_expr->expr_idx_);
          DatumValue value(value_datum, result_expr.obj_meta_);
          if (stmt.is_alter_system_kill()) {
            ObString str = value.get_string();
            const char* index = str.find(',');
            if (index != NULL) {
              str = str.split_on(index);
            }
            value.datum_.set_string(str);
          }
          EXPR_DEFINE_CAST_CTX(expr_ctx, CM_WARN_ON_FAIL);
          DatumValue res_value;
          if (OB_FAIL(DatumValueCaster::to_type(ObIntType, cast_ctx, value,
                                                 allocator, res_value))) {
          }
          ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD == ret ? OB_SUCCESS : ret;
          if (OB_FAIL(ret)) {
          } else {
            // Keep NULL target identifiers mapped to zero.
            sess_id_ = res_value.is_null() ? 0 : static_cast<uint32_t>(res_value.get_int());
          }
        }
      }
    }
  }
  return ret;
}

int ObKillSessionArg::check_auth_for_kill(uint64_t kill_tid, uint64_t kill_uid) const {
  int ret = OB_SUCCESS;
  UNUSED(kill_tid);
  if (OB_INVALID_ID == user_id_ || OB_INVALID_ID == kill_uid
      || (user_id_ != kill_uid && !has_user_super_privilege_)) {
    ret = OB_ERR_KILL_DENIED;
  }
  return ret;
}

}
}
