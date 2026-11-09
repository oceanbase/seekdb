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
#include "config_bridge.h"
#include "common/ob_timeout_ctx.h"
#include "lib/cpu/ob_cpu_topology.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "share/ob_server_struct.h"
#include "lib/task/ob_timer.h"
#include "sql/engine/cmd/ob_table_executor.h"
#include "sql/engine/cmd/ob_ai_column_util.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "query/command/ob_root_command_service.h"
#include "query/command/ob_root_service_serialization.h"
#include "query/runtime/ob_query_runtime_environment.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "sql/engine/expr/ob_expr_regexp_context.h"  // ObExprRegexpSessionVariables (unity regroup)
#include "sql/engine/cmd/ob_index_executor.h"
#include "sql/engine/cmd/ob_ddl_executor_util.h"
#include "sql/resolver/ddl/ob_create_table_stmt.h"
#include "sql/resolver/ddl/ob_alter_table_stmt.h"
#include "sql/resolver/ddl/ob_drop_table_stmt.h"
#include "sql/resolver/ddl/ob_rename_table_stmt.h"
#include "sql/resolver/ddl/ob_truncate_table_stmt.h"
#include "sql/resolver/ddl/ob_create_table_like_stmt.h"
#include "sql/resolver/ddl/ob_fork_table_stmt.h"
#include "sql/resolver/ddl/ob_recyclebin_restore_stmt.h"
#include "sql/resolver/ddl/ob_purge_stmt.h"
#include "sql/resolver/ddl/ob_optimize_stmt.h"
#include "sql/resolver/dml/ob_delete_resolver.h"
#include "sql/engine/cmd/ob_partition_executor_utils.h"

#include "sql/printer/ob_select_stmt_printer.h"
#include "share/ob_structured_event_logger.h"
namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace observer;
namespace sql
{

ObCreateTableExecutor::ObCreateTableExecutor()
{
}

ObCreateTableExecutor::~ObCreateTableExecutor()
{
}

int ObCreateTableExecutor::prepare_stmt(ObCreateTableStmt &stmt,
                                        const ObSQLSessionInfo &my_session,
                                        ObString &create_table_name)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("CreateTableExec");
  const int64_t buf_len = OB_MAX_SQL_LENGTH;
  char *buf = static_cast<char*>(allocator.alloc(buf_len));
  int64_t pos = 0;
  const int64_t session_id = my_session.get_server_sid();
  const int64_t timestamp = ObTimeUtility::current_time();
  obcall::ObCreateTableArg &create_table_arg = stmt.get_create_table_arg();
  create_table_name = create_table_arg.schema_.get_table_name_str();
  if (OB_FAIL(databuff_printf(buf, buf_len, pos, "__ctas_%ld_%ld", session_id, timestamp))) {
  } else {
    ObString tmp_table_name(pos, buf);
    if (OB_FAIL(create_table_arg.schema_.set_table_name(tmp_table_name))) {
    }
  }
  return ret;
}

int ObCreateTableExecutor::ObInsSQLPrinter::inner_print(char *buf, int64_t buf_len, int64_t &res_len)
{
  int ret = OB_SUCCESS;
  const char sep_char = '`';
  const ObSelectStmt *select_stmt = NULL;
  int64_t pos1 = 0;
  uint64_t insert_mode = 0;
  
  if (OB_ISNULL(stmt_) || OB_ISNULL(select_stmt= stmt_->get_sub_select())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const char *insert_str = NULL;
    const int64_t parallel_str_max_len = 256;
    char parallel_str[parallel_str_max_len] = {0};
    int64_t parallel_str_pos = 0;
    const char *osg_str = NULL;
    insert_mode = stmt_->get_insert_mode();
    if (insert_mode != 0 &&
        insert_mode != 1 &&
        insert_mode != 2 ) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      insert_str = insert_mode == 0 ? "insert" : insert_mode == 1 ? "insert ignore" : "replace";
      osg_str = do_osg_ ? "GATHER_OPTIMIZER_STATISTICS" : "NO_GATHER_OPTIMIZER_STATISTICS";
      if (stmt_->get_has_parallel_hint() &&
          OB_FAIL(databuff_printf(parallel_str, parallel_str_max_len, parallel_str_pos,
                                  "PARALLEL(%lu)", stmt_->get_parallelism()))) {
      }
    }
    if (OB_FAIL(ret)) {

    } else if (OB_FAIL(databuff_printf(buf, buf_len, pos1,
                                       "%s /*+ ENABLE_PARALLEL_DML %s %s */ into %c%.*s%c.%c%.*s%c",
                                       insert_str,
                                       parallel_str,
                                       osg_str,
                                       sep_char,
                                       stmt_->get_database_name().length(),
                                       stmt_->get_database_name().ptr(),
                                       sep_char,
                                       sep_char,
                                       stmt_->get_table_name().length(),
                                       stmt_->get_table_name().ptr(),
                                       sep_char
                                       ))) {
    }
  }
  if (OB_FAIL(ret)) {
    //do nothing
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < select_stmt->get_select_item_size(); ++i) {
      const SelectItem &select_item = select_stmt->get_select_item(i);
      if (OB_FAIL(databuff_printf(buf, buf_len, pos1, (0 == i)? "(": ", "))) {
      } else { /* do nothing */ }
      if (OB_SUCC(ret)) {
        if (!select_item.alias_name_.empty()) {
          if (OB_FAIL(databuff_printf(buf, buf_len, pos1, "%c%.*s%c", sep_char, LEN_AND_PTR(select_item.alias_name_), sep_char))) {
          } else { /* do nothing */ }
        } else {
          if (OB_FAIL(databuff_printf(buf, buf_len, pos1, "%c%.*s%c", sep_char, LEN_AND_PTR(select_item.expr_name_), sep_char))) {
          } else { /* do nothing */ }
        }
      } else { /* do nothing */ }
    }
  }

  if (OB_SUCC(ret)) {
    ObSelectStmtPrinter select_stmt_printer(buf, buf_len, &pos1, select_stmt,
                                            schema_guard_,
                                            print_params_,
                                            param_store_,
                                            true);
    select_stmt_printer.set_is_first_stmt_for_hint(true);  // need print global hint
    if (OB_FAIL(databuff_printf(buf, buf_len, pos1, ") "))) {
    } else if (OB_FAIL(select_stmt_printer.do_print())) {
    } else {
      res_len = pos1;
      LOG_INFO("[CTAS] successfully print sql", "sql", buf);
    }
  }
  return ret;
}
// Prepare query insertion script
int ObCreateTableExecutor::prepare_ins_arg(ObCreateTableStmt &stmt,
                                           const ObSQLSessionInfo *my_session,
                                           ObSchemaGetterGuard *schema_guard,
                                           const ParamStore *param_store,
                                           ObSqlString &ins_sql) // out, final query insert statement
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("CreateTableExec");
  char *buf = static_cast<char*>(allocator.alloc(OB_MAX_SQL_LENGTH));
  int64_t buf_len = OB_MAX_SQL_LENGTH;
  int64_t pos1 = 0;
  bool no_osg_hint = false;
  bool online_sys_var = false;
  ObSelectStmt *select_stmt = stmt.get_sub_select();
  ObObjPrintParams obj_print_params(select_stmt->get_query_ctx()->get_timezone_info());
  obj_print_params.print_origin_stmt_ = true;
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("allocate memory failed");
  } else if (OB_ISNULL(select_stmt) || OB_ISNULL(select_stmt->get_query_ctx()) || OB_ISNULL(my_session)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    //get hint
    no_osg_hint = select_stmt->get_query_ctx()->get_global_hint().has_no_gather_opt_stat_hint();

    //get system variable
    ObObj online_sys_var_obj;
    if (OB_FAIL(my_session->get_sys_variable(SYS_VAR__OPTIMIZER_GATHER_STATS_ON_LOAD, online_sys_var_obj))) {
    } else {
      online_sys_var = online_sys_var_obj.get_bool();
    }
  }

  if (OB_SUCC(ret)) {
    ObInsSQLPrinter sql_printer(&stmt, schema_guard, obj_print_params, param_store, !no_osg_hint && online_sys_var);
    ObString sql;
    if (OB_FAIL(sql_printer.do_print(allocator, sql))) {
    } else if (OB_FAIL(ins_sql.append(sql))){
    }
  }

  if (OB_SUCC(ret)) {
    ObString converted_sql = ins_sql.string();
    if (OB_FAIL(ObSQLUtils::convert_sql_text_from_schema_for_resolve(allocator,
                                                                     my_session->get_dtc_params(),
                                                                     converted_sql,
                                                                     ObCharset::COPY_STRING_ON_SAME_CHARSET))) {
    } else if (OB_FAIL(ins_sql.assign(converted_sql))) {
    }
  }
  return ret;
}
// Prepare alter table parameters
int ObCreateTableExecutor::prepare_alter_arg(ObCreateTableStmt &stmt,
                                             const ObSQLSessionInfo *my_session,
                                             const ObString &create_table_name,
                                             obcall::ObAlterTableArg &alter_table_arg) //out, final alter table arg, set session_id = 0;
{
  int ret = OB_SUCCESS;
  const obcall::ObCreateTableArg &create_table_arg = stmt.get_create_table_arg();
  ObTableSchema &table_schema = const_cast<obcall::ObCreateTableArg&>(create_table_arg).schema_;
  AlterTableSchema *alter_table_schema = &alter_table_arg.alter_table_schema_;
  table_schema.set_session_id(my_session->get_sessid_for_table());
  alter_table_arg.session_id_ = my_session->get_sessid_for_table();
  alter_table_schema->alter_type_ = OB_DDL_ALTER_TABLE;
  alter_table_arg.is_inner_ = my_session->is_inner();
  
  if (OB_FAIL(alter_table_arg.tz_info_wrap_.deep_copy(my_session->get_tz_info_wrap()))) {
  } else if (OB_FAIL(alter_table_schema->assign(table_schema))) {
  } else if (!table_schema.is_mysql_tmp_table()
             && FALSE_IT(alter_table_schema->set_session_id(0))) {
    //impossible
  } else if (OB_FAIL(alter_table_schema->set_origin_table_name(stmt.get_table_name()))) {
  } else if (OB_FAIL(alter_table_schema->set_origin_database_name(stmt.get_database_name()))) {
  } else if (OB_FAIL(alter_table_schema->set_table_name(create_table_name))) {
  } else if (OB_FAIL(alter_table_schema->set_database_name(stmt.get_database_name()))) {
  } else if (!table_schema.is_mysql_tmp_table()
             && OB_FAIL(alter_table_schema->alter_option_bitset_.add_member(obcall::ObAlterTableArg::SESSION_ID))) {
  } else if (OB_FAIL(alter_table_schema->alter_option_bitset_.add_member(obcall::ObAlterTableArg::TABLE_NAME))) {
  }
  return ret;
}
// Prepare drop table parameters
int ObCreateTableExecutor::prepare_drop_arg(const ObCreateTableStmt &stmt,
                                            const ObSQLSessionInfo *my_session,
                                            obcall::ObTableItem &table_item,
                                            obcall::ObDropTableArg &drop_table_arg) // out, drop table arguments
{
  int ret = OB_SUCCESS;
  const ObString &db_name = stmt.get_database_name();
  const ObString &tab_name = stmt.get_table_name();
  drop_table_arg.if_exist_ = true;
  
  drop_table_arg.to_recyclebin_ = false;
  drop_table_arg.table_type_ = USER_TABLE;
  drop_table_arg.session_id_ = my_session->get_sessid_for_table();
  
  int64_t foreign_key_checks = 0;
  my_session->get_foreign_key_checks(foreign_key_checks);
  drop_table_arg.foreign_key_checks_ = foreign_key_checks;
  table_item.database_name_ = db_name;
  table_item.table_name_ = tab_name;
  if (OB_FAIL(my_session->get_name_case_mode(table_item.mode_))) {
  } else if (OB_FAIL(drop_table_arg.tables_.push_back(table_item))) {
  }
  return ret;
}
// Query table creation processing, through internal session execution query insert code reference ObTableModify::ObTableModifyCtx::open_inner_conn() implementation
int ObCreateTableExecutor::execute_ctas(ObExecContext &ctx,
                                        ObCreateTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObString cur_query;
  int64_t affected_rows = 0;
  ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
  ObArenaAllocator allocator("CreateTableExec");
  HEAP_VAR(obcall::ObAlterTableArg, alter_table_arg) {
    obcall::ObDropTableArg drop_table_arg;
    obcall::ObTableItem table_item;
    ObString create_table_name;
    ObSqlString ins_sql;
    const ObGlobalContext &gctx = GCTX;
    obcall::ObCreateTableRes create_table_res;
    obcall::ObCreateTableArg &create_table_arg = stmt.get_create_table_arg();
    create_table_arg.is_inner_ = my_session->is_inner();
    bool need_clean = true;
    CK(OB_NOT_NULL(sql_proxy),
      OB_NOT_NULL(my_session),
      OB_NOT_NULL(gctx.schema_service_),
      OB_NOT_NULL(plan_ctx),
      OB_NOT_NULL(ctx.get_sql_ctx()));
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ob_write_string(allocator, my_session->get_current_query_string(), cur_query))) {
      }
    }
    
    if (OB_SUCC(ret)) {
      if (OB_FAIL(prepare_stmt(stmt, *my_session, create_table_name))) {
      } else if (OB_FAIL(prepare_ins_arg(stmt, my_session, ctx.get_sql_ctx()->schema_guard_, &plan_ctx->get_param_store(), ins_sql))) {
      } else if (OB_FAIL(prepare_alter_arg(stmt, my_session, create_table_name, alter_table_arg))) {
      } else if (OB_FAIL(prepare_drop_arg(stmt, my_session, table_item, drop_table_arg))) {
      } else if (OB_FAIL(ctx.get_sql_ctx()->schema_guard_->reset())){
      } else {// 2. create table
        DEBUG_SYNC(BEFORE_SEND_PARALLEL_CREATE_TABLE);
        create_table_arg.is_parallel_ = false;
        if (OB_FAIL(ObDDLExecutorUtil::execute_pcreate_table(
            my_session, ctx.root_command_service(), "[parallel create table]",
                                                            create_table_arg, create_table_res))) {
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (!(OB_INVALID_ID == create_table_res.table_id_
                  || (OB_INVALID_ID != create_table_res.table_id_
                      && true == create_table_res.do_nothing_)) ) { // If the table already exists, subsequent query inserts will not be performed
        // 1. for old rs
        // when table_exist, table_id == invalid_id, and do_nothing will alway be false
        // 2. for new rs
        // do_nothing is true when table_exist
        // --> when table_id == invalid, both old and new rs no table create
        // --> when table_id != invalid, both old and new rs do_nothing_ correct
        if (OB_INVALID_VERSION == create_table_res.schema_version_) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          
          if (OB_FAIL(gctx.schema_service_->async_refresh_schema(create_table_res.schema_version_))) {
          }
        }
        #ifdef ERRSIM
        {
          int tmp_ret = OB_E(EventTable::EN_CTAS_FAIL_NO_DROP_ERROR) OB_SUCCESS; // error injection, make the table unable to be cleaned
          if (OB_FAIL(tmp_ret)) {
            ret = tmp_ret;
            need_clean = false;
          }
        }
        #else
          //do nothing...
        #endif
        //3, insert data
        if (OB_SUCC(ret)) {
          bool is_mysql_temp_table = stmt.get_create_table_arg().schema_.is_mysql_tmp_table();
          bool in_trans = my_session->is_in_transaction();
          bool need_set_autocommit = (!is_mysql_temp_table || !in_trans);
          bool original_autocommit = false;
          ObBasicSessionInfo::UserScopeGuard user_scope_guard(my_session->get_sql_scope_flags());
          common::sqlclient::ObISQLConnection *conn = NULL;
          common::sqlclient::ObISQLConnectionGuard conn_guard;

          if (OB_FAIL(my_session->get_autocommit(original_autocommit))) {
          } else if (need_set_autocommit &&
                     !original_autocommit && OB_FAIL(my_session->set_autocommit(true))) {
            LOG_WARN("failed to set autocommit", K(ret));
          } else {
            if (OB_FAIL(
                    query::ObInnerSQLConnectionAccess::
                        create_connection_with_external_session(
                            my_session, conn_guard))) {
            } else if (OB_ISNULL(conn = conn_guard.get_ptr())) {
              ret = OB_INNER_STAT_ERROR;
            } else if (OB_FAIL(
                           conn->execute_write(ins_sql.ptr(), affected_rows, true))) {
            }

            if (need_set_autocommit && !original_autocommit) {
              int tmp_ret = OB_SUCCESS;
              if (OB_TMP_FAIL(my_session->set_autocommit(original_autocommit))) {
                ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
                LOG_WARN("failed to set autocommit", K(ret), K(tmp_ret), K(original_autocommit));
              }
            }
            conn = NULL;
            conn_guard.reset();
          }
        }

        DEBUG_SYNC(BEFORE_EXECUTE_CTAS_CLEAR_SESSION_ID);
        //4, refresh schema, reset table's sess id to 0
        if (OB_SUCC(ret)) {
          obcall::ObAlterTableRes res;
          bool finish = false;
          while (OB_SUCC(ret) && !finish) {
            if (OB_FAIL(query::serialize_root_service_call(
                    [&]{ return ctx.root_command_service().alter_table(alter_table_arg, res); }))) {
              if (OB_EAGAIN == ret) {
                ret = OB_SUCCESS; // maybe table lock conflict, retry
                if (OB_UNLIKELY(THIS_WORKER.get_timeout_remain() <= 0)) {
                  ret = OB_TIMEOUT;
                } else if (OB_FAIL(THIS_WORKER.check_status())) {
                }
              }
            } else {
              finish = true;
            }
          }
        }

        if (OB_FAIL(ret)) { //5, query table creation failed, need to clean up environment i.e. DROP TABLE
          my_session->update_last_active_time();
          if (OB_LIKELY(need_clean)) {
            int tmp_ret = OB_SUCCESS;
            obcall::ObDDLRes res;
            if (OB_SUCCESS != (tmp_ret = query::serialize_root_service_call([&]{
                  return ctx.root_command_service().drop_table(drop_table_arg, res);
                }))) {
            } else {
              LOG_INFO("table is created and dropped due to error ", K(ret));
            }
          }
        } else {
          plan_ctx->set_affected_rows(affected_rows);
        }

        if (OB_ERR_TABLE_EXIST == ret && create_table_arg.if_not_exist_) {
          ret = OB_SUCCESS;
        }
      } else {
      }
      char table_info_buffer[256];
      snprintf(table_info_buffer, sizeof(table_info_buffer), "table_id:%ld, hidden_table_id:%ld",
                alter_table_arg.table_id_, alter_table_arg.hidden_table_id_);
      SERVER_EVENT_ADD("ddl", "create table as select execute finish",
        "ret", ret,
        "trace_id", *ObCurTraceId::get_trace_id(),
        "rpc_dst", GCTX.self_addr(),
        "table_info", table_info_buffer,
        "schema_version", create_table_res.schema_version_);
      SQL_ENG_LOG(INFO, "finish create table execute.", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
    }
    OZ(my_session->store_query_string(cur_query));
  }
  return ret;
}


namespace {

// Escape a string for single-quoted SQL literal use.
int ai_column_escape(const ObString &in, ObSqlString &out)
{
  int ret = OB_SUCCESS;
  const char *p = in.ptr();
  const int64_t n = in.length();
  for (int64_t i = 0; OB_SUCC(ret) && i < n; ++i) {
    const char c = p[i];
    if ('\'' == c) {
      // double the quote so the literal stays inside one SQL string
      ret = out.append("''", 2);
    } else if ('\\' == c) {
      ret = out.append("\\\\", 2);
    } else if (0 == c) {
      ret = out.append("\\0", 2);
    } else if ('\n' == c) {
      ret = out.append("\\n", 2);
    } else if ('\r' == c) {
      ret = out.append("\\r", 2);
    } else if (OB_FAIL(out.append(&c, 1))) {
  }
  }
  return ret;
}

// Render an already escaped (or literal) string inside quotes, tolerating NULL buffers.
const char *ai_column_str(const ObSqlString &s)
{
  return OB_NOT_NULL(s.ptr()) ? s.ptr() : "";
}

int ai_column_type_prompt(const ObString &result_type, ObSqlString &prompt)
{
  int ret = OB_SUCCESS;
  if (0 == result_type.case_compare("json")) {
    ret = prompt.append("Return exactly one valid JSON value");
  } else if (0 == result_type.case_compare("integer")) {
    ret = prompt.append("Return exactly one base-10 integer");
  }
  return ret;
}

// Build "`pk` = <value>" text expressions for the new/old row of a trigger.
int ai_column_rowkey_predicate(const ObTableSchema &table_schema,
                               const char *row_ref,
                               ObSqlString &out)
{
  int ret = OB_SUCCESS;
  const ObRowkeyInfo &rowkey = table_schema.get_rowkey_info();
  for (int64_t i = 0; OB_SUCC(ret) && i < rowkey.get_size(); ++i) {
    uint64_t column_id = OB_INVALID_ID;
    const ObColumnSchemaV2 *col = NULL;
    if (OB_FAIL(rowkey.get_column_id(i, column_id))) {
    } else if (OB_ISNULL(col = table_schema.get_column_schema(column_id))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (col->is_hidden()) {
      // the hidden auto-increment row key of a key-less table cannot be referenced
      continue;
    } else {
      const ObString name = col->get_column_name_str();
      if (out.length() > 0 && OB_FAIL(out.append(", ' AND ', "))) {
      } else if (OB_FAIL(out.append_fmt("'`%.*s` = '", name.length(), name.ptr()))) {
      } else if (OB_FAIL(out.append_fmt(", %s.`%.*s`", row_ref, name.length(), name.ptr()))) {
  }
    }
  }
  return ret;
}

int ai_column_exec(sqlclient::ObISQLConnection *conn, const ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(conn->execute_write(ObString(sql.length(), sql.ptr()),
                                         affected_rows, false))) {
  }
  return ret;
}

// Build the catalogue row plus the row level triggers that keep the pending task
// table in step with the business data.
// Start the async AI column executor once the first AI column is registered.
int ai_column_worker_start();

int apply_ai_columns(ObExecContext &ctx,
                     const ObTableSchema &table_schema,
                     const uint64_t table_id,
                     const ObString &db_name,
                     const ObIArray<ObAiColumnDef> &defs)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
  if (defs.empty()) {
    // nothing to do
  } else if (OB_ISNULL(sql_proxy) || OB_INVALID_ID == table_id
             || table_schema.get_table_name_str().empty() || db_name.empty()) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "invalid ai column ddl context", K(ret), K(table_id), K(db_name));
  }
  common::sqlclient::ObISQLConnection *conn = NULL;
  common::sqlclient::ObISQLConnectionGuard conn_guard;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  if (OB_SUCC(ret) && !defs.empty()) {
    if (OB_ISNULL(my_session)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(query::ObInnerSQLConnectionAccess::create_connection_with_external_session(
                   my_session, conn_guard))) {
      SQL_ENG_LOG(WARN, "create inner session connection for ai column failed", K(ret));
    } else if (OB_ISNULL(conn = conn_guard.get_ptr())) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  ObSqlString tbl;
  if (OB_SUCC(ret) && OB_FAIL(tbl.append_fmt("`%.*s`.`%.*s`", db_name.length(), db_name.ptr(),
                                             table_schema.get_table_name_str().length(),
                                             table_schema.get_table_name_str().ptr()))) {
    SQL_ENG_LOG(WARN, "build ai column table name failed", K(ret), K(db_name));
  }
  if (OB_SUCC(ret) && !defs.empty()) {
    for (int64_t d = 0; OB_SUCC(ret) && d < defs.count(); ++d) {
      const ObAiColumnDef &def = defs.at(d);
      uint64_t col_id = OB_INVALID_ID;
      uint64_t state_id = OB_INVALID_ID;
      uint64_t request_id = OB_INVALID_ID;
      uint64_t spec_id = OB_INVALID_ID;
      ObSqlString hidden_state;
      ObSqlString hidden_request;
      ObSqlString hidden_spec;
      if (OB_FAIL(hidden_state.append_fmt("__ai_%.*s_state", def.column_name_.length(),
                                          def.column_name_.ptr()))
          || OB_FAIL(hidden_request.append_fmt("__ai_%.*s_request", def.column_name_.length(),
                                               def.column_name_.ptr()))
          || OB_FAIL(hidden_spec.append_fmt("__ai_%.*s_result_spec", def.column_name_.length(),
                                            def.column_name_.ptr()))) {
      }
      // column ids are already assigned by the resolver, look them up in the table schema
      for (int64_t i = 0; OB_SUCC(ret) && i < table_schema.get_column_count(); ++i) {
        const ObColumnSchemaV2 *col = table_schema.get_column_schema_by_idx(i);
        if (OB_ISNULL(col)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObString name = col->get_column_name_str();
          if (0 == name.case_compare(def.column_name_)) {
            col_id = col->get_column_id();
          } else if (0 == name.compare(hidden_state.string())) {
            state_id = col->get_column_id();
          } else if (0 == name.compare(hidden_request.string())) {
            request_id = col->get_column_id();
          } else if (0 == name.compare(hidden_spec.string())) {
            spec_id = col->get_column_id();
          }
        }
      }
      if (OB_SUCC(ret)
          && (OB_INVALID_ID == col_id || OB_INVALID_ID == state_id
              || OB_INVALID_ID == request_id || OB_INVALID_ID == spec_id)) {
        // The auxiliary columns are part of the table schema: without them the row
        // cannot carry its task state, so the catalogue row would describe storage
        // that does not exist.
        ret = OB_ERR_UNEXPECTED;
        SQL_ENG_LOG(ERROR, "ai column auxiliary columns are missing from the table schema",
                    K(ret), K(def.column_name_), K(col_id), K(state_id), K(request_id),
                    K(spec_id));
      }
      ObSqlString prompt;
      ObSqlString normalized;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ai_column_type_prompt(def.result_type_, prompt))) {
      } else if (OB_FAIL(normalized.append(def.expr_))) {
      } else if (prompt.length() > 0
                 && OB_FAIL(normalized.append_fmt(" /* %s */", prompt.ptr()))) {
      }
      // 1. catalogue row
      if (OB_SUCC(ret)) {
        ObSqlString catalog;
        ObSqlString escaped_expr;
        ObSqlString escaped_deps;
        ObSqlString escaped_prompt;
        if (OB_FAIL(ai_column_escape(ObString(normalized.length(), normalized.ptr()), escaped_expr))
            || OB_FAIL(ai_column_escape(def.depend_columns_, escaped_deps))
            || OB_FAIL(ai_column_escape(def.prompt_expr_, escaped_prompt))
            || OB_FAIL(catalog.append_fmt(
                 "INSERT INTO oceanbase.__all_ai_column (table_id, column_id, column_name, "
                 "ai_function, model_name, normalized_expr, prompt_expr, depend_columns, sync_mode, "
                 "max_retries, state_column_id, request_column_id, result_spec_column_id, "
                 "result_type, vector_dim) VALUES (%lu, %lu, '%.*s', '%.*s', '%.*s', '%s', '%s', '%s', "
                 "'async', %ld, %lu, %lu, %lu, '%.*s', %ld)",
                 table_id, col_id, def.column_name_.length(), def.column_name_.ptr(),
                 def.function_.length(), def.function_.ptr(),
                 def.model_name_.length(), def.model_name_.ptr(),
                 ai_column_str(escaped_expr), ai_column_str(escaped_prompt),
                 ai_column_str(escaped_deps),
                 def.max_retries_, state_id, request_id, spec_id,
                 def.result_type_.length(), def.result_type_.ptr(), def.vector_dim_))) {
        } else if (OB_FAIL(ai_column_exec(conn, catalog))) {
  }
      }
    }
  }
  if (OB_SUCC(ret) && !defs.empty()) {
    int tmp_ret = ai_column::add_ai_column_table(table_id);
    if (OB_SUCCESS != tmp_ret) {
      SQL_ENG_LOG(WARN, "register ai column table failed", K(tmp_ret), K(table_id));
    }
  }
  if (OB_SUCC(ret) && !defs.empty() && OB_FAIL(ai_column_worker_start())) {
    ret = OB_SUCCESS;  // a worker that cannot start must not fail the DDL
  }
  return ret;
}

// ---------------------------------------------------------------------------
// AI column background worker
//
// A task is one (row, ai column) pair.  Every invocation re-reads the row it
// was queued for: the prompt and the digest stored in the hidden request column
// are both taken from the *current* row, so an invocation never computes on a
// stale snapshot.  Publishing is guarded by that same digest, which makes a slow
// result harmless: the DML hook refreshes the digest of a row whenever one of
// its inputs changes, so a result that lost the race no longer matches and is
// dropped instead of overwriting the newer value.
//
// A model error costs one retry.  Once the retry budget of the column is spent
// the row is parked as FAILED and reported through __all_ai_column_failed.
// ---------------------------------------------------------------------------
static const int64_t AI_TASK_PROMPT_BUF = 8 * 1024;
static const int64_t AI_TASK_RESULT_BUF = 32 * 1024;
static const int64_t AI_TASK_PRED_BUF = 1024;
static const char *AI_STATE_PENDING = "PENDING";
static const char *AI_STATE_READY = "READY";
static const char *AI_STATE_FAILED = "FAILED";
// __all_ai_column_task.state: the queue is readable from SQL, so the states are
// plain numbers instead of the strings used by the hidden per row columns.
static const int64_t AI_TASK_PENDING = 1;
static const int64_t AI_TASK_RUNNING = 2;
static const int64_t AI_TASK_RETRYING = 3;
static const int64_t AI_TASK_FAILED = 4;

struct ObAiColumnTaskRow
{
  ObAiColumnTaskRow() { reset(); }
  void reset()
  {
    table_id_ = OB_INVALID_ID;
    column_id_ = OB_INVALID_ID;
    retry_count_ = 0;
    max_retries_ = 0;
    prompt_len_ = 0;
    result_len_ = 0;
    request_len_ = 0;
    row_gone_ = false;
    pred_[0] = '\0';
    db_[0] = '\0';
    tbl_[0] = '\0';
    col_[0] = '\0';
    req_col_[0] = '\0';
    state_col_[0] = '\0';
    model_[0] = '\0';
    func_[0] = '\0';
    rtype_[0] = '\0';
    prompt_[0] = '\0';
    result_[0] = '\0';
    request_[0] = '\0';
  }
  // copy one fetched value into a fixed buffer, truncating the tail if needed
  static void copy_str(char *buf, int32_t &len, const int64_t cap, const common::ObString &v)
  {
    const int32_t n = static_cast<int32_t>(v.length() < cap - 1 ? v.length() : cap - 1);
    if (n > 0) {
      MEMCPY(buf, v.ptr(), n);
    }
    buf[n] = '\0';
    len = n;
  }
  // the hidden state/request column names of this AI column (fixed naming rule)
  void hidden_names()
  {
    snprintf(state_col_, sizeof(state_col_), "__ai_%s_state", col_);
    snprintf(req_col_, sizeof(req_col_), "__ai_%s_request", col_);
  }
  uint64_t table_id_;
  uint64_t column_id_;
  int64_t retry_count_;
  int64_t max_retries_;
  bool row_gone_;
  char pred_[AI_TASK_PRED_BUF];
  char db_[256];
  char tbl_[256];
  char col_[256];
  char req_col_[256];
  char state_col_[256];
  char model_[128];
  char func_[64];
  char rtype_[64];
  char request_[128];
  int32_t request_len_;
  char prompt_[AI_TASK_PROMPT_BUF];
  int32_t prompt_len_;
  char result_[AI_TASK_RESULT_BUF];
  int32_t result_len_;
  TO_STRING_KV(K_(table_id), K_(column_id), K_(pred), K_(db), K_(tbl), K_(col),
               K_(retry_count));
};

// Run a statement and keep the first column of the first row.
int ai_worker_scalar(common::ObMySQLProxy &proxy, const ObString &sql, ObSqlString &out)
{
  int ret = OB_SUCCESS;
  out.reset();
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    common::sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(proxy.read(res, sql.ptr(), static_cast<const ObSessionParam *>(NULL)))) {
      SQL_ENG_LOG(WARN, "ai worker read failed", K(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    } else {
      ObString value;
      if (OB_FAIL(result->get_varchar(static_cast<int64_t>(0), value))) {
        SQL_ENG_LOG(WARN, "ai worker read value failed", K(ret), K(sql));
      } else if (OB_FAIL(out.append(value))) {
  }
    }
  }
  return ret;
}

// Run a write statement and report how many rows it touched.
int ai_worker_exec(common::ObMySQLProxy &proxy, const ObString &sql, int64_t &affected)
{
  int ret = OB_SUCCESS;
  affected = 0;
  if (OB_FAIL(proxy.write(sql, affected))) {
    SQL_ENG_LOG(WARN, "ai worker write failed", K(ret), K(sql));
  }
  return ret;
}

class ObAiColumnWorker : public common::ObTimerTask
{
public:
  static ObAiColumnWorker &get_instance()
  {
    static ObAiColumnWorker instance;
    return instance;
  }
  int start()
  {
    int ret = OB_SUCCESS;
    if (false == ATOMIC_CAS(&is_started_, false, true)) {
      if (OB_FAIL(timer_.init("ai_column_worker"))) {
        SQL_ENG_LOG(WARN, "ai column worker timer init failed", K(ret));
      } else if (OB_FAIL(timer_.schedule(*this, AI_WORKER_INTERVAL_US, true, false))) {
        SQL_ENG_LOG(WARN, "ai column worker timer schedule failed", K(ret));
      } else {
        is_started_ = true;
        SQL_ENG_LOG(INFO, "ai column worker started");
      }
    }
    return ret;
  }
  virtual void runTimerTask() override
  {
    int ret = process_once();
    if (OB_FAIL(ret) && OB_EAGAIN != ret) {
      SQL_ENG_LOG(WARN, "ai column worker round failed", K(ret));
    }
  }
private:
  static const int64_t AI_WORKER_INTERVAL_US = 100 * 1000;  // 100 ms
  static const int64_t AI_WORKER_BATCH = 16;
  ObAiColumnWorker() : is_started_(false), recovered_(false) {}
  int process_once();
  int recover();
  int fetch_tasks(int64_t &count);
  int claim(ObAiColumnTaskRow &row);
  int requeue(ObAiColumnTaskRow &row);
  int load_input(ObAiColumnTaskRow &row);
  int call_model(ObAiColumnTaskRow &row);
  int publish(ObAiColumnTaskRow **rows, const int64_t count);
  int retry_later(const ObAiColumnTaskRow &row, const int cause);
  int mark_failed(const ObAiColumnTaskRow &row, const int cause);
  int drop_task(const ObAiColumnTaskRow &row);
  ObAiColumnTaskRow rows_[AI_WORKER_BATCH];
  bool is_started_;
  bool recovered_;
  common::ObTimer timer_;
};

int ObAiColumnWorker::fetch_tasks(int64_t &count)
{
  int ret = OB_SUCCESS;
  count = 0;
  common::ObMySQLProxy *proxy = GCTX.sql_proxy_;
  if (OB_ISNULL(proxy)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObSqlString sql;
    // oldest eligible task first: a row that keeps changing must not starve
    if (OB_FAIL(sql.append_fmt(
          "SELECT CAST(k.table_id AS UNSIGNED), CAST(k.column_id AS UNSIGNED), "
          "k.rowkey_predicate, k.retry_count, "
          "d.database_name, t.table_name, c.column_name, c.model_name, "
          "ifnull(c.prompt_expr, ''), c.ai_function, ifnull(c.result_type, ''), "
          "ifnull(c.max_retries, 0) "
          "FROM %s.__all_ai_column_task k "
          "JOIN %s.__all_ai_column c ON c.table_id = k.table_id AND c.column_id = k.column_id "
          "JOIN %s.__all_table t ON t.table_id = k.table_id "
          "JOIN %s.__all_database d ON d.database_id = t.database_id "
          "WHERE k.state IN (%ld, %ld) AND k.eligible_ts <= NOW(6) "
          "ORDER BY k.eligible_ts, k.table_id, k.rowkey_predicate, k.column_id LIMIT %ld",
          OB_SYS_DATABASE_NAME, OB_SYS_DATABASE_NAME, OB_SYS_DATABASE_NAME,
          OB_SYS_DATABASE_NAME, AI_TASK_PENDING, AI_TASK_RETRYING, AI_WORKER_BATCH))) {
      SQL_ENG_LOG(WARN, "build ai task query failed", K(ret));
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        common::sqlclient::ObMySQLResult *result = NULL;
        if (OB_FAIL(proxy->read(res, sql.ptr(), static_cast<const ObSessionParam *>(NULL)))) {
          SQL_ENG_LOG(WARN, "ai worker fetch tasks failed", K(ret));
        } else if (OB_ISNULL(result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          while (OB_SUCC(ret) && count < AI_WORKER_BATCH) {
            ret = result->next();
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else if (OB_FAIL(ret)) {
              break;
            }
            ObAiColumnTaskRow &row = rows_[count];
            ObString value;
            int64_t ivalue = 0;
            row.reset();
            if (OB_FAIL(result->get_uint(static_cast<int64_t>(0), row.table_id_))
                || OB_FAIL(result->get_uint(static_cast<int64_t>(1), row.column_id_))) {
              SQL_ENG_LOG(WARN, "read ai task ids failed", K(ret));
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(2), value))
                       || FALSE_IT(snprintf(row.pred_, sizeof(row.pred_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_int(static_cast<int64_t>(3), row.retry_count_))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(4), value))
                       || FALSE_IT(snprintf(row.db_, sizeof(row.db_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(5), value))
                       || FALSE_IT(snprintf(row.tbl_, sizeof(row.tbl_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(6), value))
                       || FALSE_IT(snprintf(row.col_, sizeof(row.col_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(7), value))
                       || FALSE_IT(snprintf(row.model_, sizeof(row.model_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(8), value))
                       || FALSE_IT(ObAiColumnTaskRow::copy_str(
                            row.prompt_, row.prompt_len_, AI_TASK_PROMPT_BUF, value))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(9), value))
                       || FALSE_IT(snprintf(row.func_, sizeof(row.func_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_varchar(static_cast<int64_t>(10), value))
                       || FALSE_IT(snprintf(row.rtype_, sizeof(row.rtype_), "%.*s",
                                            value.length(), value.ptr()))) {
            } else if (OB_FAIL(result->get_int(static_cast<int64_t>(11), ivalue))) {
            } else {
              row.max_retries_ = ivalue;
              row.hidden_names();
              ++count;
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAiColumnWorker::recover()
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString stmt;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(stmt.append_fmt(
               "UPDATE %s.__all_ai_column_task SET state = %ld WHERE state = %ld",
               OB_SYS_DATABASE_NAME, AI_TASK_PENDING, AI_TASK_RUNNING))) {
    SQL_ENG_LOG(WARN, "build ai recovery sql failed", K(ret));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  } else if (affected > 0) {
    SQL_ENG_LOG(INFO, "recovered ai column tasks", K(affected));
  }
  return ret;
}

// Take ownership of a task with a conditional update: only the round that flips
// the row out of PENDING/RETRYING may execute it.
int ObAiColumnWorker::claim(ObAiColumnTaskRow &row)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString escaped_pred;
  ObSqlString stmt;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ai_column_escape(ObString(static_cast<int32_t>(strlen(row.pred_)), row.pred_),
                                      escaped_pred))
             || OB_FAIL(stmt.append_fmt(
                  "UPDATE %s.__all_ai_column_task SET state = %ld "
                  "WHERE table_id = %lu AND column_id = %lu AND rowkey_predicate = '%s' "
                  "AND state IN (%ld, %ld)",
                  OB_SYS_DATABASE_NAME, AI_TASK_RUNNING, row.table_id_, row.column_id_,
                  ai_column_str(escaped_pred), AI_TASK_PENDING, AI_TASK_RETRYING))) {
    SQL_ENG_LOG(WARN, "build ai claim sql failed", K(ret), K(row));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  } else if (0 == affected) {
    ret = OB_EAGAIN;
  }
  return ret;
}

// Put the task back without spending a retry: the row changed while the model
// was running, so the newer inputs have to be used for the next attempt.
int ObAiColumnWorker::requeue(ObAiColumnTaskRow &row)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString escaped_pred;
  ObSqlString stmt;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ai_column_escape(ObString(static_cast<int32_t>(strlen(row.pred_)), row.pred_),
                                      escaped_pred))
             || OB_FAIL(stmt.append_fmt(
                  "UPDATE %s.__all_ai_column_task SET state = %ld, "
                  "eligible_ts = DATE_ADD(NOW(6), INTERVAL 100000 MICROSECOND) "
                  "WHERE table_id = %lu AND column_id = %lu AND rowkey_predicate = '%s'",
                  OB_SYS_DATABASE_NAME, AI_TASK_PENDING, row.table_id_, row.column_id_,
                  ai_column_str(escaped_pred)))) {
    SQL_ENG_LOG(WARN, "build ai requeue sql failed", K(ret), K(row));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  }
  return ret;
}

// Re-read the row the task was queued for: the prompt is evaluated against the
// current inputs and the digest of those inputs comes from the hidden request
// column, so a task that was queued long ago never computes on old data.
int ObAiColumnWorker::load_input(ObAiColumnTaskRow &row)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_FAIL(sql.append_fmt("SELECT %s, `%s` FROM `%s`.`%s` WHERE %s AND `%s` = '%s'",
                             row.prompt_, row.req_col_, row.db_, row.tbl_, row.pred_,
                             row.state_col_, AI_STATE_PENDING))) {
    SQL_ENG_LOG(WARN, "build ai input query failed", K(ret));
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_ISNULL(GCTX.sql_proxy_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr(), static_cast<const ObSessionParam *>(NULL)))) {
        SQL_ENG_LOG(WARN, "read ai input failed", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          // the row is gone, or it already reached a final state
          ret = OB_SUCCESS;
          row.row_gone_ = true;
        } else {
          SQL_ENG_LOG(WARN, "read ai input row failed", K(ret), K(sql));
        }
      } else {
        ObString value;
        int tmp_ret = result->get_varchar(static_cast<int64_t>(0), value);
        if (OB_ERR_NULL_VALUE == tmp_ret) {
          // the prompt evaluated to NULL, there is nothing to ask the model
          row.row_gone_ = true;
        } else if (OB_FAIL(tmp_ret)) {
          SQL_ENG_LOG(WARN, "read ai prompt failed", K(tmp_ret), K(sql));
        } else if (FALSE_IT(ObAiColumnTaskRow::copy_str(
                     row.prompt_, row.prompt_len_, AI_TASK_PROMPT_BUF, value))) {
        } else if (OB_ERR_NULL_VALUE
                   == (tmp_ret = result->get_varchar(static_cast<int64_t>(1), value))) {
          // no digest means the row was never queued by the hook
          row.row_gone_ = true;
        } else if (OB_FAIL(tmp_ret)) {
          SQL_ENG_LOG(WARN, "read ai request digest failed", K(tmp_ret), K(sql));
        } else {
          ObAiColumnTaskRow::copy_str(
              row.request_, row.request_len_, sizeof(row.request_), value);
        }
      }
    }
  }
  return ret;
}

int ObAiColumnWorker::call_model(ObAiColumnTaskRow &row)
{
  int ret = OB_SUCCESS;
  ObSqlString escaped_prompt;
  ObSqlString sys_cfg;
  ObSqlString call_sql;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ai_column_escape(ObString(row.prompt_len_, row.prompt_), escaped_prompt))) {
    SQL_ENG_LOG(WARN, "escape ai prompt failed", K(ret));
  } else if (0 == ObString::make_string(row.rtype_).case_compare("json")) {
    ret = sys_cfg.append("{\"system\":\"Return exactly one valid JSON value\"}");
  } else if (0 == ObString::make_string(row.rtype_).case_compare("integer")) {
    ret = sys_cfg.append("{\"system\":\"Return exactly one base-10 integer\"}");
  } else if (0 == ObString::make_string(row.rtype_).case_compare("real")) {
    ret = sys_cfg.append("{\"system\":\"Return exactly one decimal number\"}");
  }
  if (OB_FAIL(ret)) {
  } else if (0 == ObString::make_string(row.func_).case_compare("ai_embed")) {
    ret = call_sql.append_fmt("SELECT AI_EMBED('%s', '%s')", row.model_,
                              ai_column_str(escaped_prompt));
  } else {
    ret = call_sql.append_fmt("SELECT AI_COMPLETE('%s', '%s', '%s')", row.model_,
                              ai_column_str(escaped_prompt),
                              sys_cfg.length() > 0 ? sys_cfg.ptr() : "{}");
  }
  if (OB_FAIL(ret)) {
    SQL_ENG_LOG(WARN, "build ai call failed", K(ret), K(row));
  } else {
    ObSqlString called;
    if (OB_FAIL(ai_worker_scalar(*GCTX.sql_proxy_,
                                 ObString(call_sql.length(), call_sql.ptr()), called))) {
      SQL_ENG_LOG(WARN, "ai model call failed", K(ret), K(row), K(call_sql));
    } else {
      ObAiColumnTaskRow::copy_str(row.result_, row.result_len_, AI_TASK_RESULT_BUF,
                                  ObString(called.length(), called.ptr()));
    }
  }
  return ret;
}

// Publish a whole request group with one statement: every AI column of the same
// row shares one request id, so they must become visible together.  The guard
// keeps the digest contract as well - a group whose inputs changed while the
// model was running no longer matches and is left queued for the newer inputs.
int ObAiColumnWorker::publish(ObAiColumnTaskRow **rows, const int64_t count)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString escaped;
  ObSqlString update;
  if (OB_ISNULL(GCTX.sql_proxy_) || NULL == rows || count <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(update.append_fmt("UPDATE `%s`.`%s` SET ",
                                       rows[0]->db_, rows[0]->tbl_))) {
    SQL_ENG_LOG(WARN, "build ai publish sql failed", K(ret), KPC(rows[0]));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    escaped.reset();
    if (OB_FAIL(ai_column_escape(ObString(rows[i]->result_len_, rows[i]->result_),
                                 escaped))) {
    } else if (OB_FAIL(update.append_fmt("%s`%s` = '%s', `%s` = '%s'",
                                         i > 0 ? ", " : "",
                                         rows[i]->col_, ai_column_str(escaped),
                                         rows[i]->state_col_, AI_STATE_READY))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(update.append_fmt(" WHERE %s", rows[0]->pred_))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    escaped.reset();
    if (OB_FAIL(ai_column_escape(ObString(rows[i]->request_len_, rows[i]->request_),
                                 escaped))) {
    } else if (OB_FAIL(update.append_fmt(" AND (`%s` = '%s' AND `%s` = '%s')",
                                         rows[i]->req_col_, ai_column_str(escaped),
                                         rows[i]->state_col_, AI_STATE_PENDING))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(update.length(), update.ptr()), affected))) {
  } else if (0 == affected) {
    // an input changed while the model was running: the tasks stay queued and
    // the next invocation picks the row up again with the newer inputs
    SQL_ENG_LOG(INFO, "ai result was superseded before publish", KPC(rows[0]));
    ret = OB_EAGAIN;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      if (OB_FAIL(drop_task(*rows[i]))) {
      }
    }
  }
  return ret;
}

int ObAiColumnWorker::drop_task(const ObAiColumnTaskRow &row)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString escaped_pred;
  ObSqlString stmt;
  if (OB_FAIL(ai_column_escape(ObString(static_cast<int32_t>(strlen(row.pred_)), row.pred_),
                               escaped_pred))
      || OB_FAIL(stmt.append_fmt(
           "DELETE FROM %s.__all_ai_column_task WHERE table_id = %lu AND column_id = %lu "
           "AND rowkey_predicate = '%s'",
           OB_SYS_DATABASE_NAME, row.table_id_, row.column_id_,
           ai_column_str(escaped_pred)))) {
    SQL_ENG_LOG(WARN, "build ai task delete failed", K(ret), K(row));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  }
  return ret;
}

int ObAiColumnWorker::retry_later(const ObAiColumnTaskRow &row, const int cause)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString escaped_pred;
  ObSqlString stmt;
  const int64_t backoff_ms = 1000 * (row.retry_count_ + 1);
  if (OB_FAIL(ai_column_escape(ObString(static_cast<int32_t>(strlen(row.pred_)), row.pred_),
                               escaped_pred))
      || OB_FAIL(stmt.append_fmt(
           "UPDATE %s.__all_ai_column_task SET state = %ld, retry_count = %ld, "
           "eligible_ts = DATE_ADD(NOW(6), INTERVAL %ld MICROSECOND) "
           "WHERE table_id = %lu AND column_id = %lu AND rowkey_predicate = '%s'",
           OB_SYS_DATABASE_NAME, AI_TASK_RETRYING, row.retry_count_ + 1, backoff_ms * 1000,
           row.table_id_, row.column_id_, ai_column_str(escaped_pred)))) {
    SQL_ENG_LOG(WARN, "build ai retry sql failed", K(ret), K(row));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  } else {
    SQL_ENG_LOG(INFO, "ai task scheduled for retry", K(cause), K(row));
  }
  return ret;
}

int ObAiColumnWorker::mark_failed(const ObAiColumnTaskRow &row, const int cause)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  ObSqlString escaped_pred;
  ObSqlString stmt;
  if (OB_FAIL(ai_column_escape(ObString(static_cast<int32_t>(strlen(row.pred_)), row.pred_),
                               escaped_pred))
      || OB_FAIL(stmt.append_fmt(
           "UPDATE `%s`.`%s` SET `%s` = '%s' WHERE %s AND `%s` = '%s'",
           row.db_, row.tbl_, row.state_col_, AI_STATE_FAILED, row.pred_,
           row.req_col_, row.request_))) {
    SQL_ENG_LOG(WARN, "build ai failed state sql failed", K(ret), K(row));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  } else if (OB_FAIL(stmt.assign_fmt(
               "REPLACE INTO %s.__all_ai_column_failed (table_id, column_id, rowkey_predicate, "
               "retry_count, error_msg) VALUES (%lu, %lu, '%s', %ld, 'ai call failed: %d')",
               OB_SYS_DATABASE_NAME, row.table_id_, row.column_id_,
               ai_column_str(escaped_pred), row.retry_count_ + 1, cause))) {
    SQL_ENG_LOG(WARN, "build ai failure record failed", K(ret), K(row));
  } else if (OB_FAIL(ai_worker_exec(*GCTX.sql_proxy_,
                                    ObString(stmt.length(), stmt.ptr()), affected))) {
  } else if (OB_FAIL(drop_task(row))) {
  }
  return ret;
}

int ObAiColumnWorker::process_once()
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  bool ready[AI_WORKER_BATCH];
  for (int64_t i = 0; i < AI_WORKER_BATCH; ++i) {
    ready[i] = false;
  }
  if (!recovered_) {
    // a task claimed before the server stopped keeps the RUNNING state; putting
    // it back is idempotent, and the attempt repeats until the metadata is up
    const int tmp_ret = recover();
    if (OB_SUCCESS == tmp_ret) {
      recovered_ = true;
    }
  }
  if (OB_FAIL(fetch_tasks(count))) {
  } else if (count > 0) {
  }
  // phase 1: own the task and compute the result, publishing happens later so
  // that all the AI columns of one row are published together
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObAiColumnTaskRow &row = rows_[i];
    int tmp_ret = OB_SUCCESS;
    if (OB_EAGAIN == (tmp_ret = claim(row))) {
      // another round already owns the task
    } else if (OB_SUCCESS != tmp_ret) {
      SQL_ENG_LOG(WARN, "claim ai task failed", K(tmp_ret), K(row));
    } else if (OB_SUCCESS != (tmp_ret = load_input(row))) {
      SQL_ENG_LOG(WARN, "load ai task input failed", K(tmp_ret), K(row));
      if (OB_TABLE_NOT_EXIST == tmp_ret || OB_ERR_UNKNOWN_TABLE == tmp_ret
          || OB_ERR_BAD_FIELD_ERROR == tmp_ret || OB_ERR_ILLEGAL_ID == tmp_ret) {
        // the table or one of its AI columns was dropped while the task waited:
        // retrying can never succeed and would delay the rest of the queue
        int tmp2 = drop_task(row);
        SQL_ENG_LOG(WARN, "drop ai task of a dropped table", K(tmp_ret), K(tmp2), K(row));
      } else {
        int tmp2 = retry_later(row, tmp_ret);
        SQL_ENG_LOG(WARN, "ai task will be retried", K(tmp_ret), K(tmp2), K(row));
      }
    } else if (row.row_gone_) {
      if (OB_SUCCESS != (tmp_ret = drop_task(row))) {
        SQL_ENG_LOG(WARN, "drop stale ai task failed", K(tmp_ret), K(row));
      }
    } else if (OB_SUCCESS != (tmp_ret = call_model(row))) {
      const int next_retry = static_cast<int>(row.retry_count_ + 1);
      if (next_retry > row.max_retries_) {
        int tmp2 = mark_failed(row, tmp_ret);
        SQL_ENG_LOG(WARN, "ai task reached its retry budget", K(tmp_ret), K(tmp2), K(row));
      } else {
        int tmp2 = retry_later(row, tmp_ret);
        SQL_ENG_LOG(WARN, "ai task will be retried", K(tmp_ret), K(tmp2), K(row));
      }
    } else {
      ready[i] = true;
    }
  }
  // phase 2: publish one statement per (row, request) group
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObAiColumnTaskRow *group[AI_WORKER_BATCH];
    int64_t members = 0;
    if (!ready[i]) {
      continue;
    }
    group[members++] = &rows_[i];
    ready[i] = false;
    for (int64_t j = i + 1; j < count; ++j) {
      if (ready[j]
          && rows_[j].table_id_ == rows_[i].table_id_
          && rows_[j].request_len_ == rows_[i].request_len_
          && 0 == MEMCMP(rows_[j].pred_, rows_[i].pred_, strlen(rows_[i].pred_) + 1)
          && 0 == MEMCMP(rows_[j].request_, rows_[i].request_, strlen(rows_[i].request_) + 1)) {
        ready[j] = false;
        group[members++] = &rows_[j];
      }
    }
    int tmp_ret = publish(group, members);
    if (OB_EAGAIN == tmp_ret) {
      // superseded by a newer write: no retry is spent on it
      for (int64_t k = 0; OB_SUCC(ret) && k < members; ++k) {
        int tmp2 = requeue(*group[k]);
        SQL_ENG_LOG(INFO, "ai publish deferred to the newer inputs", K(tmp2), KPC(group[k]));
      }
    } else if (OB_SUCCESS != tmp_ret) {
      SQL_ENG_LOG(WARN, "publish ai result failed", K(tmp_ret), KPC(group[0]));
    } else {
    }
  }
  return ret;
}

int ai_column_worker_start()
{
  int ret = ObAiColumnWorker::get_instance().start();
  if (OB_FAIL(ret)) {
    SQL_ENG_LOG(WARN, "start ai column worker failed", K(ret));
  }
  return ret;
}

}  // namespace

// Called from the observer boot path: tasks queued before a restart have to be
// drained even if no new AI column DDL arrives afterwards.
int ob_ai_column_worker_start()
{
  return ai_column_worker_start();
}

int ObCreateTableExecutor::execute(ObExecContext &ctx, ObCreateTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  obcall::ObCreateTableRes res;
  obcall::ObCreateTableArg &create_table_arg = stmt.get_create_table_arg();
  ObString first_stmt;
  ObSelectStmt *select_stmt = stmt.get_sub_select();
  const ObTableSchema &table_schema = create_table_arg.schema_;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  
  if (OB_ISNULL(my_session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  }

  if (OB_FAIL(ret)) {
  } else {
    create_table_arg.is_inner_ = my_session->is_inner();
    const_cast<obcall::ObCreateTableArg&>(create_table_arg).ddl_stmt_str_ = first_stmt;
    bool enable_parallel_create_table = false;
    {
      enable_parallel_create_table = true
                                     && config::_enable_parallel_table_creation();

    }
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(ObPartitionExecutorUtils::calc_values_exprs(ctx, stmt))) {
    } else if (OB_FAIL(set_index_arg_list(ctx, stmt))) {
    } else if (OB_ISNULL(select_stmt)) { // Processing for normal table creation
      bool &is_parallel_create = create_table_arg.is_parallel_;
      if (OB_FAIL(ctx.get_sql_ctx()->schema_guard_->reset())){
      } else if (table_schema.is_view_table()) {
        is_parallel_create = false;
      } else {
        is_parallel_create = true
                             && config::_enable_parallel_table_creation();
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else {
        DEBUG_SYNC(BEFORE_SEND_PARALLEL_CREATE_TABLE);
        if (OB_FAIL(ObDDLExecutorUtil::execute_pcreate_table(
            my_session, ctx.root_command_service(), "[parallel create table]",
                                                          create_table_arg, res))) {
        } else if (OB_FAIL(apply_ai_columns(ctx, table_schema, res.table_id_,
                                            create_table_arg.db_name_.empty()
                                                ? my_session->get_database_name()
                                                : create_table_arg.db_name_,
                                            stmt.get_ai_column_defs()))) {
          SQL_ENG_LOG(WARN, "fail to apply ai column metadata", K(ret));
        }
      }
    } else {
      if (OB_FAIL(execute_ctas(ctx, stmt))){
      }
    }
    SERVER_EVENT_ADD("ddl", "create table execute finish",
      "ret", ret,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "rpc_dst", GCTX.self_addr(),
      "table_info", res.table_id_,
      "schema_version", res.schema_version_);
    SQL_ENG_LOG(INFO, "finish create table execute.", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));

    // only CTAS or create temporary table will make session_id != 0. If such table detected, set
    // need ctas cleanup task anyway to do some cleanup jobs
    if (0 != table_schema.get_session_id()) {
      query::request_query_ctas_cleanup(
          ctx.get_query_runtime_environment());
    }
  }
  return ret;
}

int ObCreateTableExecutor::set_index_arg_list(ObExecContext &ctx, ObCreateTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  obcall::ObCreateTableArg &create_table_arg = const_cast<obcall::ObCreateTableArg &>(stmt.get_create_table_arg());
  if (stmt.get_index_partition_resolve_results().count()
      != stmt.get_index_arg_list().count()) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_index_arg_list().count(); i++) {
    HEAP_VAR(ObCreateIndexStmt, index_stmt) {
      ObPartitionResolveResult &resolve_result = stmt.get_index_partition_resolve_results().at(i);
      index_stmt.get_part_fun_exprs() = resolve_result.get_part_fun_exprs();
      index_stmt.get_part_values_exprs() = resolve_result.get_part_values_exprs();
      index_stmt.get_subpart_fun_exprs() = resolve_result.get_subpart_fun_exprs();
      index_stmt.get_template_subpart_values_exprs() = resolve_result.get_template_subpart_values_exprs();
      index_stmt.get_individual_subpart_values_exprs() = resolve_result.get_individual_subpart_values_exprs();
      if (OB_FAIL(index_stmt.get_create_index_arg().assign(stmt.get_index_arg_list().at(i)))) {
      } else if (OB_FAIL(ObPartitionExecutorUtils::calc_values_exprs(ctx, index_stmt))) {
      } else if (OB_FAIL(create_table_arg.index_arg_list_.push_back(index_stmt.get_create_index_arg()))) {
      }
    }
  }
  return ret;
}

ObAlterTableExecutor::ObAlterTableExecutor()
{
}

ObAlterTableExecutor::~ObAlterTableExecutor()
{
}

int ObAlterTableExecutor::refresh_schema_for_table()
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const ObGlobalContext &gctx = GCTX;
  ObMultiVersionSchemaService *schema_service = gctx.schema_service_;
  int64_t local_version = OB_INVALID_VERSION;
  int64_t global_version = OB_INVALID_VERSION;

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_service->get_runtime_refreshed_schema_version(
          local_version))) {
  } else if (OB_FAIL(schema_service->get_published_schema_version(
          global_version))) {
  } else if (local_version < global_version) {
    LOG_INFO("try to refresh schema", K(local_version), K(global_version));
    // force refresh schema latest version
    if (OB_FAIL(schema_service->refresh_and_add_schema())) {
    }
  }
  return ret;
}

/* From version 3100 onwards, the alter table logic is to put index creation and other operations into the same rpc sent to rs, and after returning, synchronize each created index, etc., if an index creation fails, roll back all indexes
   MySQL mode supports alter table to perform index creation operations and other operations simultaneously, need to ensure that rs processes drop index after processing add index
   Otherwise, prefix indexes will have issues:
*/
int ObAlterTableExecutor::alter_table_rpc_v2(
    obcall::ObAlterTableArg &alter_table_arg,
    obcall::ObAlterTableRes &res,
    common::ObIAllocator &allocator,
    ObSQLSessionInfo *my_session,
    query::ObIRootCommandService &root_commands,
    query::ObIQueryRuntimeEnvironment &runtime_environment,
    query::ObILocalCommandService &local_commands)
{
  int ret = OB_SUCCESS;
  // do not support cancel drop_index_task.
  bool is_support_cancel = true;
  const ObSArray<obcall::ObIndexArg *> index_arg_list = alter_table_arg.index_arg_list_;
  ObSArray<obcall::ObIndexArg *> add_index_arg_list;
  ObSArray<obcall::ObIndexArg *> drop_index_args;
  alter_table_arg.index_arg_list_.reset();
  
  if (OB_ISNULL(my_session)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < index_arg_list.size(); ++i) {
    obcall::ObIndexArg *index_arg = index_arg_list.at(i);
    if (OB_ISNULL(index_arg)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (obcall::ObIndexArg::ADD_INDEX == index_arg->index_action_type_) {
      if (OB_FAIL(add_index_arg_list.push_back(index_arg))) {
      }
    } else if (obcall::ObIndexArg::DROP_INDEX == index_arg->index_action_type_) {
      if (OB_FAIL(drop_index_args.push_back(index_arg))) {
      } else if (OB_FAIL(alter_table_arg.index_arg_list_.push_back(index_arg))) {
      } else {
        obcall::ObDropIndexArg *drop_index_arg =
            static_cast<obcall::ObDropIndexArg *>(index_arg);
        drop_index_arg->is_add_to_scheduler_ = true;
        is_support_cancel = false;
      }
    } else { // for rename/drop index action
      if (OB_FAIL(alter_table_arg.index_arg_list_.push_back(index_arg))) {
      }
    }
  }
  // for add index action
  for (int64_t i = 0; OB_SUCC(ret) && i < add_index_arg_list.size(); ++i) {
    if (OB_FAIL(alter_table_arg.index_arg_list_.push_back(add_index_arg_list.at(i)))) {
    }
  }
  if (OB_SUCC(ret)) {
    AlterTableSchema &alter_table_schema = const_cast<AlterTableSchema &>(alter_table_arg.alter_table_schema_);
    if (OB_FAIL(populate_based_schema_obj_info_(alter_table_arg))) {
    } else {
      alter_table_arg.data_version_ = DATA_CURRENT_VERSION;
    }
    DEBUG_SYNC(BEFORE_SEND_ALTER_TABLE);

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return root_commands.alter_table(alter_table_arg, res); }))) {
    } else {
      // In rollback, retries will not be attempted, nor will the schema version be checked
      alter_table_arg.based_schema_object_infos_.reset();
    }
  }

  if (OB_SUCC(ret)) {
    ObIArray<obcall::ObDDLRes> &ddl_ress = res.ddl_res_array_;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_ress.count(); ++i) {
      obcall::ObDDLRes &ddl_res = ddl_ress.at(i);
      if (!alter_table_arg.is_update_global_indexes_ && OB_FAIL(ObDDLExecutorUtil::wait_ddl_finish(
          ddl_res.task_id_, res.ddl_need_retry_at_executor_, my_session,
          runtime_environment, local_commands, is_support_cancel))) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    ObCreateIndexExecutor create_index_executor;
    uint64_t failed_index_no = OB_INVALID_ID;
    // Handling of global index for drop/truncate partition
    if (alter_table_arg.is_update_global_indexes_
        && (obcall::ObAlterTableArg::DROP_PARTITION == alter_table_arg.alter_part_type_
        || obcall::ObAlterTableArg::DROP_SUB_PARTITION == alter_table_arg.alter_part_type_
        || obcall::ObAlterTableArg::TRUNCATE_PARTITION == alter_table_arg.alter_part_type_
        || obcall::ObAlterTableArg::TRUNCATE_SUB_PARTITION == alter_table_arg.alter_part_type_)) {
      common::ObSArray<obcall::ObAlterTableResArg> &res_array = res.res_arg_array_;
      for (int64_t i = 0; OB_SUCC(ret) && i < res_array.size(); ++i) {
        SMART_VAR(obcall::ObCreateIndexArg, create_index_arg) {
          create_index_arg.index_schema_.set_table_id(res_array.at(i).schema_id_);
          create_index_arg.index_schema_.set_schema_version(res_array.at(i).schema_version_);
          if (OB_FAIL(create_index_executor.sync_check_index_status(
              *my_session, root_commands, create_index_arg, res, allocator))) {
          }
        }
      }
    } else if (res.ddl_res_array_.empty()) {
    } else if (is_create_index(res.ddl_type_) || DDL_NORMAL_TYPE == res.ddl_type_) {
      // TODO(shuangcan): alter table create index returns DDL_NORMAL_TYPE now, check if we can fix this later
      // Synchronize until index creation is successful
      for (int64_t i = 0; OB_SUCC(ret) && i < add_index_arg_list.size(); ++i) {
        obcall::ObIndexArg *index_arg = add_index_arg_list.at(i);
        obcall::ObCreateIndexArg *create_index_arg = NULL;
        if (OB_ISNULL(index_arg)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (obcall::ObIndexArg::ADD_INDEX != index_arg->index_action_type_) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(create_index_arg = static_cast<obcall::ObCreateIndexArg *>(index_arg))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (INDEX_TYPE_PRIMARY == create_index_arg->index_type_ ||
            is_fts_index(create_index_arg->index_type_) ||
            is_multivalue_index(create_index_arg->index_type_)) {
          // TODO yunshan.tys temporary bypass, since res.res_arg_array_ is empty
          // TODO yunyi temporary bypass, since res.res_arg_array_ is empty
        } else {
          create_index_arg->index_schema_.set_table_id(res.res_arg_array_.at(i).schema_id_);
          create_index_arg->index_schema_.set_schema_version(res.res_arg_array_.at(i).schema_version_);
          if (OB_FAIL(create_index_executor.sync_check_index_status(
              *my_session, root_commands, *create_index_arg, res, allocator))) {
            failed_index_no = i;
          }
        }
      }
      // Rollback all established index
      if (OB_FAIL(ret)) {
        int tmp_ret = OB_SUCCESS;
        
        for (int64_t i = 0; (OB_SUCCESS == tmp_ret) && (i < add_index_arg_list.size()); ++i) {
          if (failed_index_no == i) {
            // Synchronization index building logic has already deleted this failure
            continue;
          } else {
            obcall::ObDropIndexArg drop_index_arg;
            obcall::ObDropIndexRes drop_index_res;
            obcall::ObCreateIndexArg *create_index_arg = static_cast<obcall::ObCreateIndexArg *>(add_index_arg_list.at(i));
            
            
            drop_index_arg.index_table_id_ = res.res_arg_array_.at(i).schema_id_;
            drop_index_arg.session_id_ = create_index_arg->session_id_;
            drop_index_arg.index_name_ = create_index_arg->index_name_;
            drop_index_arg.table_name_ = create_index_arg->table_name_;
            drop_index_arg.database_name_ = create_index_arg->database_name_;
            drop_index_arg.index_action_type_ = obcall::ObIndexArg::DROP_INDEX;
            drop_index_arg.is_add_to_scheduler_ = false;
            if (OB_SUCCESS != (tmp_ret = create_index_executor.set_drop_index_stmt_str(drop_index_arg, allocator))) {
            } else if (OB_SUCCESS != (tmp_ret = query::serialize_root_service_call(
                           [&]{ return root_commands.drop_index(drop_index_arg, drop_index_res); }))) {
            }
          }
        }
        if (OB_SUCCESS != tmp_ret && OB_INVALID_ID != failed_index_no) {    // rewrite LOG_USER_ERROR message
          uint64_t index_table_id = res.res_arg_array_.at(failed_index_no).schema_id_;
          int64_t schema_version = res.res_arg_array_.at(failed_index_no).schema_version_;
          bool is_finish = false;
          if (OB_SUCCESS != (tmp_ret = ObDDLExecutorUtil::wait_build_index_finish( res.task_id_, is_finish))) {
          }
        }
        LOG_INFO("added indexes failed, we rolled back all indexes added in this same alter table sql. But we didn't roll back other actions in this same alter table sql");
      }
    }
  }

  return ret;
}

int ObAlterTableExecutor::alter_table_exchange_partition_rpc(obcall::ObExchangePartitionArg &exchange_partition_arg,
                                                             obcall::ObAlterTableRes &res,
                                                             ObSQLSessionInfo *my_session,
                                                             query::ObIRootCommandService &root_commands)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(my_session) || OB_UNLIKELY(!exchange_partition_arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(query::serialize_root_service_call(
                 [&]{ return root_commands.exchange_partition(exchange_partition_arg, res); }))) {
  } else {
    // In rollback, retries will not be attempted, nor will the schema version be checked
    exchange_partition_arg.based_schema_object_infos_.reset();
  }
  return ret;
}

namespace {
// Full-text index creation is a scan-tokenize-sort-write pipeline whose wall
// clock is dominated by the doc-word INSERT ... SELECT child task.  The DDL
// default parallelism is 1, which leaves every worker but one idle, so pick a
// modest default DOP for FTS index builds.  Explicit PARALLEL hints and
// session/table level parallelism settings are honored.
const int64_t FTS_DDL_DEFAULT_MAX_DOP = 8;

int maybe_boost_fts_ddl_parallelism(ObAlterTableStmt &stmt, obcall::ObAlterTableArg &alter_table_arg)
{
  int ret = OB_SUCCESS;
  bool has_fts_index = false;
  for (int64_t i = 0; OB_SUCC(ret) && !has_fts_index
                      && i < alter_table_arg.index_arg_list_.count(); ++i) {
    obcall::ObIndexArg *index_arg = alter_table_arg.index_arg_list_.at(i);
    if (OB_ISNULL(index_arg)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("index arg is null", K(ret), K(i));
    } else if (obcall::ObIndexArg::ADD_INDEX == index_arg->index_action_type_) {
      obcall::ObCreateIndexArg *create_index_arg = static_cast<obcall::ObCreateIndexArg *>(index_arg);
      if (is_fts_index(create_index_arg->index_type_)) {
        has_fts_index = true;
      }
    }
  }
  if (OB_SUCC(ret) && has_fts_index && !stmt.get_has_parallel_hint()
      && alter_table_arg.parallelism_ <= 1) {
    const int64_t cpu_count = common::get_cpu_count();
    const int64_t dop = MAX(1L, MIN(cpu_count / 2, FTS_DDL_DEFAULT_MAX_DOP));
    alter_table_arg.parallelism_ = dop;
    LOG_INFO("boost fts ddl parallelism", K(dop), K(cpu_count));
  }
  return ret;
}
} // namespace

int ObAlterTableExecutor::execute_retry_ai_column(ObExecContext &ctx,
                                                  ObAlterTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  common::ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ai_worker_exec(*sql_proxy, stmt.get_retry_update_sql(), affected))) {
    SQL_ENG_LOG(WARN, "retry ai column failed", K(ret), K(stmt.get_retry_ai_column()));
  } else {
    // The update only drops the stored inputs, the maintenance hook that runs with
    // it re-queues the rows and hides the stale results.
    SQL_ENG_LOG(INFO, "retry ai column done", K(affected), K(stmt.get_retry_ai_column()));
  }
  return ret;
}

int ObAlterTableExecutor::execute(ObExecContext &ctx, ObAlterTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  if (stmt.has_retry_ai_column()) {
    // RETRY AI COLUMN never reaches the DDL service.
    return execute_retry_ai_column(ctx, stmt);
  }
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  obcall::ObAlterTableArg &alter_table_arg = stmt.get_alter_table_arg();
  obcall::ObExchangePartitionArg &exchange_partition_arg = stmt.get_exchange_partition_arg();
  ObString first_stmt;
  OZ (stmt.get_first_stmt(first_stmt));
  OV (OB_NOT_NULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx)), OB_NOT_INIT);
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (stmt.is_alter_triggers()) {
    if (stmt.get_tg_arg().trigger_infos_.count() > 0) {
      stmt.get_tg_arg().ddl_stmt_str_ = first_stmt;
      OZ (query::serialize_root_service_call(
          [&]{ return ctx.root_command_service().alter_trigger(stmt.get_tg_arg()); }),
          GCTX.self_addr());
    }
  } else {
    ObSQLSessionInfo *my_session = NULL;
    obcall::ObAlterTableRes res;
    bool need_modify_fk_validate = false;
    bool need_check = false;
    bool need_modify_notnull_validate = false;
    ObArenaAllocator allocator(ObModIds::OB_SQL_EXECUTOR);
    if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    } else {
      alter_table_arg.ddl_stmt_str_ = first_stmt;
      exchange_partition_arg.ddl_stmt_str_ = first_stmt;
      my_session = ctx.get_my_session();
      if (NULL == my_session) {
        ret = OB_ERR_UNEXPECTED;
      } else if (FALSE_IT(alter_table_arg.sql_mode_ = my_session->get_sql_mode())) {
        // do nothing
      } else if (FALSE_IT(alter_table_arg.parallelism_ = stmt.get_parallelism())) {
      } else if (OB_FAIL(check_alter_partition(ctx, stmt, alter_table_arg))) {
      } else if (OB_FAIL(check_alter_part_key(ctx, alter_table_arg))) {
      } else if (OB_FAIL(set_index_arg_list(ctx, stmt))) {
      } else if (OB_FAIL(maybe_boost_fts_ddl_parallelism(stmt, alter_table_arg))) {
      } else if (OB_INVALID_ID == alter_table_arg.session_id_
                && 0 != my_session->get_sessid_for_table()
                && FALSE_IT(alter_table_arg.session_id_ = my_session->get_sessid_for_table())) {
        //impossible
      } else {
        int64_t foreign_key_checks = 0;
        my_session->get_foreign_key_checks(foreign_key_checks);
        alter_table_arg.foreign_key_checks_ = foreign_key_checks;
        if ((obcall::ObAlterTableArg::ADD_CONSTRAINT == alter_table_arg.alter_constraint_type_
            || (obcall::ObAlterTableArg::ALTER_CONSTRAINT_STATE == alter_table_arg.alter_constraint_type_))) {
          if (OB_FAIL(need_check_constraint_validity(alter_table_arg, need_check))) {
          }
        }
        // If appending a foreign key with the validate attribute or modifying a foreign key to have the validate attribute, it does not take effect immediately
        // Validate existing data meets the foreign key's validate attribute requirements before activation, ensuring the optimizer can correctly optimize based on the foreign key's validate attribute
        if (OB_SUCC(ret) && alter_table_arg.foreign_key_checks_ && 1 == alter_table_arg.foreign_key_arg_list_.count()) {
          if ((!alter_table_arg.foreign_key_arg_list_.at(0).is_modify_fk_state_
              && alter_table_arg.foreign_key_arg_list_.at(0).validate_flag_)
              || (alter_table_arg.foreign_key_arg_list_.at(0).is_modify_validate_flag_
                  && alter_table_arg.foreign_key_arg_list_.at(0).validate_flag_)) {
            need_modify_fk_validate = true;
          }
        }
        if (OB_SUCC(ret)) {
          if ((obcall::ObAlterTableArg::EXCHANGE_PARTITION == alter_table_arg.alter_part_type_)
              || (obcall::ObAlterTableArg::EXCHANGE_SUBPARTITION == alter_table_arg.alter_part_type_)) {
            if (OB_FAIL(alter_table_exchange_partition_rpc(exchange_partition_arg,
                                                           res,
                                                           my_session,
                                                           ctx.root_command_service()))) {
            }
          } else if (OB_FAIL(alter_table_rpc_v2(alter_table_arg,
                                                res,
                                                allocator,
                                                my_session,
                                                ctx.root_command_service(),
                                                *ctx.get_query_runtime_environment(),
                                                ctx.local_command_service()))) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!need_check) {
        // do nothing, don't check if data is valid
      } else if (OB_FAIL(refresh_schema_for_table())) {
      } else if (OB_FAIL(ObDDLExecutorUtil::wait_ddl_finish(
          res.task_id_, res.ddl_need_retry_at_executor_, my_session,
          *ctx.get_query_runtime_environment(), ctx.local_command_service()))) {
      }
    }
    if (OB_SUCC(ret)
        && 1 == alter_table_arg.foreign_key_arg_list_.count()) {
      if (!need_modify_fk_validate) {
        // do nothing, don't check if data is valid
      } else {
        if (OB_FAIL(refresh_schema_for_table())) {
        } else if (OB_FAIL(ObDDLExecutorUtil::wait_ddl_finish(
            res.task_id_, res.ddl_need_retry_at_executor_, my_session,
            *ctx.get_query_runtime_environment(), ctx.local_command_service()))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      bool is_support_cancel = true;
      const bool need_wait_ddl_finish = is_double_table_long_running_ddl(res.ddl_type_)
                                     || is_simple_table_long_running_ddl(res.ddl_type_)
                                     || (ObDDLType::DDL_DROP_COLUMN_INSTANT == res.ddl_type_ && res.task_id_ > 0 /* with drop lob*/);
      if (OB_SUCC(ret) && need_wait_ddl_finish) {
        int64_t affected_rows = 0;
        if (OB_FAIL(refresh_schema_for_table())) {
        } else if (OB_FAIL(ObDDLExecutorUtil::wait_ddl_finish(
            res.task_id_, res.ddl_need_retry_at_executor_, my_session,
            *ctx.get_query_runtime_environment(), ctx.local_command_service(), is_support_cancel))) {
        }
      }
    }
    if (OB_SUCC(ret) && !stmt.get_ai_column_defs().empty()) {
      // The DDL request does not carry a usable table id on the executor side, so the
      // table is looked up by name after the schema has been refreshed.
      const share::schema::ObTableSchema *ai_tbl_schema = NULL;
      SMART_VAR(ObSchemaGetterGuard, ai_guard) {
        ObString ai_db_name = stmt.get_database_name();
        if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                ai_guard))) {
          ret = OB_SUCCESS;
        } else {
          if (ai_db_name.empty()) {
            ai_db_name = my_session->get_database_name();
          }
          if (OB_FAIL(ai_guard.get_table_schema(ai_db_name, stmt.get_table_name(),
                                                false /* is_index_table */, ai_tbl_schema))) {
          } else if (OB_ISNULL(ai_tbl_schema)) {
            ret = OB_TABLE_NOT_EXIST;
            SQL_ENG_LOG(WARN, "ai column target table is gone after alter table",
                        K(ret), K(ai_db_name), K(stmt.get_table_name()));
          } else if (OB_FAIL(apply_ai_columns(ctx, *ai_tbl_schema,
                                              ai_tbl_schema->get_table_id(), ai_db_name,
                                              stmt.get_ai_column_defs()))) {
            SQL_ENG_LOG(WARN, "fail to apply ai column metadata on alter table", K(ret));
          }
        }
      }
    }
    char table_info_buffer[256];
    snprintf(table_info_buffer, sizeof(table_info_buffer), "table_id:%ld, hidden_table_id:%ld", 
              alter_table_arg.table_id_, alter_table_arg.hidden_table_id_);

    SERVER_EVENT_ADD("ddl", "alter table execute finish",
      "ret", ret,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", res.task_id_,
      "table_info", table_info_buffer,
      "schema_version", res.schema_version_);
    SQL_ENG_LOG(INFO, "finish alter table execute.", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(first_stmt));
  }
  return ret;
}

int ObAlterTableExecutor::need_check_constraint_validity(obcall::ObAlterTableArg &alter_table_arg, bool &need_check)
{
  int ret = OB_SUCCESS;
  need_check = false;
  if (obcall::ObAlterTableArg::ADD_CONSTRAINT != alter_table_arg.alter_constraint_type_
      && obcall::ObAlterTableArg::ALTER_CONSTRAINT_STATE != alter_table_arg.alter_constraint_type_) {
  } else {
    ObTableSchema::const_constraint_iterator iter =
        alter_table_arg.alter_table_schema_.constraint_begin();
    for(; iter != alter_table_arg.alter_table_schema_.constraint_end() && OB_SUCC(ret) && !need_check; iter++) {
      if (obcall::ObAlterTableArg::ALTER_CONSTRAINT_STATE == alter_table_arg.alter_constraint_type_) {
        if ((*iter)->get_is_modify_validate_flag() && (*iter)->is_validated()) {
          need_check = true;
        }
      } else if (CONSTRAINT_TYPE_CHECK == (*iter)->get_constraint_type()) {
        if ((*iter)->is_validated()) {
          need_check = (*iter)->is_validated();
        }
      } else if (CONSTRAINT_TYPE_NOT_NULL == (*iter)->get_constraint_type()) {
        if (1 != (*iter)->get_column_cnt()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_INVALID_ID != *(*iter)->cst_col_begin()) {
          // alter table modify column not null.
          need_check = (*iter)->is_validated();
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret) && !need_check) {
        (*iter)->set_need_validate_data(false);
      }
    }
  }
  return ret;
}


int ObAlterTableExecutor::resolve_alter_column_partition_expr(
    const share::schema::ObColumnSchemaV2 &col_schema,
    const share::schema::ObTableSchema &table_schema,
    ObSchemaGetterGuard &schema_guard,
    ObSQLSessionInfo &session_info,
    common::ObIAllocator &allocator,
    const bool is_sub_part,
    ObRawExprResType &dst_res_type)
{
  int ret = OB_SUCCESS;
  ObRawExpr *part_expr = NULL;
  ObRawExprFactory expr_factory(allocator);
  ObSchemaChecker schema_checker;
  if (OB_FAIL(schema_checker.init(schema_guard))) {
  } else {
    ObResolverParams resolver_ctx;
    ObStmtFactory stmt_factory(allocator);
    TableItem table_item;
    resolver_ctx.allocator_ = &allocator;
    resolver_ctx.schema_checker_ = &schema_checker;
    resolver_ctx.session_info_ = &session_info;
    resolver_ctx.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
    resolver_ctx.expr_factory_ = &expr_factory;
    resolver_ctx.stmt_factory_ = &stmt_factory;
    resolver_ctx.query_ctx_ = stmt_factory.get_query_ctx();
    table_item.table_id_ = table_schema.get_table_id();
    table_item.ref_id_ = table_schema.get_table_id();
    table_item.type_ = TableItem::BASE_TABLE;
    // This is just to use the resolver to resolve the partition expr interface.
    // The resolver of any statement has this ability. The reason for using the delete
    // resolver is that the delete resolver is the simplest
    ObPartitionFuncType part_type;
    SMART_VAR (ObDeleteResolver, delete_resolver, resolver_ctx) {
      ObDeleteStmt *delete_stmt = delete_resolver.create_stmt<ObDeleteStmt>();
      CK (OB_NOT_NULL(delete_stmt));
      CK (OB_NOT_NULL(resolver_ctx.query_ctx_));
      OZ (delete_stmt->get_table_items().push_back(&table_item));
      OZ (delete_stmt->set_table_bit_index(table_schema.get_table_id()));
      if (!is_sub_part) {
        const ObString &part_str = table_schema.get_part_option().get_part_func_expr_str();
        part_type = table_schema.get_part_option().get_part_func_type();
        OZ (delete_resolver.resolve_partition_expr(table_item, table_schema,
        part_type, part_str, part_expr));
      } else {
        const ObString &part_str = table_schema.get_sub_part_option().get_part_func_expr_str();
        part_type = table_schema.get_sub_part_option().get_part_func_type();
        OZ (delete_resolver.resolve_partition_expr(table_item, table_schema,
        part_type, part_str, part_expr));
      }
      CK (OB_NOT_NULL(part_expr));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (part_expr->is_column_ref_expr()) {
    ObColumnRefRawExpr *column_ref = static_cast<ObColumnRefRawExpr*>(part_expr);
    if (column_ref->get_column_id() == col_schema.get_column_id()) {
      if (OB_FAIL(ObRawExprUtils::init_column_expr(col_schema, &session_info, *column_ref))) {
      } else if (CS_TYPE_INVALID == column_ref->get_collation_type()) {
        column_ref->set_collation_type(table_schema.get_collation_type());
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < part_expr->get_param_count(); ++i) {
      ObRawExpr *sub_expr = part_expr->get_param_expr(i);
      if (OB_ISNULL(sub_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (sub_expr->is_column_ref_expr()) {
        ObColumnRefRawExpr *column_ref = static_cast<ObColumnRefRawExpr*>(sub_expr);
        if (column_ref->get_column_id() == col_schema.get_column_id()) {
          if (OB_FAIL(ObRawExprUtils::init_column_expr(col_schema, &session_info, *column_ref))) {
          } else if (CS_TYPE_INVALID == column_ref->get_collation_type()) {
            column_ref->set_collation_type(table_schema.get_collation_type());
          }
        }
      }
    }
  }
  OZ (part_expr->formalize(&session_info));
  OX (dst_res_type = part_expr->get_result_type());
  return ret;
}

template<class T>
int ObAlterTableExecutor::calc_range_part_high_bound(
    const ObPartitionFuncType part_func_type,
    const ObString &col_name,
    const ObRawExprResType &dst_res_type,
    T &part,
    ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObExprCtx expr_ctx;
  ObAccuracy accuracy(dst_res_type.get_accuracy());
  const ObObjType fun_expr_type = dst_res_type.get_type();
  const ObCollationType fun_collation_type = dst_res_type.get_collation_type();
  ObObjType expected_obj_type = fun_expr_type;
  ObSEArray<ObObj, obcall::OB_DEFAULT_ARRAY_SIZE> range_partition_obj;
  if (OB_FAIL(ObSQLUtils::wrap_expr_ctx(stmt::T_ALTER_TABLE, ctx, ctx.get_allocator(), expr_ctx))) {
  } else {
    expr_ctx.cast_mode_ = CM_WARN_ON_FAIL; //always set to WARN_ON_FAIL to allow calculate
    const common::ObRowkey &row_key = part.get_high_bound_val();
    int64_t obj_cnt = row_key.get_obj_cnt();
    for (int64_t i = 0; OB_SUCC(ret) && i < obj_cnt; ++i) {
      const ObObj &src_obj = row_key.get_obj_ptr()[i];
      if (src_obj.is_max_value()) {
        if (OB_FAIL(range_partition_obj.push_back(src_obj))) {
        }
      } else {
        if (ob_is_integer_type(fun_expr_type)) {
          // Type promotion to int64 or uint64
          expected_obj_type = ob_is_int_tc(fun_expr_type) ? ObIntType : ObUInt64Type;
        }
        const ObObj *dst_obj = NULL;
        EXPR_DEFINE_CAST_CTX(expr_ctx, CM_NONE);
        cast_ctx.dest_collation_ = fun_collation_type;
        cast_ctx.res_accuracy_ = &accuracy;
        EXPR_CAST_OBJ_V2(expected_obj_type, src_obj, dst_obj);
        if (OB_SUCC(ret)) {
          if (OB_ISNULL(dst_obj)) {
            ret = OB_ERR_UNEXPECTED;
          } else if ((PARTITION_FUNC_TYPE_RANGE_COLUMNS == part_func_type
                    || PARTITION_FUNC_TYPE_LIST_COLUMNS == part_func_type)
                    && OB_FAIL(ObResolverUtils::check_partition_range_value_result_type(part_func_type,
                                                                                        dst_res_type,
                                                                                        col_name,
                                                                                        const_cast<ObObj&>(*dst_obj)))) {
          } else if (OB_FAIL(range_partition_obj.push_back(*dst_obj))) {
          }
        } else if (OB_ERR_UNEXPECTED != ret) {
          ret = OB_ERR_WRONG_TYPE_COLUMN_VALUE_ERROR;
          LOG_WARN("failed to cast obj", K(expected_obj_type), K(fun_expr_type), K(src_obj));
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObRowkey high_rowkey(&range_partition_obj.at(0), range_partition_obj.count());
      part.reset_high_bound_val();
      if (OB_FAIL(part.set_high_bound_val(high_rowkey))) {
      }
    }
  }
  return ret;
}

int ObAlterTableExecutor::calc_range_values_exprs(
    const ObColumnSchemaV2 &col_schema,
    const share::schema::ObTableSchema &orig_table_schema,
    share::schema::ObTableSchema &new_table_schema,
    ObSchemaGetterGuard &schema_guard,
    ObSQLSessionInfo &session_info,
    common::ObIAllocator &allocator,
    ObExecContext &ctx,
    const bool is_subpart)
{
  int ret = OB_SUCCESS;
  ObRawExprResType dst_res_type;
  if (OB_FAIL(resolve_alter_column_partition_expr(col_schema, orig_table_schema, schema_guard,
              session_info, allocator, is_subpart, dst_res_type))) {
  } else if (is_subpart) {
    const int64_t part_num = new_table_schema.get_partition_num();
    const ObPartitionFuncType part_func_type = orig_table_schema.get_sub_part_option().get_part_func_type();
    ObPartition **part_array = new_table_schema.get_part_array();
    CK (OB_NOT_NULL(part_array));
    for (int64_t i = 0; i < part_num && OB_SUCC(ret); ++i) {
      ObPartition *part = part_array[i];
      CK (OB_NOT_NULL(part));
      CK (OB_NOT_NULL(part->get_subpart_array()));
      for (int64_t j = 0; OB_SUCC(ret) && j < part->get_subpartition_num(); j++) {
        ObSubPartition *sub_part = part->get_subpart_array()[j];
        CK (OB_NOT_NULL(sub_part));
        OZ (calc_range_part_high_bound(part_func_type,
                                       col_schema.get_column_name_str(),
                                       dst_res_type,
                                       *sub_part,
                                       ctx));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObPartitionExecutorUtils::check_increasing_range_value(part->get_subpart_array(),
                                                                           part->get_subpartition_num(),
                                                                           stmt::T_ALTER_TABLE))) {
        }
      }
    }
  } else {
    const int64_t part_num = new_table_schema.get_partition_num();
    const ObPartitionFuncType part_func_type = orig_table_schema.get_part_option().get_part_func_type();
    ObPartition **part_array = new_table_schema.get_part_array();
    CK (OB_NOT_NULL(part_array));
    for (int64_t i = 0; i < part_num && OB_SUCC(ret); ++i) {
      ObPartition *part = part_array[i];
      CK (OB_NOT_NULL(part));
      OZ (calc_range_part_high_bound(part_func_type,
                                     col_schema.get_column_name_str(),
                                     dst_res_type,
                                     *part,
                                     ctx));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObPartitionExecutorUtils::check_increasing_range_value(part_array,
                                                                         part_num,
                                                                         stmt::T_ALTER_TABLE))) {
      }
    }
  }
  return ret;
}

template<class T>
int ObAlterTableExecutor::calc_list_part_rows(
    const ObPartitionFuncType part_func_type,
    const ObString &col_name,
    const ObRawExprResType &dst_res_type,
    const T &orig_part,
    T &new_part,
    ObExecContext &ctx,
    common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObExprCtx expr_ctx;
  const ObObjType fun_expr_type = dst_res_type.get_type();
  const ObCollationType fun_collation_type = dst_res_type.get_collation_type();
  ObObjType expected_obj_type = fun_expr_type;
  ObSEArray<ObObj, obcall::OB_DEFAULT_ARRAY_SIZE> range_partition_obj;
  if (OB_FAIL(ObSQLUtils::wrap_expr_ctx(stmt::T_ALTER_TABLE, ctx, ctx.get_allocator(), expr_ctx))) {
  } else {
    new_part.reset_list_row_values();
    expr_ctx.cast_mode_ = CM_WARN_ON_FAIL; //always set to WARN_ON_FAIL to allow calculate
    const common::ObIArray<common::ObNewRow>& row_list = orig_part.get_list_row_values();
    for (int64_t i = 0; OB_SUCC(ret) && i < row_list.count(); i ++) {
      const common::ObNewRow &row = row_list.at(i);
      for (int64_t j = 0; OB_SUCC(ret) && j < row.get_count(); ++j) {
        const ObObj &src_obj = row.get_cell(j);
        if (src_obj.is_max_value()) {
          if (OB_FAIL(range_partition_obj.push_back(src_obj))) {
          }
        } else {
          if (ob_is_integer_type(fun_expr_type)) {
            // Type promotion to int64 or uint64
            expected_obj_type = ob_is_int_tc(fun_expr_type) ? ObIntType : ObUInt64Type;
          }
          const ObObj *dst_obj = NULL;
          EXPR_DEFINE_CAST_CTX(expr_ctx, CM_NONE);
          cast_ctx.dest_collation_ = fun_collation_type;
          EXPR_CAST_OBJ_V2(expected_obj_type, src_obj, dst_obj);
          if (OB_SUCC(ret)) {
            if (OB_ISNULL(dst_obj)) {
              ret = OB_ERR_UNEXPECTED;
            } else if ((PARTITION_FUNC_TYPE_RANGE_COLUMNS == part_func_type
                      || PARTITION_FUNC_TYPE_LIST_COLUMNS == part_func_type)
                      && OB_FAIL(ObResolverUtils::check_partition_range_value_result_type(part_func_type,
                                                                                          dst_res_type,
                                                                                          col_name,
                                                                                          const_cast<ObObj&>(*dst_obj)))) {
            } else if (OB_FAIL(range_partition_obj.push_back(*dst_obj))) {
            }
          } else if (OB_ERR_UNEXPECTED != ret) {
            ret = OB_ERR_WRONG_TYPE_COLUMN_VALUE_ERROR;
            LOG_WARN("failed to cast obj", K(expected_obj_type), K(fun_expr_type), K(src_obj));
          }
        }
      }
      if (OB_SUCC(ret)) {
        ObNewRow row;
        ObObj* obj_array = (ObObj *)allocator.alloc(range_partition_obj.count() * sizeof(ObObj));
        CK (OB_NOT_NULL(obj_array));
        for (int64_t k = 0; OB_SUCC(ret) && k < range_partition_obj.count(); k ++) {
          new (obj_array + k) ObObj();
          obj_array[k] = (&range_partition_obj.at(0))[k];
        }
        OX (row.assign(obj_array, range_partition_obj.count()));
        OZ (new_part.add_list_row(row));
        OX (range_partition_obj.reuse());
      }
    }
  }
  return ret;
}

int ObAlterTableExecutor::calc_list_values_exprs(
    const ObColumnSchemaV2 &col_schema,
    const share::schema::ObTableSchema &orig_table_schema,
    share::schema::ObTableSchema &new_table_schema,
    ObSchemaGetterGuard &schema_guard,
    ObSQLSessionInfo &session_info,
    common::ObIAllocator &allocator,
    ObExecContext &ctx,
    const bool is_subpart)
{
  int ret = OB_SUCCESS;
  ObRawExprResType dst_res_type;
  if (OB_FAIL(resolve_alter_column_partition_expr(col_schema, orig_table_schema, schema_guard,
              session_info, allocator, is_subpart, dst_res_type))) {
  } else if (is_subpart) {
    const int64_t part_num = orig_table_schema.get_partition_num();
    const ObPartitionFuncType part_func_type = orig_table_schema.get_sub_part_option().get_part_func_type();
    ObPartition **orig_part_array = orig_table_schema.get_part_array();
    ObPartition **new_part_array = new_table_schema.get_part_array();
    CK (OB_NOT_NULL(orig_part_array) && OB_NOT_NULL(new_part_array));
    for (int64_t i = 0; i < part_num && OB_SUCC(ret); ++i) {
      ObPartition *orig_part = orig_part_array[i];
      ObPartition *new_part = new_part_array[i];
      CK (OB_NOT_NULL(orig_part) && OB_NOT_NULL(new_part));
      CK (OB_NOT_NULL(orig_part->get_subpart_array()));
      CK (OB_NOT_NULL(orig_part->get_subpart_array()));
      for (int64_t j = 0; OB_SUCC(ret) && j < orig_part->get_subpartition_num(); j++) {
        ObSubPartition *orig_sub_part = orig_part->get_subpart_array()[j];
        ObSubPartition *new_sub_part = new_part->get_subpart_array()[j];
        CK (OB_NOT_NULL(orig_sub_part) && OB_NOT_NULL(new_sub_part));
        OZ (calc_list_part_rows(part_func_type,
                                col_schema.get_column_name_str(),
                                dst_res_type,
                                *orig_sub_part,
                                *new_sub_part,
                                ctx,
                                allocator));
      }
    }
  } else {
    const int64_t part_num = orig_table_schema.get_partition_num();
    const ObPartitionFuncType part_func_type = orig_table_schema.get_part_option().get_part_func_type();
    ObPartition **orig_part_array = orig_table_schema.get_part_array();
    ObPartition **new_part_array = new_table_schema.get_part_array();
    CK (OB_NOT_NULL(orig_part_array) && OB_NOT_NULL(new_part_array));
    for (int64_t i = 0; i < part_num && OB_SUCC(ret); ++i) {
      ObPartition *orig_part = orig_part_array[i];
      ObPartition *new_part = new_part_array[i];
      CK (OB_NOT_NULL(orig_part) && OB_NOT_NULL(new_part));
      OZ (calc_list_part_rows(part_func_type,
                              col_schema.get_column_name_str(),
                              dst_res_type,
                              *orig_part,
                              *new_part,
                              ctx,
                              allocator));
    }
  }
  return ret;
}

int ObAlterTableExecutor::check_alter_part_key(ObExecContext &ctx,
                                               obcall::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (arg.is_alter_columns_) {
    bool is_contain_part_key = false;
    ObSchemaGetterGuard schema_guard;
    common::ObIAllocator &allocator = arg.allocator_;
    AlterTableSchema &table_schema = const_cast<AlterTableSchema &>(arg.alter_table_schema_);
    share::schema::ObTableSchema::const_column_iterator it_begin = table_schema.column_begin();
    share::schema::ObTableSchema::const_column_iterator it_end = table_schema.column_end();
    
    schema_guard.set_session_id(arg.session_id_);
    const ObString &origin_database_name = table_schema.get_origin_database_name();
    const ObString &origin_table_name = table_schema.get_origin_table_name();
    ObSQLSessionInfo *my_session = ctx.get_my_session();
    const share::schema::ObTableSchema *orig_table_schema = NULL;
    AlterColumnSchema *alter_column_schema = NULL;
    const ObColumnSchemaV2 *orig_column_schema = NULL;
    CK (!origin_database_name.empty() && !origin_table_name.empty());
    CK (OB_NOT_NULL(my_session));
    OZ (ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
    schema_guard));
    if (FAILEDx(schema_guard.get_table_schema(
                                              origin_database_name,
                                              origin_table_name,
                                              false/*is_index*/,
                                              orig_table_schema))) {
    } else if (OB_ISNULL(orig_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
    } else {
      OZ (table_schema.assign_partition_schema(*orig_table_schema));
      for(;OB_SUCC(ret) && it_begin != it_end; it_begin++) {
        if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*it_begin))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_DDL_CHANGE_COLUMN == alter_column_schema->alter_type_
          || OB_DDL_MODIFY_COLUMN == alter_column_schema->alter_type_) {
          bool is_same = false;
          const ObString &orig_column_name = alter_column_schema->get_origin_column_name();
          orig_column_schema = orig_table_schema->get_column_schema(orig_column_name);
          if (OB_ISNULL(orig_column_schema)) {
            ret = OB_ERR_CANT_DROP_FIELD_OR_KEY;
            LOG_USER_ERROR(OB_ERR_CANT_DROP_FIELD_OR_KEY, orig_column_name.length(), orig_column_name.ptr());
          } else if (OB_FAIL(ObTableSchema::check_is_exactly_same_type(
            *orig_column_schema, *alter_column_schema, is_same))) {
          } else if (!is_same && orig_column_schema->is_tbl_part_key_column()) {
            const bool is_part = orig_column_schema->is_part_key_column();
            const bool is_subpart = orig_column_schema->is_subpart_key_column();
            if (is_contain_part_key) {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "There are several mutually exclusive "
              "DDL in single statement");
            }
            // The column may be part key column, and may be subpart key column too.
            if (OB_SUCC(ret) && is_part) {
              if (orig_table_schema->is_list_part()) {
                OZ (calc_list_values_exprs(*alter_column_schema, *orig_table_schema, table_schema,
                  schema_guard, *my_session, allocator, ctx, false));
              } else if (orig_table_schema->is_range_part()) {
                OZ (calc_range_values_exprs(*alter_column_schema, *orig_table_schema, table_schema,
                  schema_guard, *my_session, allocator, ctx, false));
              }
            }
            if (OB_SUCC(ret) && is_subpart) {
              if (orig_table_schema->is_list_subpart()) {
                OZ (calc_list_values_exprs(*alter_column_schema, *orig_table_schema, table_schema,
                  schema_guard, *my_session, allocator, ctx, true));
              } else if (orig_table_schema->is_range_subpart()) {
                OZ (calc_range_values_exprs(*alter_column_schema, *orig_table_schema, table_schema,
                  schema_guard, *my_session, allocator, ctx, true));
              }
            }
            is_contain_part_key = true;
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterTableExecutor::check_alter_partition(ObExecContext &ctx,
                                                ObAlterTableStmt &stmt,
                                                const obcall::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  AlterTableSchema &table_schema = const_cast<AlterTableSchema &>(arg.alter_table_schema_);

  if (arg.is_alter_partitions_) {
    if (obcall::ObAlterTableArg::REPARTITION_TABLE == arg.alter_part_type_) {
      if (table_schema.is_range_part() || table_schema.is_list_part()
         || table_schema.is_range_subpart() || table_schema.is_list_subpart()) {
        if (OB_FAIL(ObPartitionExecutorUtils::calc_values_exprs_for_alter_table(ctx,
                                                                                table_schema,
                                                                                stmt))) {
        }
      }
    } else if (obcall::ObAlterTableArg::ADD_PARTITION == arg.alter_part_type_) {
      if (table_schema.is_range_part() || table_schema.is_list_part()) {
        if (OB_FAIL(ObPartitionExecutorUtils::calc_values_exprs_for_alter_table(ctx,
                                                                                table_schema,
                                                                                stmt))) {
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_WARN(OB_NOT_SUPPORTED, "add hash partition");
      }
    } else if (obcall::ObAlterTableArg::ADD_SUB_PARTITION == arg.alter_part_type_) {
      if (table_schema.is_range_subpart()) {
        if (OB_FAIL(ObPartitionExecutorUtils::set_individual_range_part_high_bound(
                    ctx, stmt::T_CREATE_TABLE, table_schema, stmt))) {
        }
      } else if (table_schema.is_list_subpart()) {
        if (OB_FAIL(ObPartitionExecutorUtils::set_individual_list_part_rows(
                    ctx, stmt, stmt::T_CREATE_TABLE, table_schema,
                    stmt.get_subpart_fun_exprs(),
                    stmt.get_individual_subpart_values_exprs()))) {
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_WARN(OB_NOT_SUPPORTED, "add hash subpartition");
      }
    } else if (obcall::ObAlterTableArg::DROP_PARTITION == arg.alter_part_type_
               || obcall::ObAlterTableArg::DROP_SUB_PARTITION == arg.alter_part_type_
               || obcall::ObAlterTableArg::RENAME_PARTITION == arg.alter_part_type_
               || obcall::ObAlterTableArg::RENAME_SUB_PARTITION == arg.alter_part_type_
               || obcall::ObAlterTableArg::TRUNCATE_PARTITION == arg.alter_part_type_
               || obcall::ObAlterTableArg::TRUNCATE_SUB_PARTITION == arg.alter_part_type_) {
      // do-nothing
    } else if ((obcall::ObAlterTableArg::EXCHANGE_PARTITION == arg.alter_part_type_)
        || (obcall::ObAlterTableArg::EXCHANGE_SUBPARTITION == arg.alter_part_type_)) {
      // do-nothing
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  return ret;
}

int ObAlterTableExecutor::set_index_arg_list(ObExecContext &ctx, ObAlterTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  obcall::ObAlterTableArg &alter_table_arg = const_cast<obcall::ObAlterTableArg &>(stmt.get_alter_table_arg());
  if (OB_ISNULL(my_session)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (stmt.get_index_partition_resolve_results().count()
      != stmt.get_index_arg_list().count()) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_index_arg_list().count(); i++) {
    HEAP_VAR(ObCreateIndexStmt, index_stmt) {
      obcall::ObCreateIndexArg &create_index_arg = *(stmt.get_index_arg_list().at(i));
      ObPartitionResolveResult &resolve_result = stmt.get_index_partition_resolve_results().at(i);
      index_stmt.get_part_fun_exprs() = resolve_result.get_part_fun_exprs();
      index_stmt.get_part_values_exprs() = resolve_result.get_part_values_exprs();
      index_stmt.get_subpart_fun_exprs() = resolve_result.get_subpart_fun_exprs();
      index_stmt.get_template_subpart_values_exprs() = resolve_result.get_template_subpart_values_exprs();
      index_stmt.get_individual_subpart_values_exprs() = resolve_result.get_individual_subpart_values_exprs();
      if (OB_FAIL(index_stmt.get_create_index_arg().assign(create_index_arg))) {
      } else if (OB_FAIL(ObPartitionExecutorUtils::calc_values_exprs(ctx, index_stmt))) {
      } else {
        create_index_arg.is_inner_ = my_session->is_inner();
        if (OB_FAIL(create_index_arg.assign(index_stmt.get_create_index_arg()))) {
        } else if (OB_FAIL(alter_table_arg.index_arg_list_.push_back(&create_index_arg))) {
        }
      }
    }
  }
  return ret;
}

ObCommentExecutor::ObCommentExecutor()
{
}

ObCommentExecutor::~ObCommentExecutor()
{
}

int ObCommentExecutor::execute(ObExecContext &ctx, ObAlterTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = nullptr;
  obcall::ObAlterTableArg &alter_table_arg = stmt.get_alter_table_arg();
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
  } else {
    ObSQLSessionInfo *my_session = nullptr;
    obcall::ObSetCommentArg set_comment_arg;
    obcall::ObParallelDDLRes set_comment_res;
    
    alter_table_arg.ddl_stmt_str_ = first_stmt;
    alter_table_arg.is_parallel_ = true;
    my_session = ctx.get_my_session();
    int64_t start_time = ObTimeUtility::current_time();
    ObTimeoutCtx tctx;
    const int64_t rpc_timeout = OB_DEFAULT_RPC_TIMEOUT;
    if (OB_ISNULL(my_session)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_INVALID_ID == alter_table_arg.session_id_
               && FALSE_IT(alter_table_arg.session_id_ = my_session->get_sessid_for_table())) {
      // should not in this field
    } else if (OB_FAIL(tctx.set_timeout(rpc_timeout))) {
    } else if (OB_FAIL(assign_alter_to_comment_(alter_table_arg, set_comment_arg))) {
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return ctx.root_command_service().set_comment(set_comment_arg, set_comment_res); }))) {
    } else {
      int64_t refresh_time = ObTimeUtility::current_time();
      if (OB_FAIL(ObDDLExecutorUtil::wait_local_schema_visible(
          tctx, my_session, set_comment_res.schema_version_))) {
      }
      int64_t end_time = ObTimeUtility::current_time();
      LOG_INFO("[parallel_comment_table]", KR(ret),
               "cost", end_time - start_time,
               "execute_time", refresh_time - start_time,
               "wait_schema", end_time - refresh_time,
               "table_name", set_comment_arg.table_name_);
    }
  }
  return ret;
}

int ObCommentExecutor::assign_alter_to_comment_(const obcall::ObAlterTableArg &alter_table_arg, obcall::ObSetCommentArg &set_comment_arg)
{
  int ret = OB_SUCCESS;
  const AlterTableSchema &alter_table_schema = alter_table_arg.alter_table_schema_;
  set_comment_arg.session_id_ = alter_table_arg.session_id_;
  set_comment_arg.database_name_ = alter_table_schema.get_origin_database_name();
  set_comment_arg.table_name_ = alter_table_schema.get_origin_table_name();
  if (OB_FAIL(set_comment_arg.ObDDLArg::assign(alter_table_arg))) {
  } else if (alter_table_arg.alter_table_schema_.alter_option_bitset_.has_member(obcall::ObAlterTableArg::COMMENT)) {
    set_comment_arg.op_type_ = obcall::ObSetCommentArg::COMMENT_TABLE;
    set_comment_arg.table_comment_ = alter_table_schema.get_comment();
  } else {
    set_comment_arg.op_type_ = obcall::ObSetCommentArg::COMMENT_COLUMN;
    ObTableSchema::const_column_iterator it_begin = alter_table_schema.column_begin();
    ObTableSchema::const_column_iterator it_end = alter_table_schema.column_end();
    for(; OB_SUCC(ret) && it_begin != it_end; it_begin++) {
      ObColumnSchemaV2 *column_schema = static_cast<ObColumnSchemaV2 *>(*it_begin);
      if (OB_ISNULL(column_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(set_comment_arg.column_name_list_.push_back(column_schema->get_column_name()))) {
      } else if (OB_FAIL(set_comment_arg.column_comment_list_.push_back(column_schema->get_comment()))) {
      }
    }
  }

  return ret;
}
/**
 *
 */

ObDropTableExecutor::ObDropTableExecutor()
{
}

ObDropTableExecutor::~ObDropTableExecutor()
{
}

int ObDropTableExecutor::execute(ObExecContext &ctx, ObDropTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  const obcall::ObDropTableArg &drop_table_arg = stmt.get_drop_table_arg();
  obcall::ObDropTableArg &tmp_arg = const_cast<obcall::ObDropTableArg&>(drop_table_arg);
  ObString first_stmt;
  ObSQLSessionInfo *my_session = NULL;
  int64_t foreign_key_checks = 0;
  
  const ObTableType table_type = drop_table_arg.table_type_;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    int64_t affected_rows = 0;
    tmp_arg.ddl_stmt_str_ = first_stmt;
    my_session = ctx.get_my_session();
    if (NULL == my_session) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_INVALID_ID == drop_table_arg.session_id_
               && FALSE_IT(tmp_arg.session_id_ = my_session->get_sessid_for_table())) {
      //impossible
    } else if (FALSE_IT(my_session->get_foreign_key_checks(foreign_key_checks))) {
    } else if (FALSE_IT(tmp_arg.foreign_key_checks_ = foreign_key_checks)) {
    } else {
      bool is_parallel_drop = false;
      if (!ObSchemaUtils::is_support_parallel_drop(table_type)) {
        is_parallel_drop = false;
      } else if (OB_FAIL(ObParallelDDLControlMode::is_parallel_ddl_enable(ObParallelDDLControlMode::DROP_TABLE, is_parallel_drop))) {
      }

      if (OB_SUCC(ret)) {
        if (is_parallel_drop) {
          int64_t start_time = ObTimeUtility::current_time();
          obcall::ObDropTableRes res;
          ObTimeoutCtx timeout_ctx;
          tmp_arg.is_parallel_ = true;
          if (OB_FAIL(timeout_ctx.set_timeout(OB_DEFAULT_RPC_TIMEOUT))) {
          } else if (OB_FAIL(ctx.root_command_service().parallel_drop_table(drop_table_arg, res))) {
          } else {
            int64_t refresh_time = ObTimeUtility::current_time();
            if (!res.do_nothing_ && OB_FAIL(ObDDLExecutorUtil::wait_local_schema_visible(
                                            timeout_ctx, my_session, res.schema_version_))) {
              LOG_ERROR("fail to wait for local schema visibility", KR(ret), K(res));
            }
            int64_t end_time = ObTimeUtility::current_time();
            LOG_INFO("[parallel_drop_table]", KR(ret),
                      "cost", end_time - start_time,
                      "execute_time", refresh_time - start_time,
                      "wait_schema", end_time - refresh_time);

            SERVER_EVENT_ADD("ddl", "drop table execute finish",
              "ret", ret,
              "trace_id", *ObCurTraceId::get_trace_id(),
              "rpc_dst", GCTX.self_addr(),
              "schema_version", res.schema_version_);
            SQL_ENG_LOG(INFO, "finish drop table execute.", KR(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
          }
        } else {
          obcall::ObDDLRes res;
          if (OB_FAIL(query::serialize_root_service_call(
                  [&]{ return ctx.root_command_service().drop_table(drop_table_arg, res); }))) {
          } else if (res.is_valid() && OB_FAIL(ObDDLExecutorUtil::wait_ddl_retry_task_finish(
              res.task_id_, *my_session, *ctx.get_query_runtime_environment(),
              ctx.local_command_service(), affected_rows))) {
          } else {
            //do nothing
          }

          SERVER_EVENT_ADD("ddl", "drop table execute finish",
          "ret", ret,
          "trace_id", *ObCurTraceId::get_trace_id(),
          "task_id", res.task_id_,
          "schema_id", res.schema_id_);
          SQL_ENG_LOG(INFO, "finish drop table execute.", KR(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
        }
      }
    }    
  }

  return ret;
}

/**
 *
 */
ObRenameTableExecutor::ObRenameTableExecutor()
{
}

ObRenameTableExecutor::~ObRenameTableExecutor()
{
}

int ObRenameTableExecutor::execute(ObExecContext &ctx, ObRenameTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  const obcall::ObRenameTableArg &rename_table_arg = stmt.get_rename_table_arg();
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(query::serialize_root_service_call(
                 [&]{ return ctx.root_command_service().rename_table(rename_table_arg); }))) {
  }
  return ret;
}

/**
 *
 */

ObTruncateTableExecutor::ObTruncateTableExecutor()
{
}

ObTruncateTableExecutor::~ObTruncateTableExecutor()
{
}

int ObTruncateTableExecutor::check_use_parallel_truncate(const obcall::ObTruncateTableArg &arg, bool &use_parallel_truncate)
{
  int ret = OB_SUCCESS;
  use_parallel_truncate = false;
  const ObTableSchema *table_schema = NULL;
  
  const ObString table_name = arg.table_name_;
  const ObString database_name = arg.database_name_;
  share::schema::ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
  } else if (FALSE_IT(schema_guard.set_session_id(arg.session_id_))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( database_name, table_name, false, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else {
    use_parallel_truncate = true;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (use_parallel_truncate
             && OB_FAIL(ObParallelDDLControlMode::is_parallel_ddl_enable(
                        ObParallelDDLControlMode::TRUNCATE_TABLE, use_parallel_truncate))) {
  }
  return ret;
}

int ObTruncateTableExecutor::execute(ObExecContext &ctx, ObTruncateTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  const obcall::ObTruncateTableArg &truncate_table_arg = stmt.get_truncate_table_arg();
  obcall::ObTruncateTableArg &tmp_arg = const_cast<obcall::ObTruncateTableArg&>(truncate_table_arg);
  ObString first_stmt;
  obcall::ObDDLRes res;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    tmp_arg.ddl_stmt_str_ = first_stmt;

    ObSqlExecutorCtx *task_exec_ctx = NULL;
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_ISNULL(my_session)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_INVALID_ID == truncate_table_arg.session_id_
               && FALSE_IT(tmp_arg.session_id_ = my_session->get_sessid_for_table())) {
      //impossible
    } else {
      int64_t foreign_key_checks = 0;
      my_session->get_foreign_key_checks(foreign_key_checks);
      tmp_arg.foreign_key_checks_ = foreign_key_checks;
      int64_t affected_rows = 0;
      bool use_parallel_truncate = false;
      
      if (OB_FAIL(check_use_parallel_truncate(truncate_table_arg, use_parallel_truncate))) {
      } else if (!use_parallel_truncate) {
        if (OB_FAIL(query::serialize_root_service_call(
                [&]{ return ctx.root_command_service().truncate_table(truncate_table_arg, res); }))) {
        } else if (res.is_valid()
          && OB_FAIL(ObDDLExecutorUtil::wait_ddl_retry_task_finish(
              res.task_id_, *my_session, *ctx.get_query_runtime_environment(),
              ctx.local_command_service(), affected_rows))) {
        }
      } else {
        // new parallel truncate
        ObTimeoutCtx timeout_ctx;
        tmp_arg.is_parallel_ = true;
        if (OB_FAIL(timeout_ctx.set_timeout(OB_DEFAULT_RPC_TIMEOUT))) {
        } else {
          int64_t start_time = ObTimeUtility::current_time();
          while (OB_SUCC(ret)) {
            DEBUG_SYNC(BEFORE_PARELLEL_TRUNCATE);
            if (OB_FAIL(query::serialize_root_service_call(
                    [&]{ return ctx.root_command_service().truncate_table_v2(truncate_table_arg, res); }))) {
              if ((OB_TRY_LOCK_ROW_CONFLICT == ret || OB_TIMEOUT == ret || OB_NOT_MASTER == ret
                    || OB_RS_NOT_MASTER == ret || OB_RS_SHUTDOWN == ret || OB_SERVER_RUNTIME_NOT_READY == ret) && timeout_ctx.get_timeout() > 0) {
                ob_usleep(1 * 1000 * 1000);
                // retry
                ret = OB_SUCCESS;
              }
            } else {
              // success
              break;
            }
          }
          int64_t step_time = ObTimeUtility::current_time();
          LOG_INFO("truncate_table_v2 finish trans", K(ret), "cost", step_time-start_time, "table_name", truncate_table_arg.table_name_, K(res));
          if (OB_FAIL(ret)) {
          } else if (!res.is_valid()) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(ObDDLExecutorUtil::wait_local_schema_visible(
                     timeout_ctx, my_session, res.task_id_))) {
          }
          int64_t end_time = ObTimeUtility::current_time();
          LOG_INFO("truncate_table_v2", KR(ret), "cost", end_time-start_time,
                                                "trans_cost", step_time - start_time,
                                                "wait_refresh", end_time - step_time,
                                                "table_name", truncate_table_arg.table_name_,
                                                K(res));
        }
      }
    }
    SERVER_EVENT_ADD("ddl", "truncate table execute finish",
      "ret", ret,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", res.task_id_,
      "table_info", truncate_table_arg.table_name_,
      "schema_id", res.schema_id_);
    SQL_ENG_LOG(INFO, "finish truncate table execute.", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
  }
  return ret;
}

ObCreateTableLikeExecutor::ObCreateTableLikeExecutor()
{
}

ObCreateTableLikeExecutor::~ObCreateTableLikeExecutor()
{
}

int ObCreateTableLikeExecutor::execute(ObExecContext &ctx, ObCreateTableLikeStmt &stmt)
{
  int ret = OB_SUCCESS;
  const obcall::ObCreateTableLikeArg &create_table_like_arg = stmt.get_create_table_like_arg();
  obcall::ObCreateTableLikeArg &tmp_arg = const_cast<obcall::ObCreateTableLikeArg&>(create_table_like_arg);
  ObString first_stmt;
  ObSQLSessionInfo *my_session = nullptr;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else if (OB_ISNULL(my_session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    tmp_arg.ddl_stmt_str_ = first_stmt;
    tmp_arg.session_id_ = my_session->get_sessid_for_table();
    ObSqlExecutorCtx *task_exec_ctx = NULL;
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return ctx.root_command_service().create_table_like(create_table_like_arg); }))) {
    }
  }
  return ret;
}

ObForkTableExecutor::ObForkTableExecutor()
{
}

ObForkTableExecutor::~ObForkTableExecutor()
{
}

int ObForkTableExecutor::execute(ObExecContext &ctx, ObForkTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  const obcall::ObForkTableArg &fork_table_arg = stmt.get_fork_table_arg();
  obcall::ObForkTableArg &tmp_arg = const_cast<obcall::ObForkTableArg&>(fork_table_arg);
  ObString first_stmt;
  obcall::ObDDLRes res;
  ObSQLSessionInfo *my_session = nullptr;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else if (OB_ISNULL(my_session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    uint64_t data_version = 0;
    tmp_arg.ddl_stmt_str_ = first_stmt;
    tmp_arg.session_id_ = my_session->get_sessid_for_table();
    ObSqlExecutorCtx *task_exec_ctx = NULL;
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return ctx.root_command_service().fork_table(fork_table_arg, res); }))) {
    } else {
      LOG_INFO("fork table executor finished", K(fork_table_arg));
    }
  }
  return ret;
}

int ObRecyclebinRestoreTableExecutor::execute(ObExecContext &ctx, ObRecyclebinRestoreTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  const obcall::ObRecyclebinRestoreTableArg &restore_table_arg = stmt.get_restore_table_arg();
  obcall::ObRecyclebinRestoreTableArg &tmp_arg = const_cast<obcall::ObRecyclebinRestoreTableArg&>(restore_table_arg);
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    tmp_arg.ddl_stmt_str_ = first_stmt;
    ObSqlExecutorCtx *task_exec_ctx = NULL;
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return ctx.root_command_service().restore_table_from_recyclebin(restore_table_arg); }))) {
    }
  }
  return ret;
}

int ObPurgeTableExecutor::execute(ObExecContext &ctx, ObPurgeTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  const obcall::ObPurgeTableArg &purge_table_arg = stmt.get_purge_table_arg();
  obcall::ObPurgeTableArg &tmp_arg = const_cast<obcall::ObPurgeTableArg&>(purge_table_arg);
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    tmp_arg.ddl_stmt_str_ = first_stmt;

    ObSqlExecutorCtx *task_exec_ctx = NULL;
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return ctx.root_command_service().purge_table(purge_table_arg); }))) {
    }
  }
  return ret;
}

int ObOptimizeTableExecutor::execute(ObExecContext &ctx, ObOptimizeTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  obcall::ObOptimizeTableArg &arg = stmt.get_optimize_table_arg();
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    arg.ddl_stmt_str_ = first_stmt;
    ObSqlExecutorCtx *task_exec_ctx = nullptr;
    if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(query::serialize_root_service_call(
                   [&]{ return ctx.root_command_service().optimize_table(arg); }))) {
    }
  }
  return ret;
}

int ObAlterTableExecutor::populate_based_schema_obj_info_(obcall::ObAlterTableArg &alter_table_arg) {
  int ret = OB_SUCCESS;
  const uint64_t table_id = alter_table_arg.alter_table_schema_.get_table_id();
  if (OB_INVALID_ID != table_id) {
    
    SMART_VAR(ObSchemaGetterGuard, guard) {
    const ObTableSchema *orig_table = nullptr;
    if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                guard))) {
    } else if (OB_FAIL(guard.get_table_schema( table_id, orig_table))) {
    } else if (OB_ISNULL(orig_table)) {
      ret = OB_TABLE_NOT_EXIST;
    } else {
      bool find = false;
      for (int i = 0; i < OB_SUCC(ret) && alter_table_arg.based_schema_object_infos_.count(); ++i) {
        ObBasedSchemaObjectInfo &based_schema_object_info =
                                alter_table_arg.based_schema_object_infos_.at(i);
        if (based_schema_object_info.schema_id_ == alter_table_arg.table_id_) {
          find = true;
          if (based_schema_object_info.schema_version_ == orig_table->get_schema_version()) {
            break;
          } else {
            ret = OB_ERR_PARALLEL_DDL_CONFLICT;
          }
        }
      }
      if (false == find) {
        if (OB_FAIL(alter_table_arg.based_schema_object_infos_.push_back(
                    ObBasedSchemaObjectInfo(table_id, TABLE_SCHEMA,
                                            orig_table->get_schema_version())))) {
        }
      }
    }
    } // end smart var
  }
  return ret;
}


// ---------------------------------------------------------------------------
// AI column task bookkeeping
//
// The transaction that changes a row is also the one that queues the work for
// it, so a rollback never leaves a task behind and the task is visible to the
// same transaction that made the change.  Because the hidden state column is
// not reachable from user SQL, the queue is maintained from the kernel: a row
// whose state is missing or PENDING and that has no task row is queued, and a
// task whose row disappeared is dropped.
// ---------------------------------------------------------------------------
namespace ai_column
{

static const int64_t AI_TABLE_CACHE_CAP = 2048;
static uint64_t g_ai_table_cache[AI_TABLE_CACHE_CAP];
static int64_t g_ai_table_count = 0;
static bool g_ai_table_loaded = false;

static bool cache_has(const uint64_t table_id)
{
  bool found = false;
  for (int64_t i = 0; !found && i < g_ai_table_count; ++i) {
    found = (g_ai_table_cache[i] == table_id);
  }
  return found;
}

int add_ai_column_table(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID != table_id && !cache_has(table_id)) {
    if (g_ai_table_count >= AI_TABLE_CACHE_CAP) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("too many tables with ai columns", K(ret), K(table_id));
    } else {
      g_ai_table_cache[g_ai_table_count++] = table_id;
    }
  }
  return ret;
}

bool table_has_ai_column(const uint64_t table_id)
{
  return cache_has(table_id);
}

static int load_ai_tables(common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(sql.append_fmt("SELECT table_id FROM %s.__all_ai_column GROUP BY table_id",
                                    OB_SYS_DATABASE_NAME))) {
    LOG_WARN("build ai column table scan failed", K(ret));
  } else {
    SMART_VAR(ObISQLClient::ReadResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(conn->execute_read(sql.ptr(), res))) {
        LOG_WARN("read ai column catalogue failed", K(ret));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        while (OB_SUCC(ret)) {
          ret = result->next();
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else if (OB_FAIL(ret)) {
            break;
          } else {
            int64_t table_id = 0;
            if (OB_FAIL(result->get_int(static_cast<int64_t>(0), table_id))) {
              LOG_WARN("read ai column table id failed", K(ret));
            } else if (OB_FAIL(add_ai_column_table(static_cast<uint64_t>(table_id)))) {
              LOG_WARN("cache ai column table failed", K(ret), K(table_id));
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    g_ai_table_loaded = true;
  }
  return ret;
}

// MD5(CONCAT_WS('|', `content`, `id`)): the digest of everything the AI result
// depends on. A row is up to date when the digest stored next to the result
// still matches the row, so a dependency change invalidates it without any
// per column change tracking.
static int build_input_digest(const ObString &deps,
                              const ObIArray<ObString> &pks,
                              ObSqlString &out)
{
  int ret = OB_SUCCESS;
  bool first = true;
  if (OB_FAIL(out.append("MD5(CONCAT_WS('|'"))) {
    LOG_WARN("build ai input digest failed", K(ret));
  }
  const char *p = deps.ptr();
  const char *end = p + deps.length();
  while (OB_SUCC(ret) && p < end) {
    const char *comma = p;
    while (comma < end && ',' != *comma) {
      ++comma;
    }
    ObString item(static_cast<int32_t>(comma - p), p);
    item = item.trim();
    if (!item.empty() && OB_FAIL(out.append_fmt(", `%.*s`", item.length(), item.ptr()))) {
      LOG_WARN("build ai input digest failed", K(ret));
    } else {
      first = false;
    }
    p = comma < end ? comma + 1 : end;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < pks.count(); ++i) {
    if (OB_FAIL(out.append_fmt(", `%.*s`", pks.at(i).length(), pks.at(i).ptr()))) {
      LOG_WARN("build ai input digest failed", K(ret));
    } else {
      first = false;
    }
  }
  if (OB_SUCC(ret) && first) {
    // a constant AI column still has to be invalidated when its row key changes
    ret = out.append(", ''");
  }
  if (OB_SUCC(ret) && OB_FAIL(out.append("))"))) {
    LOG_WARN("build ai input digest failed", K(ret));
  }
  return ret;
}

// CONCAT('`pk1` = ', b.`pk1`, ' AND `pk2` = ', b.`pk2`)
static int build_rowkey_predicate(const ObIArray<ObString> &pks,
                                  const char *alias,
                                  ObSqlString &out)
{
  int ret = OB_SUCCESS;
  if (pks.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(out.append_fmt("CONCAT('`%.*s` = ', %s.`%.*s`",
                                    pks.at(0).length(), pks.at(0).ptr(), alias,
                                    pks.at(0).length(), pks.at(0).ptr()))) {
    LOG_WARN("build ai rowkey predicate failed", K(ret));
  }
  for (int64_t i = 1; OB_SUCC(ret) && i < pks.count(); ++i) {
    if (OB_FAIL(out.append_fmt(", ' AND `%.*s` = ', %s.`%.*s`",
                               pks.at(i).length(), pks.at(i).ptr(), alias,
                               pks.at(i).length(), pks.at(i).ptr()))) {
      LOG_WARN("build ai rowkey predicate failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(out.append(")"))) {
    LOG_WARN("close ai rowkey predicate failed", K(ret));
  }
  return ret;
}


// Temporary diagnostic helper: logging is unreliable in this build, so probe
// results go to a file instead.
// True when the statement returns at least one row.
static int read_has_row(common::ObMySQLProxy &proxy, const ObSqlString &sql, bool &has_row)
{
  int ret = OB_SUCCESS;
  has_row = false;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    common::sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(proxy.read(res, sql.ptr(), static_cast<const ObSessionParam *>(NULL)))) {
      LOG_WARN("read ai column readiness failed", K(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    } else {
      has_row = true;
    }
  }
  return ret;
}

int has_unfinished_task(common::ObMySQLProxy &proxy,
                        const uint64_t table_id,
                        const uint64_t column_id,
                        bool &unfinished)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  unfinished = false;
  if (OB_FAIL(sql.assign_fmt("SELECT 1 FROM %s.__all_ai_column_task "
                             "WHERE table_id = %lu AND column_id = %lu LIMIT 1",
                             OB_SYS_DATABASE_NAME, table_id, column_id))) {
    LOG_WARN("build ai task readiness query failed", K(ret));
  } else if (OB_FAIL(read_has_row(proxy, sql, unfinished))) {
  } else if (unfinished) {
  } else if (OB_FAIL(sql.assign_fmt("SELECT 1 FROM %s.__all_ai_column_failed "
                                    "WHERE table_id = %lu AND column_id = %lu LIMIT 1",
                                    OB_SYS_DATABASE_NAME, table_id, column_id))) {
    LOG_WARN("build ai failure readiness query failed", K(ret));
  } else if (OB_FAIL(read_has_row(proxy, sql, unfinished))) {
  }
  return ret;
}

int maintain_tasks(common::sqlclient::ObISQLConnection *conn,
                   common::ObMySQLProxy *proxy,
                   ObIAllocator &alloc,
                   const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  UNUSED(proxy);
  ObString db_name;
  ObString table_name;
  ObSEArray<ObString, 4> pks;
  struct AiColumnDef {
    AiColumnDef() : column_id_(OB_INVALID_ID) {}
    uint64_t column_id_;
    ObString name_;          // result column
    ObString state_name_;    // hidden state column
    ObString request_name_;  // hidden request column, holds the input digest
    ObString deps_;          // comma separated dependency columns
  };
  ObSEArray<AiColumnDef, 8> defs;
  if (OB_ISNULL(conn) || OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!g_ai_table_loaded && OB_FAIL(load_ai_tables(conn))) {
    LOG_WARN("load ai column tables failed", K(ret));
  } else if (!cache_has(table_id)) {
    // table has no AI column, nothing to maintain
  } else {
    ObSqlString sql;
    // 1. names of the table
    if (OB_FAIL(sql.append_fmt(
          "SELECT d.database_name, t.table_name FROM %s.__all_table t "
          "JOIN %s.__all_database d ON d.database_id = t.database_id WHERE t.table_id = %lu",
          OB_SYS_DATABASE_NAME, OB_SYS_DATABASE_NAME, table_id))) {
      LOG_WARN("build ai table name query failed", K(ret));
    } else {
      SMART_VAR(ObISQLClient::ReadResult, res) {
        common::sqlclient::ObMySQLResult *result = NULL;
        if (OB_FAIL(conn->execute_read(sql.ptr(), res))) {
          LOG_WARN("read ai table name failed", K(ret), K(sql));
        } else if (OB_ISNULL(result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(result->next())) {
          LOG_WARN("ai column table lookup found no row", K(ret), K(table_id));
        } else {
          ObString d;
          ObString t;
          if (OB_FAIL(result->get_varchar(static_cast<int64_t>(0), d))
              || OB_FAIL(result->get_varchar(static_cast<int64_t>(1), t))) {
            LOG_WARN("read ai table name value failed", K(ret));
          } else if (OB_FAIL(ob_write_string(alloc, d, db_name))
                     || OB_FAIL(ob_write_string(alloc, t, table_name))) {
            LOG_WARN("copy ai table name failed", K(ret));
          }
        }
      }
    }
    // 2. rowkey columns in order
    if (OB_SUCC(ret) && OB_FAIL(sql.assign_fmt(
          "SELECT column_name FROM %s.__all_column WHERE table_id = %lu "
          "AND rowkey_position > 0 ORDER BY rowkey_position",
          OB_SYS_DATABASE_NAME, table_id))) {
      LOG_WARN("build ai rowkey query failed", K(ret));
    } else if (OB_SUCC(ret)) {
      SMART_VAR(ObISQLClient::ReadResult, res) {
        common::sqlclient::ObMySQLResult *result = NULL;
        if (OB_FAIL(conn->execute_read(sql.ptr(), res))) {
          LOG_WARN("read ai rowkey columns failed", K(ret), K(sql));
        } else if (OB_ISNULL(result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          while (OB_SUCC(ret)) {
            ret = result->next();
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else if (OB_FAIL(ret)) {
              break;
            } else {
              ObString name;
              ObString copied;
              if (OB_FAIL(result->get_varchar(static_cast<int64_t>(0), name))) {
                LOG_WARN("read ai rowkey name failed", K(ret));
              } else if (OB_FAIL(ob_write_string(alloc, name, copied))) {
                LOG_WARN("copy ai rowkey name failed", K(ret));
              } else if (OB_FAIL(pks.push_back(copied))) {
                LOG_WARN("push ai rowkey name failed", K(ret));
              }
            }
          }
        }
      }
    }
    // 3. AI columns of the table
    if (OB_SUCC(ret) && OB_FAIL(sql.assign_fmt(
          "SELECT a.column_id, c.column_name, s.column_name, ifnull(r.column_name, ''), "
          "ifnull(a.depend_columns, '') FROM %s.__all_ai_column a "
          "JOIN %s.__all_column c ON c.table_id = a.table_id AND c.column_id = a.column_id "
          "JOIN %s.__all_column s ON s.table_id = a.table_id AND s.column_id = a.state_column_id "
          "LEFT JOIN %s.__all_column r ON r.table_id = a.table_id "
          "AND r.column_id = a.request_column_id WHERE a.table_id = %lu",
          OB_SYS_DATABASE_NAME, OB_SYS_DATABASE_NAME, OB_SYS_DATABASE_NAME,
          OB_SYS_DATABASE_NAME, table_id))) {
      LOG_WARN("build ai column query failed", K(ret));
    } else if (OB_SUCC(ret)) {
      SMART_VAR(ObISQLClient::ReadResult, res) {
        common::sqlclient::ObMySQLResult *result = NULL;
        if (OB_FAIL(conn->execute_read(sql.ptr(), res))) {
          LOG_WARN("read ai columns failed", K(ret), K(sql));
        } else if (OB_ISNULL(result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          while (OB_SUCC(ret)) {
            ret = result->next();
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else if (OB_FAIL(ret)) {
              break;
            } else {
              AiColumnDef def;
              int64_t column_id = 0;
              ObString name;
              ObString state_name;
              ObString request_name;
              ObString deps;
              if (OB_FAIL(result->get_int(static_cast<int64_t>(0), column_id))
                  || OB_FAIL(result->get_varchar(static_cast<int64_t>(1), name))
                  || OB_FAIL(result->get_varchar(static_cast<int64_t>(2), state_name))
                  || OB_FAIL(result->get_varchar(static_cast<int64_t>(3), request_name))
                  || OB_FAIL(result->get_varchar(static_cast<int64_t>(4), deps))) {
                LOG_WARN("read ai column row failed", K(ret));
              } else if (FALSE_IT(def.column_id_ = static_cast<uint64_t>(column_id))) {
              } else if (OB_FAIL(ob_write_string(alloc, name, def.name_))
                         || OB_FAIL(ob_write_string(alloc, state_name, def.state_name_))
                         || OB_FAIL(ob_write_string(alloc, request_name, def.request_name_))
                         || OB_FAIL(ob_write_string(alloc, deps, def.deps_))) {
                LOG_WARN("copy ai column name failed", K(ret));
              } else if (OB_FAIL(defs.push_back(def))) {
                LOG_WARN("push ai column failed", K(ret));
              }
            }
          }
        }
      }
    }
    // 4. queue the pending rows and drop the tasks of rows that are gone.
    //    All AI columns of a row share one request id so a reader can see that
    //    the columns of the same row change belong to one request group.
    ObSqlString shared_deps;
    ObSqlString shared_request;
    for (int64_t i = 0; OB_SUCC(ret) && i < defs.count(); ++i) {
      if (0 == i) {
        ret = shared_deps.append(defs.at(i).deps_);
      } else {
        ret = shared_deps.append_fmt(",%.*s", defs.at(i).deps_.length(),
                                     defs.at(i).deps_.ptr());
      }
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("build ai shared dependency list failed", K(ret));
    } else if (OB_FAIL(build_input_digest(
                 ObString(shared_deps.length(), shared_deps.ptr()), pks, shared_request))) {
      LOG_WARN("build ai request id failed", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < defs.count(); ++i) {
      const AiColumnDef &def = defs.at(i);
      ObSqlString pred;
      ObSqlString stmt;
      ObSqlString digest;
      if (OB_FAIL(build_rowkey_predicate(pks, "b", pred))) {
      } else if (OB_FAIL(build_input_digest(def.deps_, pks, digest))) {
      } else if (def.request_name_.empty()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai hidden request column is missing", K(ret), K(def.name_));
      } else if (OB_FAIL(stmt.assign_fmt(
            "UPDATE `%.*s`.`%.*s` SET `%.*s` = NULL, `%.*s` = 'PENDING', "
            "`%.*s` = %s WHERE NOT (`%.*s` <=> %s)",
            db_name.length(), db_name.ptr(), table_name.length(), table_name.ptr(),
            def.name_.length(), def.name_.ptr(),
            def.state_name_.length(), def.state_name_.ptr(),
            def.request_name_.length(), def.request_name_.ptr(), digest.ptr(),
            def.request_name_.length(), def.request_name_.ptr(), digest.ptr()))) {
        LOG_WARN("build ai invalidate sql failed", K(ret));
      } else {
        int64_t affected = 0;
        if (OB_FAIL(conn->execute_write(stmt.ptr(), affected))) {
          LOG_WARN("invalidate ai column failed", K(ret), K(stmt));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(stmt.assign_fmt(
            "INSERT INTO %s.__all_ai_column_task (table_id, column_id, rowkey_predicate, "
            "request_id, state, retry_count, eligible_ts) "
            "SELECT %lu, %lu, %s, %s, 1, 0, NOW(6) FROM `%.*s`.`%.*s` b "
            "WHERE (b.`%.*s` IS NULL OR b.`%.*s` = 'PENDING') "
            "AND NOT EXISTS (SELECT 1 FROM %s.__all_ai_column_task k WHERE k.table_id = %lu "
            "AND k.column_id = %lu AND k.rowkey_predicate = %s)",
            OB_SYS_DATABASE_NAME, table_id, def.column_id_, pred.ptr(), shared_request.ptr(),
            db_name.length(), db_name.ptr(), table_name.length(), table_name.ptr(),
            def.state_name_.length(), def.state_name_.ptr(),
            def.state_name_.length(), def.state_name_.ptr(),
            OB_SYS_DATABASE_NAME, table_id, def.column_id_, pred.ptr()))) {
        LOG_WARN("build ai task queue sql failed", K(ret));
      } else {
        int64_t affected = 0;
        if (OB_FAIL(conn->execute_write(stmt.ptr(), affected))) {
          LOG_WARN("queue ai column task failed", K(ret), K(stmt));
        }
      }
      if (OB_SUCC(ret)) {
        // A row that is queued again is being recomputed, so its recorded
        // failure is over: strict reads must not keep rejecting it.
        if (OB_FAIL(stmt.assign_fmt(
              "DELETE FROM %s.__all_ai_column_failed WHERE table_id = %lu AND column_id = %lu "
              "AND rowkey_predicate IN (SELECT %s FROM `%.*s`.`%.*s` b "
              "WHERE (b.`%.*s` IS NULL OR b.`%.*s` = 'PENDING'))",
              OB_SYS_DATABASE_NAME, table_id, def.column_id_, pred.ptr(),
              db_name.length(), db_name.ptr(), table_name.length(), table_name.ptr(),
              def.state_name_.length(), def.state_name_.ptr(),
              def.state_name_.length(), def.state_name_.ptr()))) {
          LOG_WARN("build ai failure cleanup sql failed", K(ret));
        } else {
          int64_t affected = 0;
          if (OB_FAIL(conn->execute_write(stmt.ptr(), affected))) {
            LOG_WARN("clean ai failure records failed", K(ret), K(stmt));
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(stmt.assign_fmt(
              "DELETE FROM %s.__all_ai_column_task WHERE table_id = %lu AND column_id = %lu "
              "AND rowkey_predicate NOT IN (SELECT %s FROM `%.*s`.`%.*s` b)",
              OB_SYS_DATABASE_NAME, table_id, def.column_id_, pred.ptr(),
              db_name.length(), db_name.ptr(), table_name.length(), table_name.ptr()))) {
          LOG_WARN("build ai task cleanup sql failed", K(ret));
        } else {
          int64_t affected = 0;
          if (OB_FAIL(conn->execute_write(stmt.ptr(), affected))) {
            LOG_WARN("clean stale ai tasks failed", K(ret), K(stmt));
          }
        }
      }
    }
  }
  return ret;
}

}  // namespace ai_column

} //end namespace sql
} //end namespace oceanbase
