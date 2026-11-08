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

#include "sql/engine/dml/ob_ai_column_dml.h"

#include <stdio.h>
#include <string.h>

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/string/ob_sql_string.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/utility.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_sql_context.h"
#include "sql/resolver/ddl/ob_table_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{

using namespace common;
using namespace common::sqlclient;
using namespace share::schema;

namespace
{

int load_ai_column_defs(ObTableModifyOp &op,
                        const share::schema::ObTableSchema &table_schema,
                        common::ObIAllocator &alloc,
                        common::ObIArray<ObAiColumnDef> &defs)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObMySQLProxy *sql_proxy = op.get_exec_ctx().get_sql_proxy();
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(sql.assign_fmt(
               "SELECT column_id, column_name, state_column_id, request_column_id, "
               "result_spec_column_id, max_retries, normalized_expr, dep_column_names, result_type "
               "FROM oceanbase.%s WHERE table_id = %lu",
               OB_ALL_AI_COLUMN_TNAME, table_schema.get_table_id()))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(sql_proxy->read(res, sql.ptr()))) {
      } else if (NULL == (result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        while (OB_SUCC(ret)) {
          ObAiColumnDef def;
          int64_t column_id = 0;
          int64_t state_id = 0;
          int64_t request_id = 0;
          int64_t spec_id = 0;
          ObString column_name;
          ObString normalized;
          ObString dep_names;
          if (OB_FAIL(result->next())) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            }
          } else if (OB_FAIL(result->get_int("column_id", column_id))
                     || OB_FAIL(result->get_varchar("column_name", column_name))
                     || OB_FAIL(result->get_int("state_column_id", state_id))
                     || OB_FAIL(result->get_int("request_column_id", request_id))
                     || OB_FAIL(result->get_int("result_spec_column_id", spec_id))
                     || OB_FAIL(result->get_int("max_retries", def.max_retries_))
                     || OB_FAIL(result->get_varchar("normalized_expr", normalized))
                     || OB_FAIL(result->get_varchar("dep_column_names", dep_names))
                     || OB_FAIL(result->get_int("result_type", def.result_type_))) {
          } else if (OB_FAIL(ob_write_string(alloc, column_name, def.column_name_))
                     || OB_FAIL(ob_write_string(alloc, normalized, def.normalized_expr_))
                     || OB_FAIL(ob_write_string(alloc, dep_names, def.dep_column_names_))) {
          } else {
            def.result_column_id_ = static_cast<uint64_t>(column_id);
            def.state_column_id_ = static_cast<uint64_t>(state_id);
            def.request_column_id_ = static_cast<uint64_t>(request_id);
            def.result_spec_column_id_ = static_cast<uint64_t>(spec_id);
            const ObColumnSchemaV2 *state_column =
                table_schema.get_column_schema(def.state_column_id_);
            const ObColumnSchemaV2 *request_column =
                table_schema.get_column_schema(def.request_column_id_);
            const ObColumnSchemaV2 *spec_column =
                table_schema.get_column_schema(def.result_spec_column_id_);
            if (NULL == state_column || NULL == request_column || NULL == spec_column) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(ob_write_string(alloc, state_column->get_column_name_str(),
                                               def.state_column_name_))
                       || OB_FAIL(ob_write_string(alloc, request_column->get_column_name_str(),
                                                  def.request_column_name_))
                       || OB_FAIL(ob_write_string(alloc, spec_column->get_column_name_str(),
                                                  def.result_spec_column_name_))) {
            } else if (OB_FAIL(defs.push_back(def))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int find_column_index(const UIntFixedArray &column_ids, const uint64_t column_id, int64_t &index)
{
  int ret = OB_SUCCESS;
  index = -1;
  for (int64_t i = 0; -1 == index && i < column_ids.count(); ++i) {
    if (column_ids.at(i) == column_id) {
      index = i;
    }
  }
  if (-1 == index) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int build_rowkey_predicate(ObTableModifyOp &op,
                           const ObDMLBaseCtDef &ctdef,
                           const ExprFixedArray &row,
                           const ObTableSchema &table_schema,
                           common::ObSqlString &predicate)
{
  int ret = OB_SUCCESS;
  ObEvalCtx &eval_ctx = op.get_eval_ctx();
  ObIAllocator &alloc = op.get_exec_ctx().get_allocator();
  const ObObjPrintParams &print_params = op.get_obj_print_params();
  const ObRowkeyInfo &rowkey_info = table_schema.get_rowkey_info();
  for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_info.get_size(); ++i) {
    uint64_t column_id = OB_INVALID_ID;
    int64_t index = -1;
    ObDatum *datum = NULL;
    ObObj obj;
    char *literal = static_cast<char *>(alloc.alloc(256));
    int64_t literal_len = 256;
    int64_t pos = 0;
    const ObColumnSchemaV2 *column = NULL;
    if (OB_FAIL(rowkey_info.get_column_id(i, column_id))) {
    } else if (OB_FAIL(find_column_index(ctdef.column_ids_, column_id, index))) {
      index = i;
      ret = OB_SUCCESS;
    } else if (OB_ISNULL(column = table_schema.get_column_schema(column_id))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (index >= row.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(row.at(index)->eval(eval_ctx, datum))) {
    } else if (OB_ISNULL(datum) || datum->is_null()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datum->to_obj(obj, row.at(index)->obj_meta_))) {
    } else if (OB_ISNULL(literal)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(obj.print_sql_literal(literal, literal_len, pos, alloc, print_params))) {
    } else if (i > 0 && OB_FAIL(predicate.append(" and "))) {
    } else if (OB_FAIL(predicate.append_fmt("`%.*s` = %.*s",
                                            column->get_column_name_str().length(),
                                            column->get_column_name_str().ptr(),
                                            static_cast<int32_t>(pos), literal))) {
    }
  }
  return ret;
}

int make_request_id(char *buf, const int64_t buf_len)
{
  static uint64_t seq = 0;
  const uint64_t id = ATOMIC_AAF(&seq, 1);
  return snprintf(buf, static_cast<size_t>(buf_len), "%lx_%lx",
                  static_cast<uint64_t>(ObTimeUtility::current_time()), id);
}

int column_id_of_name(const ObTableSchema &table_schema, const ObString &name, uint64_t &column_id)
{
  int ret = OB_SUCCESS;
  const ObColumnSchemaV2 *column = table_schema.get_column_schema(name);
  if (NULL == column) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    column_id = column->get_column_id();
  }
  return ret;
}

int update_needs_maintenance(ObTableModifyOp &op,
                             const ObUpdCtDef &upd_ctdef,
                             const ObTableSchema &table_schema,
                             const common::ObIArray<ObAiColumnDef> &defs,
                             bool &need)
{
  int ret = OB_SUCCESS;
  need = false;
  if (upd_ctdef.old_row_.count() == 0
      || upd_ctdef.old_row_.count() != upd_ctdef.new_row_.count()) {
    need = true;
  }
  const ObRowkeyInfo &rowkey_info = table_schema.get_rowkey_info();
  for (int64_t i = 0; OB_SUCC(ret) && !need && i < rowkey_info.get_size(); ++i) {
    uint64_t column_id = OB_INVALID_ID;
    int64_t index = -1;
    ObDatum *old_datum = NULL;
    ObDatum *new_datum = NULL;
    if (OB_FAIL(rowkey_info.get_column_id(i, column_id))) {
    } else if (OB_FAIL(find_column_index(upd_ctdef.column_ids_, column_id, index))) {
      ret = OB_SUCCESS;
      need = true;
    } else if (OB_FAIL(upd_ctdef.old_row_.at(index)->eval(op.get_eval_ctx(), old_datum))
               || OB_FAIL(upd_ctdef.new_row_.at(index)->eval(op.get_eval_ctx(), new_datum))) {
    } else if (NULL == old_datum || NULL == new_datum
               || !ObDatum::binary_equal(*old_datum, *new_datum)) {
      need = true;
    }
  }
  // A dependency column changed when its value in the old row differs from the
  // value in the new row. Comparing values is more robust than inspecting the
  // assign list, which differs between the single-row and batch DML paths.
  for (int64_t k = 0; OB_SUCC(ret) && !need && k < defs.count(); ++k) {
    const ObString &deps = defs.at(k).dep_column_names_;
    int32_t start = 0;
    for (int32_t p = 0; OB_SUCC(ret) && !need && p <= deps.length(); ++p) {
      if (p == deps.length() || ',' == deps.ptr()[p]) {
        if (p > start) {
          const ObString dep_name(p - start, deps.ptr() + start);
          uint64_t dep_id = OB_INVALID_ID;
          int64_t index = -1;
          ObDatum *old_datum = NULL;
          ObDatum *new_datum = NULL;
          if (OB_SUCCESS != column_id_of_name(table_schema, dep_name, dep_id)) {
            // an unresolved name (e.g. a function name) is harmless
          } else if (OB_SUCCESS != find_column_index(upd_ctdef.column_ids_, dep_id, index)) {
            need = true;
          } else if (OB_FAIL(upd_ctdef.old_row_.at(index)->eval(op.get_eval_ctx(), old_datum))
                     || OB_FAIL(upd_ctdef.new_row_.at(index)->eval(op.get_eval_ctx(), new_datum))) {
          } else if (NULL == old_datum || NULL == new_datum
                     || !ObDatum::binary_equal(*old_datum, *new_datum)) {
            need = true;
          }
        }
        start = p + 1;
      }
    }
  }
  return ret;
}

} // anonymous namespace

bool table_has_ai_column(const ObTableSchema &table_schema)
{
  bool found = false;
  for (int64_t i = 0; !found && i < table_schema.get_column_count(); ++i) {
    const ObColumnSchemaV2 *column = table_schema.get_column_schema_by_idx(i);
    if (NULL != column && column->has_column_flag(AI_COLUMN_RESULT_FLAG)) {
      found = true;
    }
  }
  return found;
}

int ObAiColumnDml::process_insert(ObTableModifyOp &op, const ObInsCtDef &ctdef)
{
  return process(op, ctdef, AI_DML_INSERT);
}

int ObAiColumnDml::process_update(ObTableModifyOp &op, const ObUpdCtDef &ctdef)
{
  return process(op, ctdef, AI_DML_UPDATE);
}

int ObAiColumnDml::process_delete(ObTableModifyOp &op, const ObDelCtDef &ctdef)
{
  return process(op, ctdef, AI_DML_DELETE);
}

int ObAiColumnDml::process(ObTableModifyOp &op,
                           const ObDMLBaseCtDef &ctdef,
                           const DmlEvent event)
{
  int ret = OB_SUCCESS;
  ObExecContext &exec_ctx = op.get_exec_ctx();
  ObSQLSessionInfo *session = exec_ctx.get_my_session();
  ObSqlCtx *sql_ctx = exec_ctx.get_sql_ctx();
  ObSchemaGetterGuard *schema_guard = NULL;
  const ObTableSchema *table_schema = NULL;
  const ObDatabaseSchema *db_schema = NULL;
  ObIAllocator &alloc = exec_ctx.get_allocator();
  ObSEArray<ObAiColumnDef, 4> defs;
  const uint64_t cand_ids[2] = { ctdef.das_base_ctdef_.index_tid_,
                                 ctdef.das_base_ctdef_.table_id_ };
  bool skip = false;
  if (OB_ISNULL(session) || OB_ISNULL(sql_ctx)
      || OB_ISNULL(schema_guard = sql_ctx->schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (session->is_in_ai_column_maintenance()) {
    skip = true;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < 2; ++i) {
      table_schema = NULL;
      if (OB_FAIL(schema_guard->get_table_schema(cand_ids[i], table_schema))) {
      } else if (NULL != table_schema) {
        break;
      }
    }
    if (OB_SUCC(ret) && NULL == table_schema) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  if (OB_FAIL(ret) || skip) {
  } else if (!table_has_ai_column(*table_schema)) {
    // table without AI columns
  } else if (OB_FAIL(schema_guard->get_database_schema(table_schema->get_database_id(),
                                                        db_schema))) {
  } else if (OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(load_ai_column_defs(op, *table_schema, alloc, defs))) {
  } else if (defs.empty()) {
    // metadata is missing
  } else if (AI_DML_UPDATE == event) {
    const ObUpdCtDef &upd_ctdef = static_cast<const ObUpdCtDef &>(ctdef);
    bool need = false;
    if (OB_FAIL(update_needs_maintenance(op, upd_ctdef, *table_schema, defs, need))) {
    } else if (!need) {
      skip = true;
    }
  }

  if (OB_FAIL(ret) || skip) {
  } else {
    common::ObSqlString predicate;
    common::ObSqlString update_sql;
    common::ObSqlString task_sql;
    common::ObSqlString clean_sql;
    char request_id[64];
    make_request_id(request_id, sizeof(request_id));
    const int32_t request_id_len = static_cast<int32_t>(strlen(request_id));
    const ObString db_name = db_schema->get_database_name_str();
    const ObString &table_name = table_schema->get_table_name_str();
    const ExprFixedArray &rowkey_row = (AI_DML_DELETE == event)
                                       ? ctdef.old_row_ : ctdef.new_row_;
    if (OB_FAIL(build_rowkey_predicate(op, ctdef, rowkey_row, *table_schema, predicate))) {
    } else if (AI_DML_DELETE == event) {
      if (OB_FAIL(clean_sql.append_fmt(
                    "DELETE FROM oceanbase.%s WHERE table_id = %lu AND rowkey_predicate = ",
                    OB_ALL_AI_COLUMN_TASK_TNAME, table_schema->get_table_id()))) {
      } else if (OB_FAIL(sql_append_hex_escape_str(predicate.string(), clean_sql))) {
      }
    } else {
      if (OB_FAIL(update_sql.append_fmt("UPDATE `%.*s`.`%.*s` SET ",
                                        db_name.length(), db_name.ptr(),
                                        table_name.length(), table_name.ptr()))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < defs.count(); ++i) {
        const ObAiColumnDef &def = defs.at(i);
        if (AI_DML_UPDATE == event
            && OB_FAIL(update_sql.append_fmt("`%.*s` = NULL, ",
                                             def.column_name_.length(), def.column_name_.ptr()))) {
        } else if (OB_FAIL(update_sql.append_fmt(
                     "`%.*s` = 1, `%.*s` = ",
                     def.state_column_name_.length(), def.state_column_name_.ptr(),
                     def.request_column_name_.length(), def.request_column_name_.ptr()))) {
        } else if (OB_FAIL(sql_append_hex_escape_str(
                     ObString(request_id_len, request_id), update_sql))) {
        } else if (OB_FAIL(update_sql.append_fmt(", `%.*s` = '0', ",
                                                  def.result_spec_column_name_.length(),
                                                  def.result_spec_column_name_.ptr()))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(update_sql.set_length(update_sql.length() - 2))) {
        } else if (OB_FAIL(update_sql.append_fmt(" WHERE %.*s",
                                                  predicate.string().length(),
                                                  predicate.string().ptr()))) {
        }
      }
      if (OB_SUCC(ret)
          && OB_FAIL(task_sql.append_fmt(
               "INSERT INTO oceanbase.%s (table_id, column_id, request_id, rowkey_predicate, "
               "eligible_ts, state, retry_count) VALUES ",
               OB_ALL_AI_COLUMN_TASK_TNAME))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < defs.count(); ++i) {
        const ObAiColumnDef &def = defs.at(i);
        if (OB_FAIL(task_sql.append_fmt("%s(%lu, %lu, ",
                                       0 == i ? "" : ", ",
                                       table_schema->get_table_id(),
                                       def.result_column_id_))) {
        } else if (OB_FAIL(sql_append_hex_escape_str(
                     ObString(request_id_len, request_id), task_sql))) {
        } else if (OB_FAIL(task_sql.append(", "))) {
        } else if (OB_FAIL(sql_append_hex_escape_str(predicate.string(), task_sql))) {
        } else if (OB_FAIL(task_sql.append(", now(6), 1, 0)"))) {
        }
      }
      if (OB_SUCC(ret) && AI_DML_UPDATE == event
          && table_schema->get_rowkey_info().get_size() > 0) {
        // a primary key change deletes the old row; clean tasks of the old key
        common::ObSqlString old_predicate;
        if (OB_FAIL(build_rowkey_predicate(op, ctdef, ctdef.old_row_,
                                           *table_schema, old_predicate))) {
        } else if (OB_FAIL(clean_sql.append_fmt(
                     "DELETE FROM oceanbase.%s WHERE table_id = %lu AND rowkey_predicate = ",
                     OB_ALL_AI_COLUMN_TASK_TNAME, table_schema->get_table_id()))) {
        } else if (OB_FAIL(sql_append_hex_escape_str(old_predicate.string(), clean_sql))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      session->set_in_ai_column_maintenance(true);
      exec_ctx.get_das_ctx().is_ai_column_cascading_ = true;
      int inner_ret = OB_SUCCESS;
      session->set_inner_conn(NULL);
      op.close_inner_conn();
      if (OB_FAIL(inner_ret = op.open_inner_conn())) {
      } else if (OB_FAIL(inner_ret = op.begin_nested_session(false))) {
      } else {
        if (clean_sql.length() > 0) {
          inner_ret = op.execute_write(clean_sql.ptr());
        }
        if (OB_SUCC(inner_ret) && AI_DML_DELETE != event) {
          inner_ret = op.execute_write(update_sql.ptr());
        }
        if (OB_SUCC(inner_ret) && AI_DML_DELETE != event) {
          inner_ret = op.execute_write(task_sql.ptr());
        }
        int end_ret = op.end_nested_session();
        if (OB_SUCCESS != end_ret && OB_SUCC(inner_ret)) {
          inner_ret = end_ret;
        }
      }
      if (OB_NOT_INIT == inner_ret && AI_DML_INSERT == event) {
        // Autocommit inserts may not have an initialised inner connection.
        // The hidden state column defaults to PENDING, so publishing only the
        // task row is enough for the worker to pick the row up.
        int64_t affected_rows = 0;
        if (OB_ISNULL(GCTX.sql_proxy_)) {
          ret = OB_NOT_INIT;
        } else if (OB_FAIL(GCTX.sql_proxy_->write(task_sql.ptr(), affected_rows))) {
        }
      } else if (OB_FAIL(inner_ret)) {
        ret = inner_ret;
      }
      exec_ctx.get_das_ctx().is_ai_column_cascading_ = false;
      session->set_in_ai_column_maintenance(false);
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
