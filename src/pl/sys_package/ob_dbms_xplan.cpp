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
#include "ob_dbms_xplan.h"
#include "observer/ob_inner_sql_connection_pool.h"
#include "sql/ob_spi.h"
#include "sql/resolver/ddl/ob_explain_stmt.h" // ObExplainDisplayOpt

namespace oceanbase
{
using namespace sql;
using namespace common;
using namespace share;
using namespace observer;
using namespace sqlclient;
namespace pl {
/**
 * @brief ObDbmsXplan::enable_opt_trace
 * @param ctx
 * @param params
 *      sql_id      IN VARCHAR2,
 *      identifier  IN VARCHAR2 DEFAULT ''
 * @param result
 * @return
 */
int ObDbmsXplan::enable_opt_trace(ObExecContext &ctx, ParamStore &params, ObObj &result)
{
  int ret = OB_SUCCESS;
  UNUSED(result);
  ObString sql_id;
  ObString identifier;
  number::ObNumber level_num;
  int64_t level;
  int idx = 0;
  ObSQLSessionInfo *session = ctx.get_my_session();
  if (3 != params.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expect four params", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null session", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(sql_id))) {
    LOG_WARN("failed to get sql string", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(identifier))) {
    LOG_WARN("failed to get identified", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_number(level_num))) {
    LOG_WARN("failed to get number value", K(ret));
  } else if (OB_FAIL(level_num.cast_to_int64(level))) {
    LOG_WARN("failed to cast int", K(ret));
  } else if (OB_FALSE_IT(session->get_optimizer_tracer().set_session_info(session))) {
  } else if (OB_FAIL(session->get_optimizer_tracer().enable_trace(identifier, 
                                                                  sql_id,
                                                                  level))) {
    LOG_WARN("failed to enable optimizer tracer", K(ret));
  }
  return ret;
}

/**
 * @brief ObDbmsXplan::disable_opt_trace
 * @param ctx
 * @param params
 * @param result
 * @return
 */
int ObDbmsXplan::disable_opt_trace(ObExecContext &ctx, ParamStore &params, ObObj &result)
{
  int ret = OB_SUCCESS;
  UNUSED(result);
  ObSQLSessionInfo *session = ctx.get_my_session();
  if (0 != params.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expect four params", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null session", K(ret));
  } else {
    session->get_optimizer_tracer().set_enable(false);
  }
  return ret;
}

/**
 * @brief ObDbmsXplan::set_opt_trace_parameter
 * @param ctx
 * @param params
 *      sql_id      IN VARCHAR2,
 *      identifier  IN VARCHAR2 DEFAULT ''
 * @param result
 * @return
 */
int ObDbmsXplan::set_opt_trace_parameter(ObExecContext &ctx, ParamStore &params, ObObj &result)
{
  int ret = OB_SUCCESS;
  UNUSED(result);
  ObString sql_id;
  ObString identifier;
  number::ObNumber level_num;
  int64_t level;
  ObSQLSessionInfo *session = ctx.get_my_session();
  int idx = 0;
  if (3 != params.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expect four params", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null session", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(sql_id))) {
    LOG_WARN("failed to get sql string", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(identifier))) {
    LOG_WARN("failed to get identified", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_number(level_num))) {
    LOG_WARN("failed to get number value", K(ret));
  } else if (OB_FAIL(level_num.cast_to_int64(level))) {
    LOG_WARN("failed to cast int", K(ret));
  } else if (OB_FAIL(session->get_optimizer_tracer().set_parameters(identifier, 
                                                                    sql_id,
                                                                    level))) {
    LOG_WARN("failed to init optimizer tracer", K(ret));
  }
  return ret;
}

int ObDbmsXplan::display(sql::ObExecContext &ctx,
                        sql::ParamStore &params,
                        common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString table_name;
  ObString statement_id;
  ObString format;
  ObString filter_preds;
  ObSQLSessionInfo *session = ctx.get_my_session();
  int idx = 0;
  if (4 != params.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expect four params", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null session", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(format))) {
    LOG_WARN("failed to get format", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(statement_id))) {
    LOG_WARN("failed to get statement id", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(table_name))) {
    LOG_WARN("failed to get table name", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(filter_preds))) {
    LOG_WARN("failed to get filter preds", K(ret));
  } else {
    PlanText plan_text;
    ExplainType type;
    ObExplainDisplayOpt option;
    int last_id = -1;
    bool alloc_buffer = true;
    ObSqlPlan sql_plan(ctx.get_allocator());
    sql_plan.set_session_info(session);
    ObSEArray<ObSqlPlanItem*, 4> plan_infos;
    ObSEArray<ObSqlPlanItem*, 4> cur_plan_infos;
    if (OB_FAIL(get_plan_info_by_plan_table(ctx, 
                                            table_name,
                                            statement_id,
                                            filter_preds,
                                            plan_infos))) {
      LOG_WARN("failed to get plan info", K(ret));
    } else if (OB_FAIL(get_plan_format(format, type, option))) {
      LOG_WARN("failed to get plan format type", K(ret));
    } else if (OB_FALSE_IT(option.with_real_info_ = false)) {
    }
    for (int i = 0; OB_SUCC(ret) && i < plan_infos.count(); ++i) {
      ObSqlPlanItem *item = plan_infos.at(i);
      if (OB_ISNULL(item)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null plan item", K(ret));
      } else if (item->id_ <= last_id) {
        //new plan
        if (OB_FAIL(sql_plan.format_sql_plan(cur_plan_infos, 
                                            type, 
                                            option, 
                                            plan_text,
                                            alloc_buffer))) {
          LOG_WARN("failed to format sql plan", K(ret));
        } else {
          cur_plan_infos.reuse();
          alloc_buffer = false;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(cur_plan_infos.push_back(item))) {
        LOG_WARN("failed to push back plan item", K(ret));
      } else {
        last_id = item->id_;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(sql_plan.format_sql_plan(cur_plan_infos, 
                                                type, 
                                                option, 
                                                plan_text,
                                                alloc_buffer))) {
      LOG_WARN("failed to format sql plan", K(ret));
    } else if (OB_FAIL(set_display_result(ctx, plan_text, result))) {
      LOG_WARN("failed to convert plan text to string", K(ret));
    }
  }
  return ret;
}

int ObDbmsXplan::display_cursor(sql::ObExecContext &ctx,
                                sql::ParamStore &params,
                                common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString sql_id;
  ObString svr_ip;
  ObString format;
  int64_t plan_id;
  
  int64_t svr_port = 0;
  number::ObNumber num_val;
  ObString plan_name;
  ObString sql_handle;
  uint64_t plan_hash = 0;
  ObSQLSessionInfo *session = ctx.get_my_session();
  int idx = 0;

  if (!(7 == params.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("params num not match", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null session", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_number(num_val))) {
    LOG_WARN("failed to get number value", K(ret));
  } else if (OB_FAIL(num_val.cast_to_int64(plan_id))) {
    LOG_WARN("failed to cast int", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(format))) {
    LOG_WARN("failed to get format", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(svr_ip))) {
    LOG_WARN("failed to get sql id", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_number(num_val))) {
    LOG_WARN("failed to get number value", K(ret));
  } else if (OB_FAIL(num_val.cast_to_int64(svr_port))) {
    LOG_WARN("failed to cast int", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(sql_handle))) {
    LOG_WARN("failed to get sql string", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(plan_name))) {
    LOG_WARN("failed to get plan name", K(ret));
  } else if (!plan_name.empty() && OB_FAIL(num_val.from(plan_name.ptr(), 
                                                        plan_name.length(), 
                                                        ctx.get_allocator()))) {
      ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
      LOG_WARN("failed to get plan hash");
      ObString msg = "plan_name";
      LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, msg.length(), msg.ptr());
  } else if (!plan_name.empty() && !num_val.is_valid_uint64(plan_hash)) {
    ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
    LOG_WARN("failed to get uint64 value", K(ret));
    ObString msg = "plan_name";
    LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, msg.length(), msg.ptr());
  } else if (plan_name.empty() ^ sql_handle.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan hash or sql id");
    ObString msg = "miss plan_name or sql_handle";
    LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg.ptr());
  } else {
    if (0 == plan_id && plan_name.empty() && sql_handle.empty()) {
      plan_id = session->get_last_plan_id();
    }
    PlanText plan_text;
    ExplainType type;
    ObExplainDisplayOpt option;
    ObSqlPlan sql_plan(ctx.get_allocator());
    sql_plan.set_session_info(session);
    ObSEArray<ObSqlPlanItem*, 4> plan_infos;
    if (0 == svr_ip.length() &&
        OB_FAIL(get_server_ip_port(ctx, svr_ip, svr_port))) {
      LOG_WARN("failed to get svr ip and port", K(ret));
    } else if (OB_FAIL(get_plan_info_by_id(ctx, 
                                           svr_ip, 
                                           svr_port, 
                                           plan_id,
                                           sql_handle,
                                           plan_hash, 
                                           plan_infos))) {
      LOG_WARN("failed to get plan info", K(ret));
    } else if (OB_FAIL(get_plan_format(format, type, option))) {
      LOG_WARN("failed to get plan format type", K(ret));
    } else if (OB_FAIL(sql_plan.format_sql_plan(plan_infos, 
                                                type, 
                                                option, 
                                                plan_text))) {
      LOG_WARN("failed to format sql plan", K(ret));
    } else if (OB_FAIL(set_display_result(ctx, plan_text, result))) {
      LOG_WARN("failed to convert plan text to string", K(ret));
    }
  }
  return ret;
}

int ObDbmsXplan::display_active_session_plan(sql::ObExecContext &ctx,
                                            sql::ParamStore &params,
                                            common::ObObj &result)
{
  int ret = OB_SUCCESS;
  number::ObNumber num_val;
  int64_t session_id = 0;
  ObString format;
  ObString svr_ip;
  int64_t svr_port;
  ObSQLSessionInfo *session = ctx.get_my_session();
  int idx = 0;
  if (4 != params.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expect four params", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_number(num_val))) {
    LOG_WARN("failed to get number value", K(ret));
  } else if (OB_FAIL(num_val.cast_to_int64(session_id))) {
    LOG_WARN("failed to cast int", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(format))) {
    LOG_WARN("failed to get format", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null session", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_varchar(svr_ip))) {
    LOG_WARN("failed to get sql id", K(ret));
  } else if (OB_FAIL(params.at(idx++).get_number(num_val))) {
    LOG_WARN("failed to get number value", K(ret));
  } else if (OB_FAIL(num_val.cast_to_int64(svr_port))) {
    LOG_WARN("failed to cast int", K(ret));
  } else {
    PlanText plan_text;
    ExplainType type;
    ObExplainDisplayOpt option;
    ObSqlPlan sql_plan(ctx.get_allocator());
    sql_plan.set_session_info(session);
    ObSEArray<ObSqlPlanItem*, 4> plan_infos;
    if (0 == svr_ip.length() && 
        OB_FAIL(get_server_ip_port(ctx, svr_ip, svr_port))) {
      LOG_WARN("failed to get svr ip and port", K(ret));
    } else if (OB_FAIL(get_plan_info_by_session_id(ctx, 
                                                  session_id, 
                                                  svr_ip, 
                                                  svr_port,
                                                  plan_infos))) {
      LOG_WARN("failed to get plan info", K(ret));
    } else if (OB_FAIL(get_plan_format(format, type, option))) {
      LOG_WARN("failed to get plan format type", K(ret));
    } else if (OB_FAIL(sql_plan.format_sql_plan(plan_infos, 
                                                type, 
                                                option, 
                                                plan_text))) {
      LOG_WARN("failed to format sql plan", K(ret));
    } else if (OB_FAIL(set_display_result(ctx, plan_text, result))) {
      LOG_WARN("failed to convert plan text to string", K(ret));
    }
  }
  return ret;
}

int ObDbmsXplan::get_server_ip_port(sql::ObExecContext &ctx,
                                    ObString &svr_ip,
                                    int64_t &svr_port)
{
  int ret = OB_SUCCESS;
  const ObAddr &addr = GCTX.self_addr();
  svr_port = addr.get_port();
  char ip_buf[OB_IP_STR_BUFF] = {'\0'};
  if (!addr.ip_to_string(ip_buf, sizeof(ip_buf))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ip to string failed", K(ret));
  } else {
    ObString ipstr_tmp = ObString::make_string(ip_buf);
    if (OB_FAIL(ob_write_string (ctx.get_allocator(), ipstr_tmp, svr_ip))) {
      LOG_WARN("ob write string failed", K(ret));
    } else if (svr_ip.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("host ip is empty", K(ret));
    }
  }
  return ret;
}

int ObDbmsXplan::get_plan_format(const ObString &format,
                                ExplainType &type, 
                                ObExplainDisplayOpt& option)
{
  int ret = OB_SUCCESS;
  option.with_color_ = false;
  option.with_tree_line_ = false;
  option.with_real_info_ = false;
  if (format.case_compare("BASIC") == 0) {
    type = EXPLAIN_BASIC;
    option.with_tree_line_ = true;
  } else if (format.case_compare("TYPICAL") == 0) {
    type = EXPLAIN_TRADITIONAL;
    option.with_tree_line_ = true;
    option.with_real_info_ = true;
  } else if (format.case_compare("ALL") == 0) {
    type = EXPLAIN_EXTENDED;
    option.with_tree_line_ = true;
    option.with_real_info_ = true;
  } else if (format.case_compare("ADVANCED") == 0) {
    type = EXPLAIN_EXTENDED;
    option.with_tree_line_ = true;
    option.with_real_info_ = true;
  }
  return ret;
}

int ObDbmsXplan::set_display_result(sql::ObExecContext &ctx,
                                    PlanText &plan_text,
                                    common::ObObj &result)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(set_display_result_for_mysql(ctx, plan_text, result))) {
    LOG_WARN("failed to set display result", K(ret));
  }
  return ret;
}

int ObDbmsXplan::set_display_result_for_mysql(sql::ObExecContext &ctx,
                                              PlanText &plan_text,
                                              common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString ret_str;
  ObTextStringResult text_res(ObTextType, true, &ctx.get_allocator());
  if (OB_FAIL(text_res.init(plan_text.pos_))) {
    LOG_WARN("failed to init text res", K(ret), K(text_res), K(plan_text.pos_));
  } else if (OB_FAIL(text_res.append(plan_text.buf_, plan_text.pos_))) {
    LOG_WARN("failed to append ret_str", K(ret), K(text_res));
  } else {
    text_res.get_result_buffer(ret_str);
    result.set_lob_value(ObTextType, ret_str.ptr(), ret_str.length());
    result.set_has_lob_header();
  }
  return ret;
}

int ObDbmsXplan::get_plan_info_by_plan_table(sql::ObExecContext &ctx,
                                             ObString table_name,
                                             ObString statement_id,
                                             ObString filter_preds,
                                             ObIArray<ObSqlPlanItem*> &plan_infos)
{
  int ret = OB_SUCCESS;
  UNUSED(statement_id);
  UNUSED(filter_preds);
  ObSqlString sql;
  ObSqlString filter;
  ObSqlString true_filter;
  if (OB_FAIL(true_filter.assign_fmt("1 = 1"))) {
    LOG_WARN("failed to assign string", K(ret));
  } else if (0 == filter_preds.length()) {
    if (OB_FAIL(filter.assign_fmt("PLAN_ID = (SELECT MAX(PLAN_ID) FROM %.*s)",
                                    table_name.length(),
                                    table_name.ptr()
                                    ))) {
      LOG_WARN("failed to assign string", K(ret));
    }
  } else if (0 == statement_id.length()) {
    if (OB_FAIL(filter.assign_fmt("%.*s", 
                                  0 == filter_preds.length() ?
                                  (int)true_filter.length() :
                                  filter_preds.length(),
                                  0 == filter_preds.length() ?
                                  true_filter.ptr() :
                                  filter_preds.ptr()
                                  ))) {
      LOG_WARN("failed to assign string", K(ret));
    }
  } else {
    if (OB_FAIL(filter.assign_fmt("STATEMENT_ID='%.*s' AND %.*s", 
                                  statement_id.length(), 
                                  statement_id.ptr(), 
                                  0 == filter_preds.length() ?
                                  (int)true_filter.length() :
                                  filter_preds.length(),
                                  0 == filter_preds.length() ?
                                  true_filter.ptr() :
                                  filter_preds.ptr()))) {
      LOG_WARN("failed to assign string", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(sql.assign_fmt("SELECT \
                      OPERATOR,\
                      OPTIONS,\
                      OBJECT_NODE,\
                      OBJECT_INSTANCE OBJECT_ID,\
                      OBJECT_OWNER,\
                      OBJECT_NAME,\
                      OBJECT_ALIAS,\
                      OBJECT_TYPE,\
                      OPTIMIZER,\
                      ID,\
                      PARENT_ID,\
                      DEPTH,\
                      POSITION,\
                      SEARCH_COLUMNS,\
                      IS_LAST_CHILD,\
                      COST,\
                      0 REAL_COST,\
                      CARDINALITY,\
                      0 REAL_CARDINALITY,\
                      BYTES,\
                      ROWSET,\
                      OTHER_TAG,\
                      PARTITION_START,\
                      PARTITION_STOP,\
                      PARTITION_ID,\
                      OTHER,\
                      DISTRIBUTION,\
                      CPU_COST,\
                      IO_COST,\
                      TEMP_SPACE,\
                      ACCESS_PREDICATES,\
                      FILTER_PREDICATES,\
                      STARTUP_PREDICATES,\
                      PROJECTION,\
                      SPECIAL_PREDICATES,\
                      TIME,\
                      QBLOCK_NAME,\
                      REMARKS,\
                      OTHER_XML\
                    FROM %.*s\
                    WHERE %.*s\
                    ORDER BY PLAN_ID,ID",
                    table_name.length(),
                    table_name.ptr(),
                    (int)filter.length(),
                    filter.ptr()
                    ))) {
      LOG_WARN("failed to assign string", K(ret));
    } else if (OB_FAIL(inner_get_plan_info_use_current_session(ctx, sql, plan_infos))) {
      LOG_WARN("failed to get plan info", K(ret));
    }
  }
  return ret;
}

int ObDbmsXplan::get_plan_info_by_id(sql::ObExecContext &ctx,
                                      const ObString &svr_ip,
                                      int64_t svr_port,
                                      uint64_t plan_id,
                                      const ObString &sql_handle, 
                                      uint64_t plan_hash,
                                      ObIArray<ObSqlPlanItem*> &plan_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_FAIL(sql.assign_fmt("SELECT \
                    OPERATOR,\
                    OPTIONS,\
                    OBJECT_NODE,\
                    OBJECT_ID,\
                    OBJECT_OWNER,\
                    OBJECT_NAME,\
                    OBJECT_ALIAS,\
                    OBJECT_TYPE,\
                    OPTIMIZER,\
                    ID,\
                    PARENT_ID,\
                    DEPTH,\
                    POSITION,\
                    SEARCH_COLUMNS,\
                    IS_LAST_CHILD,\
                    COST,\
                    REAL_COST,\
                    CARDINALITY,\
                    REAL_CARDINALITY,\
                    BYTES,\
                    ROWSET,\
                    OTHER_TAG,\
                    PARTITION_START,\
                    PARTITION_STOP,\
                    PARTITION_ID,\
                    OTHER,\
                    DISTRIBUTION,\
                    CPU_COST,\
                    IO_COST,\
                    TEMP_SPACE,\
                    ACCESS_PREDICATES,\
                    FILTER_PREDICATES,\
                    STARTUP_PREDICATES,\
                    PROJECTION,\
                    SPECIAL_PREDICATES,\
                    TIME,\
                    QBLOCK_NAME,\
                    REMARKS,\
                    OTHER_XML\
                  FROM OCEANBASE.__ALL_VIRTUAL_SQL_PLAN\
                  WHERE 1=1 "))) {
    LOG_WARN("failed to assign string", K(ret));
  } else if (plan_id != 0 && OB_FAIL(sql.append_fmt("AND PLAN_ID=%lu ", plan_id))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (plan_hash != 0 && OB_FAIL(sql.append_fmt("AND PLAN_HASH=%lu ", plan_hash))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (!sql_handle.empty() &&
             OB_FAIL(sql.append_fmt("AND SQL_ID='%.*s' ", 
                                    sql_handle.length(), 
                                    sql_handle.ptr()))) {
    LOG_WARN("failed to assign string", K(ret));
  } else if (OB_FAIL(sql.append_fmt("ORDER BY ID "))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (OB_FAIL(inner_get_plan_info(ctx, sql, plan_infos))) {
    LOG_WARN("failed to get plan info", K(ret));
  }
  return ret;
}

int ObDbmsXplan::get_plan_info_by_session_id(sql::ObExecContext &ctx,
                                            int64_t session_id,
                                            const ObString &svr_ip,
                                            int64_t svr_port,
                                            ObIArray<ObSqlPlanItem*> &plan_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_FAIL(sql.assign_fmt("SELECT \
                      OPERATOR,\
                      OPTIONS,\
                      OBJECT_NODE,\
                      OBJECT_ID,\
                      OBJECT_OWNER,\
                      OBJECT_NAME,\
                      OBJECT_ALIAS,\
                      OBJECT_TYPE,\
                      OPTIMIZER,\
                      A.ID,\
                      PARENT_ID,\
                      DEPTH,\
                      POSITION,\
                      SEARCH_COLUMNS,\
                      IS_LAST_CHILD,\
                      COST,\
                      CAST(NULL AS NUMBER(20,0)) REAL_COST,\
                      CARDINALITY,\
                      CAST(NULL AS NUMBER(20,0)) REAL_CARDINALITY,\
                      BYTES,\
                      ROWSET,\
                      OTHER_TAG,\
                      PARTITION_START,\
                      PARTITION_STOP,\
                      PARTITION_ID,\
                      OTHER,\
                      DISTRIBUTION,\
                      CAST(NULL AS NUMBER(20,0)) CPU_COST,\
                      CAST(NULL AS NUMBER(20,0)) IO_COST,\
                      TEMP_SPACE,\
                      ACCESS_PREDICATES,\
                      FILTER_PREDICATES,\
                      STARTUP_PREDICATES,\
                      PROJECTION,\
                      SPECIAL_PREDICATES,\
                      TIME,\
                      QBLOCK_NAME,\
                      REMARKS,\
                      OTHER_XML\
                    FROM OCEANBASE.__ALL_VIRTUAL_SQL_PLAN A INNER JOIN\
                      (SELECT PLAN_ID \
                        FROM OCEANBASE.__ALL_VIRTUAL_PROCESSLIST \
                        WHERE ID=%ld \
                        LIMIT 1) E\
                      ON A.PLAN_ID = E.PLAN_ID\
                    WHERE 1 = 1\
                    ORDER BY A.ID",
                    session_id))) {
    LOG_WARN("failed to assign string", K(ret));
  } else if (OB_FAIL(inner_get_plan_info(ctx, sql, plan_infos))) {
    LOG_WARN("failed to get plan info", K(ret));
  }
  return ret;
}

int ObDbmsXplan::inner_get_plan_info(sql::ObExecContext &ctx, 
                                    const ObSqlString& sql, 
                                    ObIArray<ObSqlPlanItem*> &plan_infos)
{
  int ret = OB_SUCCESS;
  common::ObISQLClient *sql_proxy = GCTX.sql_proxy_;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null sql proxy", K(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObSQLSessionInfo *my_session = ctx.get_my_session();
      sqlclient::ObMySQLResult *mysql_result = NULL;
      if (OB_ISNULL(my_session)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("session is null", K(ret), K(my_session));
      } else if (OB_FAIL(sql_proxy->read(res, sql.ptr()))) {
        LOG_WARN("failed to execute recover sql", K(ret), K(sql));
      } else if (OB_ISNULL(mysql_result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("execute sql fail", K(ret), K(sql));
      }
      while (OB_SUCC(ret) && OB_SUCC(mysql_result->next())) {
        void *buf = NULL;
        ObSqlPlanItem *plan_info = NULL;
        if (OB_ISNULL(buf=ctx.get_allocator().alloc(sizeof(ObSqlPlanItem)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory", K(ret));
        } else {
          plan_info = new(buf)ObSqlPlanItem();
          if (OB_FAIL(read_plan_info_from_result(ctx, *mysql_result, *plan_info))) {
            LOG_WARN("failed to read plan info", K(ret));
          } else if (OB_FAIL(plan_infos.push_back(plan_info))) {
            LOG_WARN("failed to push back info", K(ret));
          }
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int ObDbmsXplan::inner_get_plan_info_use_current_session(sql::ObExecContext &ctx, 
                                                        const ObSqlString& sql, 
                                                        ObIArray<ObSqlPlanItem*> &plan_infos)
{
  int ret = OB_SUCCESS;
  ObInnerSQLConnectionPool *pool = NULL;
  ObInnerSQLConnection *conn = NULL;
  sql::ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(ctx.get_sql_proxy()) ||
      OB_ISNULL(session = ctx.get_my_session()) ||
      OB_ISNULL((pool = static_cast<ObInnerSQLConnectionPool *>(ctx.get_sql_proxy()->get_pool())))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null sql proxy", K(ret));
  } else if (OB_FAIL(pool->acquire_spi_conn(session, conn))) {
    LOG_WARN("failed to get sql connection", K(ret));
  } else if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null sql connection", K(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      sqlclient::ObMySQLResult *mysql_result = NULL;
      if (OB_FAIL(conn->execute_read(sql.ptr(), res))) {
        LOG_WARN("failed to execute recover sql", K(ret), K(sql));
      } else if (OB_ISNULL(mysql_result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("execute sql fail", K(ret));
      }
      while (OB_SUCC(ret) && OB_SUCC(mysql_result->next())) {
        void *buf = NULL;
        ObSqlPlanItem *plan_info = NULL;
        if (OB_ISNULL(buf=ctx.get_allocator().alloc(sizeof(ObSqlPlanItem)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory", K(ret));
        } else {
          plan_info = new(buf)ObSqlPlanItem();
          if (OB_FAIL(read_plan_info_from_result(ctx, *mysql_result, *plan_info))) {
            LOG_WARN("failed to read plan info", K(ret));
          } else if (OB_FAIL(plan_infos.push_back(plan_info))) {
            LOG_WARN("failed to push back info", K(ret));
          }
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  if (OB_NOT_NULL(conn)) {
    ctx.get_sql_proxy()->close(conn, ret);
  }
  return ret;
}

int ObDbmsXplan::read_plan_info_from_result(sql::ObExecContext &ctx,
                                            sqlclient::ObMySQLResult& mysql_result, 
                                            ObSqlPlanItem &plan_info)
{
  int ret = OB_SUCCESS;
  int64_t int_value;
  ObString varchar_val;
  number::ObNumber num_val;

  #define GET_NUM_VALUE(IDX, value)                                                     \
  do {                                                                                  \
    if (OB_FAIL(ret)) {                                                                 \
    } else if (OB_FAIL(mysql_result.get_number(IDX, num_val))) {                        \
      if (OB_ERR_NULL_VALUE == ret ||                                                   \
          OB_ERR_MIN_VALUE == ret ||                                                    \
          OB_ERR_MAX_VALUE == ret) {                                                    \
        plan_info.value = 0;                                                            \
        ret = OB_SUCCESS;                                                               \
      } else {                                                                          \
        LOG_WARN("failed to get number value", K(ret));                                 \
      }                                                                                 \
    } else if (OB_FAIL(num_val.cast_to_int64(int_value))) {                             \
      LOG_WARN("failed to cast to int64", K(ret));                                      \
    } else {                                                                            \
      plan_info.value = int_value;                                                      \
    }                                                                                   \
  } while(0);

  #define GET_INT_VALUE(IDX, value)                                                     \
  do {                                                                                  \
    if (OB_FAIL(ret)) {                                                                 \
    } else if (OB_FAIL(mysql_result.get_int(IDX, int_value))) {                         \
      if (OB_ERR_NULL_VALUE == ret ||                                                   \
          OB_ERR_MIN_VALUE == ret ||                                                    \
          OB_ERR_MAX_VALUE == ret) {                                                    \
        plan_info.value = 0;                                                            \
        ret = OB_SUCCESS;                                                               \
      } else {                                                                          \
        ret = OB_SUCCESS;                                                               \
        /*retry number type*/                                                           \
        GET_NUM_VALUE(IDX, value);                                                      \
      }                                                                                 \
    } else {                                                                            \
      plan_info.value = int_value;                                                      \
    }                                                                                   \
  } while(0);

  #define GET_VARCHAR_VALUE(IDX, value)                                                 \
  do {                                                                                  \
    if (OB_FAIL(ret)) {                                                                 \
    } else if (OB_FAIL(mysql_result.get_varchar(IDX, varchar_val))) {                   \
      if (OB_ERR_NULL_VALUE == ret ||                                                   \
          OB_ERR_MIN_VALUE == ret ||                                                    \
          OB_ERR_MAX_VALUE == ret) {                                                    \
        plan_info.value = NULL;                                                         \
        plan_info.value##len_ = 0;                                                      \
        ret = OB_SUCCESS;                                                               \
      } else {                                                                          \
        LOG_WARN("failed to get varchar value", K(ret));                                \
      }                                                                                 \
    } else {                                                                            \
      char *buf = NULL;                                                                 \
      plan_info.value##len_ = varchar_val.length();                                     \
      if (0 == varchar_val.length()) {                                                  \
        plan_info.value = NULL;                                                         \
      } else if (OB_ISNULL(buf=(char*)ctx.get_allocator().alloc(varchar_val.length()))) { \
        ret = OB_ALLOCATE_MEMORY_FAILED;                                                \
        LOG_WARN("failed to allocate memory", K(ret));                                  \
      } else {                                                                          \
        MEMCPY(buf, varchar_val.ptr(), varchar_val.length());                           \
        plan_info.value = buf;                                                          \
      }                                                                                 \
    }                                                                                   \
  } while(0);
  GET_VARCHAR_VALUE(OPERATOR, operation_);
  GET_VARCHAR_VALUE(OPTIONS, options_);
  GET_VARCHAR_VALUE(OBJECT_NODE, object_node_);
  GET_INT_VALUE(OBJECT_ID, object_id_);
  GET_VARCHAR_VALUE(OBJECT_OWNER, object_owner_);
  GET_VARCHAR_VALUE(OBJECT_NAME, object_name_);
  GET_VARCHAR_VALUE(OBJECT_ALIAS, object_alias_);
  GET_VARCHAR_VALUE(OBJECT_TYPE, object_type_);
  GET_VARCHAR_VALUE(OPTIMIZER, optimizer_);
  GET_INT_VALUE(ID, id_);
  GET_INT_VALUE(PARENT_ID, parent_id_);
  GET_INT_VALUE(DEPTH, depth_);
  GET_INT_VALUE(POSITION, position_);
  GET_INT_VALUE(SEARCH_COLUMNS, search_columns_);
  GET_INT_VALUE(IS_LAST_CHILD, is_last_child_);
  GET_INT_VALUE(COST, cost_);
  GET_INT_VALUE(REAL_COST, real_cost_);
  GET_INT_VALUE(CARDINALITY, cardinality_);
  GET_INT_VALUE(REAL_CARDINALITY, real_cardinality_);
  GET_INT_VALUE(BYTES, bytes_);
  GET_INT_VALUE(ROWSET, rowset_);
  GET_VARCHAR_VALUE(OTHER_TAG, other_tag_);
  GET_VARCHAR_VALUE(PARTITION_START, partition_start_);
  GET_VARCHAR_VALUE(PARTITION_STOP, partition_stop_);
  GET_INT_VALUE(PARTITION_ID, partition_id_);
  GET_VARCHAR_VALUE(OTHER, other_);
  GET_VARCHAR_VALUE(DISTRIBUTION, distribution_);
  GET_INT_VALUE(CPU_COST, cpu_cost_);
  GET_INT_VALUE(IO_COST, io_cost_);
  GET_INT_VALUE(TEMP_SPACE, temp_space_);
  GET_VARCHAR_VALUE(ACCESS_PREDICATES, access_predicates_);
  GET_VARCHAR_VALUE(FILTER_PREDICATES, filter_predicates_);
  GET_VARCHAR_VALUE(STARTUP_PREDICATES, startup_predicates_);
  GET_VARCHAR_VALUE(PROJECTION, projection_);
  GET_VARCHAR_VALUE(SPECIAL_PREDICATES, special_predicates_);
  GET_INT_VALUE(TIME, time_);
  GET_VARCHAR_VALUE(QBLOCK_NAME, qblock_name_);
  GET_VARCHAR_VALUE(REMARKS, remarks_);
  GET_VARCHAR_VALUE(OTHER_XML, other_xml_);
  if (OB_SUCC(ret) &&
      OB_SUCCESS != (OB_E(EventTable::EN_LEADER_STORAGE_ESTIMATION) OB_SUCCESS)) {
    plan_info.real_cost_ = 0.0;
    plan_info.cpu_cost_ = 0.0;
    plan_info.io_cost_ = 0.0;
  }
  return ret;
}

}
}
