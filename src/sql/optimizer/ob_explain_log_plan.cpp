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

#define USING_LOG_PREFIX SQL_OPT
#include "sql/optimizer/ob_explain_log_plan.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/optimizer/ob_log_values.h"
#include "sql/code_generator/ob_code_generator.h"

using namespace oceanbase;
using namespace sql;
using namespace oceanbase::common;
using namespace oceanbase::json;
using namespace oceanbase::sql::log_op_def;

/**
 *  This function does two things:
 *
 *  1. generate a logical plan for the 'real' query that's being 'explained'
 *  2. generate the plan text from the logical plan and put the text in the buffer
 *     and remember the logical plan as well.
 */
int ObExplainLogPlan::generate_normal_raw_plan()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_stmt()) || OB_UNLIKELY(!get_stmt()->is_explain_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObLogPlan *child_plan = NULL;
    const ObDMLStmt *child_stmt = NULL;
    ObLogicalOperator *top = NULL;
    ObLogValues *values_op = NULL;
    int64_t batch_size = 0;
    const ObExplainStmt *explain_stmt = static_cast<const ObExplainStmt*>(get_stmt());
    if (OB_ISNULL(child_stmt = explain_stmt->get_explain_query_stmt())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (explain_stmt->is_file_describe()) {
      // DuckDB-style DESCRIBE for file table functions: emit one 6-column row
      // per column of the file table instead of the plan text.
      ObLogValues *desc_values = NULL;
      ObSEArray<ObRawExpr *, 6> desc_exprs;
      const ObCharsetInfo *cs = NULL;
      ObCollationType cs_type = CS_TYPE_UTF8MB4_GENERAL_CI;
      if (OB_NOT_NULL(optimizer_context_.get_session_info())) {
        cs_type = optimizer_context_.get_session_info()->get_local_collation_connection();
        cs = ObCharset::get_charset(cs_type);
      }
      if (OB_FAIL(allocate_values_as_top(top))) {
      } else if (OB_ISNULL(top) || OB_UNLIKELY(LOG_VALUES != top->get_type())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(desc_values = static_cast<ObLogValues*>(top))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        // six output exprs: column_name, column_type, null, key, default, extra
        for (int64_t c = 0; OB_SUCC(ret) && c < 6; ++c) {
          ObConstRawExpr *output = NULL;
          if (OB_FAIL(optimizer_context_.get_expr_factory().create_raw_expr(T_VARCHAR, output))) {
          } else if (OB_ISNULL(output)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            ObObj v;
            v.set_varchar(ObString(""));
            v.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
            output->set_value(v);
            if (OB_FAIL(output->formalize(optimizer_context_.get_session_info()))) {
            } else if (OB_FAIL(desc_values->get_output_exprs().push_back(output))) {
            } else if (OB_FAIL(optimizer_context_.get_all_exprs().append(output))) {
            }
          }
        }
      }
      const char *type_names[] = {"BOOLEAN", "BIGINT", "DOUBLE",
                                  "VARCHAR", "DATE", "DATETIME"};
      auto type_to_name = [&](ObObjType t) -> const char * {
        switch (t) {
          case ObTinyIntType: return type_names[0];
          case ObIntType: return type_names[1];
          case ObDoubleType: return type_names[2];
          case ObDateType: return type_names[4];
          case ObDateTimeType: return type_names[5];
          default: return type_names[3];
        }
      };
      for (int64_t i = 0; OB_SUCC(ret) && i < child_stmt->get_column_size(); ++i) {
        const ColumnItem *col = child_stmt->get_column_item(i);
        if (OB_ISNULL(col) || OB_ISNULL(col->expr_)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ObObjType t = col->expr_->get_result_type().get_type();
          bool not_null = col->expr_->get_result_flag() & NOT_NULL_FLAG;
          char name_buf[OB_MAX_COLUMN_NAME_LENGTH];
          char type_buf[64];
          if (OB_FAIL(databuff_printf(name_buf, sizeof(name_buf), "%.*s",
                                      col->column_name_.length(), col->column_name_.ptr()))) {
          } else if (OB_FAIL(databuff_printf(type_buf, sizeof(type_buf), "%s", type_to_name(t)))) {
          } else {
            const char *cells[6] = {
                name_buf, type_buf, not_null ? "NO" : "YES", "NULL", "NULL", "NULL"};
            ObNewRow row;
            ObObj cells_obj[6];
            for (int64_t c = 0; OB_SUCC(ret) && c < 6; ++c) {
              cells_obj[c].set_varchar(ObString(cells[c]));
              cells_obj[c].set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
            }
            row.cells_ = cells_obj;
            row.count_ = 6;
            if (OB_FAIL(desc_values->add_row(row))) {
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        desc_values->set_branch_id(0);
        desc_values->set_id(0);
        desc_values->set_op_id(0);
        set_max_op_id(1);
        set_plan_root(desc_values);
        desc_values->mark_is_plan_root();
        optimizer_context_.set_packed(false);
        // keep all_exprs: the 6 output const exprs above are the source for
        // physical expr generation in ObCodeGenerator::generate_exprs
        optimizer_context_.set_plan_type(ObPhyPlanType::OB_PHY_PLAN_LOCAL,
                                         ObPhyPlanType::OB_PHY_PLAN_LOCAL,
                                         false);
      }
      return ret;
    } else if (OB_ISNULL(child_plan = optimizer_context_.get_log_plan_factory().
                         create(optimizer_context_, *child_stmt))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create log plan for explain stmt");
    } else if (OB_FAIL(child_plan->generate_plan())) {
    } else if (OB_FAIL(remove_duplicate_constraints())) {
    } else if (OB_FAIL(check_explain_generate_plan_with_outline(child_plan))) {
    } else if (OB_FAIL(ObCodeGenerator::detect_batch_size(*child_plan, batch_size))) {
    } else if (OB_FAIL(allocate_values_as_top(top))) {
    } else if (OB_ISNULL(top) || OB_UNLIKELY(LOG_VALUES != top->get_type())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      set_plan_root(top);
      top->mark_is_plan_root();
      child_plan->get_optimizer_context().set_batch_size(batch_size);
      values_op = static_cast<ObLogValues*>(top);
      values_op->set_explain_plan(child_plan);
      ObSqlPlan sql_plan(get_allocator());
      sql_plan.set_session_info(get_optimizer_context().get_session_info());
      ObExplainLogPlan *explain_plan = static_cast<ObExplainLogPlan*>(child_plan);
      ObSEArray<common::ObString, 64> plan_strs;
      const ObString& into_table = explain_stmt->get_into_table();
      const ObString& statement_id = explain_stmt->get_statement_id();
      if (OB_FAIL(sql_plan.store_sql_plan_for_explain(get_optimizer_context().get_exec_ctx(),
                                                      child_plan,
                                                      explain_stmt->get_explain_type(),
                                                      0 == into_table.length() ? "PLAN_TABLE" : into_table,
                                                      0 == statement_id.length() ? "" : statement_id,
                                                      explain_stmt->get_display_opt(),
                                                      plan_strs))) {
      } else {
        //For explain stmt, we can do pack at the stage of expr alloc,
        //But we need to use the FALSE flag to tell the driver 
        //to use normal encoding instead of memcopy
        optimizer_context_.set_packed(false);
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < plan_strs.count(); ++i) {
        ObObj obj;
        obj.set_varchar(plan_strs.at(i));
        ObNewRow row;
        row.cells_ = &obj;
        row.count_ = 1;
        if (OB_FAIL(values_op->add_row(row))) {
        }
      }
      if (OB_SUCC(ret)) {
        // set values_op operator id && set max operator id for LogPlan
        values_op->set_op_id(0);
        set_max_op_id(1);
      }
    }
    get_optimizer_context().get_all_exprs().reuse();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(allocate_output_expr_for_values_op(*values_op))) {
    } else {
      get_optimizer_context().set_plan_type(ObPhyPlanType::OB_PHY_PLAN_LOCAL,
                                            ObPhyPlanType::OB_PHY_PLAN_LOCAL,
                                            false);
    }
  }
  return ret;
}

int ObExplainLogPlan::check_explain_generate_plan_with_outline(ObLogPlan *real_plan)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObSqlCtx *sql_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  const ObExplainStmt *explain_stmt = static_cast<const ObExplainStmt*>(get_stmt());
  ObQueryCtx *query_ctx = get_optimizer_context().get_query_ctx();
  if (OB_ISNULL(real_plan) || OB_ISNULL(explain_stmt)
      || OB_ISNULL(exec_ctx = get_optimizer_context().get_exec_ctx())
      || OB_ISNULL(session_info = get_optimizer_context().get_session_info())
      || OB_ISNULL(sql_ctx = exec_ctx->get_sql_ctx()) || OB_ISNULL(query_ctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (session_info->is_inner() || sql_ctx->is_prepare_protocol_) {
    /* do not check explain for inner sql (include query in PL) */
  } else if (OB_UNLIKELY(get_optimizer_context().get_query_ctx() != NULL
                         && get_optimizer_context().get_query_ctx()->get_injected_random_status())) { 
    /* do nothing */
  } else if (sql_ctx->multi_stmt_item_.is_part_of_multi_stmt()
             && sql_ctx->multi_stmt_item_.get_seq_num() > 0) {
    /* generate plan call by ObMPQuery::process_with_tmp_context use tmp context, do not check */
  } else if (EXPLAIN_UNINITIALIZED !=  explain_stmt->get_explain_type()
             && EXPLAIN_BASIC !=  explain_stmt->get_explain_type()
             && EXPLAIN_OUTLINE !=  explain_stmt->get_explain_type()) {
    /* generate plan again for explain/explain basic/explain outline,
      do not check explain extended/explain extended_noaddr */
  } else if (0 == sql_ctx->first_plan_hash_) {  /* generate plan first time */
    void *tmp_ptr = NULL;
    sql_ctx->first_outline_data_.reset();
    if (OB_UNLIKELY(0 == real_plan->get_signature()) || 0 < sql_ctx->retry_times_) {
      /* do nothing */
    } else if (OB_SUCC(OB_E(EventTable::EN_EXPLAIN_GENERATE_PLAN_WITH_OUTLINE) OB_SUCCESS)) {
      /* do nothing */
    } else if (OB_UNLIKELY(NULL == (tmp_ptr = get_optimizer_context().get_allocator().alloc(OB_MAX_SQL_LENGTH)))) {
      /* allocator in optimizer context is from ObResultSet */
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("fail to alloc memory", K(ret));
    } else {
      PlanText plan_text;
      plan_text.buf_ = static_cast<char *>(tmp_ptr);
      plan_text.buf_len_ = OB_MAX_SQL_LENGTH;
      if (OB_FAIL(ObSqlPlan::get_plan_outline_info_one_line(plan_text, real_plan))) {
      } else {
        sql_ctx->first_outline_data_.assign_ptr(plan_text.buf_, static_cast<ObString::obstr_size_t>(plan_text.pos_));
        sql_ctx->first_plan_hash_ = real_plan->get_signature();
        sql_ctx->first_equal_param_cons_cnt_ = query_ctx->all_equal_param_constraints_.count();
        sql_ctx->first_const_param_cons_cnt_ = query_ctx->all_plan_const_param_constraints_.count();
        sql_ctx->first_expr_cons_cnt_ = query_ctx->all_expr_constraints_.count();
        ret = OB_SQL_RETRY_OUTLINE;
      }
    }
  } else {  /* check generate plan again use outline data */
    SMART_VAR(char[OB_MAX_SQL_LENGTH], buf) {
      PlanText plan_text;
      plan_text.buf_ = buf;
      plan_text.buf_len_ = OB_MAX_SQL_LENGTH;
      if (OB_FAIL(ObSqlPlan::get_plan_outline_info_one_line(plan_text, real_plan))) {
      } else {
        ObString cur_outline_data;
        const uint64_t cur_plan_hash = real_plan->get_signature();
        cur_outline_data.assign_ptr(plan_text.buf_, static_cast<ObString::obstr_size_t>(plan_text.pos_));
        if (cur_plan_hash != sql_ctx->first_plan_hash_) {
          ret = OB_OUTLINE_NOT_REPRODUCIBLE;
          LOG_WARN("failed to generate plan use outline", K(sql_ctx->first_plan_hash_));
        }
        if (0 != (cur_outline_data.case_compare(sql_ctx->first_outline_data_))) {
          ret = OB_OUTLINE_NOT_REPRODUCIBLE;
          LOG_WARN("failed to generate plan use outline", K(sql_ctx->first_outline_data_));
        }
        if (query_ctx->all_equal_param_constraints_.count() != sql_ctx->first_equal_param_cons_cnt_ || 
            query_ctx->all_plan_const_param_constraints_.count() != sql_ctx->first_const_param_cons_cnt_ || 
            query_ctx->all_expr_constraints_.count() != sql_ctx->first_expr_cons_cnt_) {
          ret = OB_OUTLINE_NOT_REPRODUCIBLE;
          LOG_WARN("failed to generate plan use outline", K(sql_ctx->first_equal_param_cons_cnt_), 
                                                          K(sql_ctx->first_const_param_cons_cnt_), 
                                                          K(sql_ctx->first_expr_cons_cnt_),
                                                          K(query_ctx->all_equal_param_constraints_.count()),
                                                          K(query_ctx->all_plan_const_param_constraints_.count()),
                                                          K(query_ctx->all_expr_constraints_.count()));
        }
        if (OB_FAIL(ret)) {
        }
      }
      sql_ctx->first_plan_hash_ = 0;
      sql_ctx->first_outline_data_.reset();
      sql_ctx->first_equal_param_cons_cnt_ = 0;
      sql_ctx->first_const_param_cons_cnt_ = 0;
      sql_ctx->first_expr_cons_cnt_ = 0;
    }
  }
  return ret;
}
