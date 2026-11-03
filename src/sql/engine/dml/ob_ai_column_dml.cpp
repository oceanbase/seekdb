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
#include "sql/engine/dml/ob_table_insert_op.h"
#include "sql/engine/dml/ob_table_update_op.h"
#include "sql/engine/dml/ob_table_delete_op.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_sql_context.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_struct.h"
#include "lib/string/ob_sql_string.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

const char *const ObAiColumnDml::STATE_PENDING = "PENDING";
const char *const ObAiColumnDml::STATE_READY = "READY";
const char *const ObAiColumnDml::STATE_FAILED = "FAILED";

namespace
{
// The name of the inner table that stores the pending AI column tasks.
const char *const AI_TASK_TABLE = "oceanbase.__all_ai_column_task";
// The maximum length of the request id of one change.
const int64_t MAX_REQUEST_ID_LENGTH = 64;

int escape_sql_string(ObSqlString &buf, const ObString &str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(buf.append("'"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < str.length(); ++i) {
      const char c = str.ptr()[i];
      if ('\'' == c || '\\' == c) {
        char esc[2] = {'\\', c};
        ret = buf.append(esc, 2);
      } else {
        ret = buf.append(&c, 1);
      }
    }
    if (OB_SUCC(ret)) {
      ret = buf.append("'");
    }
  }
  return ret;
}

// Appends the SQL literal of a datum.  The row key of a table is usually made
// of integers or strings, the other types are printed with their canonical
// representation.
int append_datum_literal(ObSqlString &buf, const ObDatum &datum, const ObObjMeta &meta)
{
  int ret = OB_SUCCESS;
  if (datum.is_null()) {
    ret = buf.append(" IS NULL");
  } else if (ob_is_integer_type(meta.get_type())) {
    ret = buf.append(" = ");
    if (OB_SUCC(ret)) {
      ret = buf.append_fmt("%ld", datum.get_int());
    }
  } else if (ob_is_string_type(meta.get_type())) {
    ret = buf.append(" = ");
    if (OB_SUCC(ret)) {
      ret = escape_sql_string(buf, datum.get_string());
    }
  } else {
    ObObj obj;
    if (OB_FAIL(datum.to_obj(obj, meta))) {
    } else {
      char tmp[OB_MAX_DEFAULT_VALUE_LENGTH];
      const int64_t len = obj.to_string(tmp, sizeof(tmp));
      if (OB_UNLIKELY(len <= 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = buf.append(" = ");
        if (OB_SUCC(ret)) {
          ret = buf.append(tmp, len);
        }
      }
    }
  }
  return ret;
}

// Runs one write statement in a nested session, so that it shares the
// transaction of the business statement.
int execute_nested_write(ObTableModifyOp &op, const char *sql)
{
  int ret = OB_SUCCESS;
  // the nested sql statements of the ai column feature must skip the
  // end-of-statement nested session check of the session
  op.get_exec_ctx().set_ai_invalidation_sql(true);
  if (OB_FAIL(op.open_inner_conn())) {
    LOG_WARN("fail to open inner connection", K(ret));
  } else if (OB_FAIL(op.begin_nested_session(false))) {
    LOG_WARN("fail to begin nested session", K(ret));
  } else {
    ret = op.execute_write(sql);
    if (OB_SUCCESS != ret) {
      LOG_WARN("fail to execute nested sql", K(ret), K(sql));
    }
    const int end_ret = op.end_nested_session();
    if (OB_SUCCESS != end_ret) {
      LOG_WARN("fail to end nested session", K(end_ret));
      if (OB_SUCCESS == ret) {
        ret = end_ret;
      }
    }
  }
  return ret;
}

// Reads the result of one nested select statement.
int execute_nested_read(ObTableModifyOp &op, const char *sql, ObMySQLProxy::MySQLResult &res)
{
  int ret = OB_SUCCESS;
  op.get_exec_ctx().set_ai_invalidation_sql(true);
  if (OB_FAIL(op.open_inner_conn())) {
    LOG_WARN("fail to open inner connection", K(ret));
  } else if (OB_FAIL(op.begin_nested_session(false))) {
    LOG_WARN("fail to begin nested session", K(ret));
  } else {
    ret = op.execute_read(sql, res);
    if (OB_SUCCESS != ret) {
      LOG_WARN("fail to execute nested sql", K(ret), K(sql));
    }
    const int end_ret = op.end_nested_session();
    if (OB_SUCCESS != end_ret) {
      LOG_WARN("fail to end nested session", K(end_ret));
      if (OB_SUCCESS == ret) {
        ret = end_ret;
      }
    }
  }
  return ret;
}

// Fetches the name of the database of one table, the name is deep copied
// into `alloc`.
int fetch_db_name(ObSchemaGetterGuard &schema_guard,
                  const ObTableSchema &table_schema,
                  ObIAllocator &alloc,
                  ObString &db_name)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema *db_schema = NULL;
  if (OB_FAIL(schema_guard.get_database_schema(table_schema.get_database_id(), db_schema))) {
    LOG_WARN("fail to get the database schema", K(ret));
  } else if (OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema is null", K(ret), K(table_schema.get_database_id()));
  } else if (OB_FAIL(ObAiColumnSpec::copy_str(alloc, db_schema->get_database_name_str(), db_name))) {
    LOG_WARN("fail to copy the database name", K(ret));
  }
  return ret;
}
} // namespace

ObAiColumnDmlState::ObAiColumnDmlState()
    : allocator_(ObModIds::OB_MODULE_PAGE_ALLOCATOR, OB_MALLOC_NORMAL_BLOCK_SIZE)
{
}

ObAiColumnDmlState::~ObAiColumnDmlState()
{
}

void ObAiColumnDmlState::reset()
{
  pending_rows_.reset();
  pending_deletes_.reset();
}

int ObAiColumnDml::gen_request_id(ObIAllocator &alloc, ObString &request_id)
{
  int ret = OB_SUCCESS;
  static uint64_t request_seq = 0;
  const uint64_t seq = __sync_add_and_fetch(&request_seq, 1);
  char buf[MAX_REQUEST_ID_LENGTH];
  const int64_t len = snprintf(buf, sizeof(buf), "%ld_%ld_%lu",
                               static_cast<long>(ObTimeUtility::fast_current_time()),
                               static_cast<long>(GETTID()),
                               seq);
  if (OB_UNLIKELY(len <= 0 || len >= static_cast<int64_t>(sizeof(buf)))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    char *copy = static_cast<char *>(alloc.alloc(len + 1));
    if (OB_ISNULL(copy)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(copy, buf, len);
      copy[len] = '\0';
      request_id.assign_ptr(copy, static_cast<int32_t>(len));
    }
  }
  return ret;
}

int ObAiColumnDml::build_hidden_col_name(const ObString &col_name,
                                         const char *prefix,
                                         ObIAllocator &alloc,
                                         ObString &name)
{
  int ret = OB_SUCCESS;
  char buf[OB_MAX_COLUMN_NAME_LENGTH + 32];
  const int64_t len = snprintf(buf, sizeof(buf), "%s%.*s", prefix,
                               static_cast<int32_t>(col_name.length()), col_name.ptr());
  if (OB_UNLIKELY(len <= 0 || len >= static_cast<int64_t>(sizeof(buf)))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    char *copy = static_cast<char *>(alloc.alloc(len + 1));
    if (OB_ISNULL(copy)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(copy, buf, len);
      copy[len] = '\0';
      name.assign_ptr(copy, static_cast<int32_t>(len));
    }
  }
  return ret;
}

int ObAiColumnDml::append_sql_literal(ObSqlString &buf, const ObObj &obj, ObCollationType coll_type)
{
  UNUSED(coll_type);
  int ret = OB_SUCCESS;
  if (obj.is_null()) {
    ret = buf.append(" IS NULL");
  } else if (ob_is_integer_type(obj.get_type())) {
    ret = buf.append_fmt("%ld", obj.get_int());
  } else if (ob_is_string_type(obj.get_type())) {
    ret = escape_sql_string(buf, obj.get_string());
  } else {
    char tmp[OB_MAX_DEFAULT_VALUE_LENGTH];
    const int64_t len = obj.to_string(tmp, sizeof(tmp));
    if (OB_UNLIKELY(len <= 0)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = buf.append(tmp, len);
    }
  }
  return ret;
}

int ObAiColumnDml::collect_ai_columns_by_schema(
    ObTableModifyOp &op,
    const ObTableSchema &table_schema,
    ObSEArray<ObAiColumnMeta, 4> &ai_columns)
{
  int ret = OB_SUCCESS;
  ObIAllocator &alloc = op.get_exec_ctx().get_allocator();
  const uint64_t table_id = table_schema.get_table_id();
  ObTableSchema::const_column_iterator iter = table_schema.column_begin();
  for (; OB_SUCC(ret) && iter != table_schema.column_end(); ++iter) {
    const ObColumnSchemaV2 *col = *iter;
    if (OB_ISNULL(col)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!col->is_ai_result_column()) {
      continue;
    } else {
      ObAiColumnMeta meta;
      meta.table_id_ = table_id;
      meta.result_column_id_ = col->get_column_id();
      ObString state_name;
      ObString request_name;
      ObString spec_name;
      const ObColumnSchemaV2 *state_col = NULL;
      const ObColumnSchemaV2 *request_col = NULL;
      const ObColumnSchemaV2 *spec_col = NULL;
      if (OB_FAIL(build_hidden_col_name(col->get_column_name_str(), "__ai_state_", alloc, state_name))
          || OB_FAIL(build_hidden_col_name(col->get_column_name_str(), "__ai_request_", alloc, request_name))
          || OB_FAIL(build_hidden_col_name(col->get_column_name_str(), "__ai_spec_", alloc, spec_name))) {
        LOG_WARN("fail to build hidden column name", K(ret));
      } else if (OB_ISNULL(state_col = table_schema.get_column_schema(state_name))
                 || OB_ISNULL(request_col = table_schema.get_column_schema(request_name))
                 || OB_ISNULL(spec_col = table_schema.get_column_schema(spec_name))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai hidden columns are missing", K(ret), K(col->get_column_name_str()));
      } else if (OB_FAIL(meta.spec_.parse(col->get_ai_spec_text(), alloc))) {
        // a column without a valid spec is not an ai column
        LOG_WARN("fail to parse ai column spec", K(ret), K(col->get_column_name_str()));
        ret = OB_SUCCESS;
      } else {
        meta.state_column_id_ = state_col->get_column_id();
        meta.request_column_id_ = request_col->get_column_id();
        meta.spec_column_id_ = spec_col->get_column_id();
        meta.result_column_name_ = col->get_column_name_str();
        if (OB_FAIL(ObAiColumnSpec::copy_str(alloc, meta.result_column_name_, meta.result_column_name_))
            || OB_FAIL(ObAiColumnSpec::copy_str(alloc, state_name, meta.state_column_name_))
            || OB_FAIL(ObAiColumnSpec::copy_str(alloc, request_name, meta.request_column_name_))
            || OB_FAIL(ObAiColumnSpec::copy_str(alloc, spec_name, meta.spec_column_name_))) {
          LOG_WARN("fail to copy ai column names", K(ret));
        } else if (OB_FAIL(ai_columns.push_back(meta))) {
          LOG_WARN("fail to push ai column", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObAiColumnDml::collect_ai_columns(ObTableModifyOp &op,
                                       const ObDMLBaseCtDef &ctdef,
                                       ObSEArray<ObAiColumnMeta, 4> &ai_columns)
{
  int ret = OB_SUCCESS;
  if (!ctdef.is_primary_index_) {
    // only the main table of the statement triggers the hook, the index
    // tables are maintained by the main table
  } else {
    const uint64_t table_id = ctdef.das_base_ctdef_.index_tid_;
    ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
    const ObTableSchema *table_schema = NULL;
    if (OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is null", K(ret));
    } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
      LOG_WARN("fail to get table schema", K(ret), K(table_id));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema is null", K(ret), K(table_id));
    } else if (OB_FAIL(collect_ai_columns_by_schema(op, *table_schema, ai_columns))) {
      LOG_WARN("fail to collect ai columns", K(ret));
    }
  }
  return ret;
}

int ObAiColumnDml::check_write_protection(ObTableModifyOp &op,
                                          const ObDMLBaseCtDef &ctdef,
                                          const ObSEArray<ObAiColumnMeta, 4> &ai_columns)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = op.get_exec_ctx().get_my_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (AI_COL_MODE_NONE != session->get_ai_column_internal_mode()) {
    // the internal modes are allowed to write the ai result columns
  } else if (session->is_inner()) {
    // the SQL of internal components (e.g. the background worker that
    // publishes a generated result) may write the ai result columns
  } else if (ai_columns.empty()) {
    // nothing to protect
  } else {
    bool hit = false;
    if (DAS_OP_TABLE_UPDATE == ctdef.dml_type_) {
      // the SET list of an update is known: only an assigned column is a
      // write, an unassigned column keeps the value of the old row
      const ObUpdCtDef &upd_ctdef = static_cast<const ObUpdCtDef &>(ctdef);
      const UIntFixedArray &assigned_column_ids = upd_ctdef.dupd_ctdef_.updated_column_ids_;
      for (int64_t i = 0; OB_SUCC(ret) && !hit && i < ai_columns.count(); ++i) {
        hit = has_exist_in_array(assigned_column_ids,
                                 ai_columns.at(i).result_column_id_);
      }
    } else if (DAS_OP_TABLE_INSERT == ctdef.dml_type_) {
      // the insert column list is not kept in the ctdef, the value of the
      // result column is used to detect the write: a NULL value means the
      // column was not assigned by the user
      const ObExprPtrIArray &row = ctdef.new_row_;
      for (int64_t i = 0; OB_SUCC(ret) && !hit && i < ai_columns.count(); ++i) {
        const uint64_t result_cid = ai_columns.at(i).result_column_id_;
        for (int64_t j = 0; OB_SUCC(ret) && !hit && j < ctdef.column_ids_.count(); ++j) {
          if (ctdef.column_ids_.at(j) != result_cid) {
            continue;
          }
          ObDatum *datum = NULL;
          if (OB_FAIL(row.at(j)->eval(op.get_eval_ctx(), datum))) {
          } else if (!datum->is_null()) {
            hit = true;
          }
        }
      }
    }
    if (OB_SUCC(ret) && hit) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "write the result column of an AI column");
      LOG_WARN("ai result column is protected", K(ret));
    }
  }
  return ret;
}

int ObAiColumnDml::build_rowkey_predicate(ObTableModifyOp &op,
                                          const ExprFixedArray &row,
                                          const UIntFixedArray &column_ids,
                                          const ObTableSchema &table_schema,
                                          ObIAllocator &alloc,
                                          ObString &predicate)
{
  int ret = OB_SUCCESS;
  ObSqlString buf;
  const ObRowkeyInfo &rowkey_info = table_schema.get_rowkey_info();
  const int64_t rowkey_cnt = rowkey_info.get_size();
  for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_cnt; ++i) {
    uint64_t column_id = OB_INVALID_ID;
    const ObColumnSchemaV2 *column = NULL;
    int64_t index = OB_INVALID_INDEX;
    if (OB_FAIL(rowkey_info.get_column_id(i, column_id))) {
      LOG_WARN("fail to get rowkey column id", K(ret));
    } else if (OB_ISNULL(column = table_schema.get_column_schema(column_id))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("rowkey column is missing", K(ret), K(column_id));
    } else if (!has_exist_in_array(column_ids, column_id, &index)
               || index >= row.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("rowkey column is not in the dml row", K(ret), K(column_id));
    } else {
      ObDatum *datum = NULL;
      if (OB_FAIL(row.at(index)->eval(op.get_eval_ctx(), datum))) {
        LOG_WARN("fail to eval rowkey datum", K(ret), K(column_id));
      } else if (i > 0 && OB_FAIL(buf.append(" AND "))) {
      } else if (OB_FAIL(buf.append_fmt("`%.*s`", column->get_column_name_str().length(),
                                       column->get_column_name_str().ptr()))) {
      } else if (OB_FAIL(append_datum_literal(buf, *datum, row.at(index)->obj_meta_))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    char *copy = static_cast<char *>(alloc.alloc(buf.length() + 1));
    if (OB_ISNULL(copy)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(copy, buf.ptr(), buf.length());
      copy[buf.length()] = '\0';
      predicate.assign_ptr(copy, static_cast<int32_t>(buf.length()));
    }
  }
  return ret;
}

int ObAiColumnDml::column_value_changed(ObTableModifyOp &op,
                                         const ObDMLBaseCtDef &ctdef,
                                         uint64_t column_id,
                                         bool &changed)
{
  int ret = OB_SUCCESS;
  changed = false;
  int64_t index = OB_INVALID_INDEX;
  if (!has_exist_in_array(ctdef.column_ids_, column_id, &index)) {
    // the column is not part of the dml row
  } else if (index >= ctdef.old_row_.count() || index >= ctdef.new_row_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid dml row index", K(ret), K(index));
  } else {
    ObDatum *old_datum = NULL;
    ObDatum *new_datum = NULL;
    if (OB_FAIL(ctdef.old_row_.at(index)->eval(op.get_eval_ctx(), old_datum))
        || OB_FAIL(ctdef.new_row_.at(index)->eval(op.get_eval_ctx(), new_datum))) {
      LOG_WARN("fail to eval column datum", K(ret), K(column_id));
    } else {
      changed = !ObDatum::binary_equal(*old_datum, *new_datum);
    }
  }
  return ret;
}

int ObAiColumnDml::rowkey_changed(ObTableModifyOp &op,
                                   const ObDMLBaseCtDef &ctdef,
                                   const ObTableSchema &table_schema,
                                   bool &changed)
{
  int ret = OB_SUCCESS;
  changed = false;
  const ObRowkeyInfo &rowkey_info = table_schema.get_rowkey_info();
  for (int64_t i = 0; OB_SUCC(ret) && !changed && i < rowkey_info.get_size(); ++i) {
    uint64_t column_id = OB_INVALID_ID;
    if (OB_FAIL(rowkey_info.get_column_id(i, column_id))) {
      LOG_WARN("fail to get rowkey column id", K(ret));
    } else if (OB_FAIL(column_value_changed(op, ctdef, column_id, changed))) {
      LOG_WARN("fail to check rowkey change", K(ret), K(column_id));
    }
  }
  return ret;
}

int ObAiColumnDml::add_pending_row(ObTableModifyOp &op,
                                    const ObTableSchema &table_schema,
                                    const ObString &db_name,
                                    const ObString &rowkey_predicate,
                                    const ObSEArray<uint64_t, 4> &column_ids,
                                    ObString &request_id)
{
  int ret = OB_SUCCESS;
  ObAiColumnDmlState &state = op.get_ai_column_dml_state();
  ObIAllocator &alloc = state.get_allocator();
  ObAiColumnDmlState::PendingRow pending_row;
  pending_row.table_id_ = table_schema.get_table_id();
  char *db_name_buf = static_cast<char *>(alloc.alloc(db_name.length() + 1));
  char *table_name = static_cast<char *>(alloc.alloc(table_schema.get_table_name_str().length() + 1));
  char *predicate = static_cast<char *>(alloc.alloc(rowkey_predicate.length() + 1));
  if (OB_ISNULL(db_name_buf) || OB_ISNULL(table_name) || OB_ISNULL(predicate)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    const int64_t db_len = db_name.length();
    const int64_t table_len = table_schema.get_table_name_str().length();
    MEMCPY(db_name_buf, db_name.ptr(), db_len);
    db_name_buf[db_len] = '\0';
    MEMCPY(table_name, table_schema.get_table_name_str().ptr(), table_len);
    table_name[table_len] = '\0';
    MEMCPY(predicate, rowkey_predicate.ptr(), rowkey_predicate.length());
    predicate[rowkey_predicate.length()] = '\0';
    pending_row.db_name_.assign_ptr(db_name_buf, static_cast<int32_t>(db_len));
    pending_row.table_name_.assign_ptr(table_name, static_cast<int32_t>(table_len));
    pending_row.rowkey_predicate_.assign_ptr(predicate, static_cast<int32_t>(rowkey_predicate.length()));
    if (OB_FAIL(gen_request_id(alloc, request_id))) {
      LOG_WARN("fail to generate request id", K(ret));
    } else {
      pending_row.request_id_ = request_id;
      for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
        if (OB_FAIL(pending_row.column_ids_.push_back(column_ids.at(i)))) {
          LOG_WARN("fail to push column id", K(ret));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(state.pending_rows_.push_back(pending_row))) {
        LOG_WARN("fail to push pending row", K(ret));
      }
    }
  }
  return ret;
}

int ObAiColumnDml::on_insert_row(ObTableInsertOp &op, const ObInsCtDef &ctdef, ObInsRtDef &rtdef)
{
  int ret = OB_SUCCESS;
  UNUSED(rtdef);
  ObSEArray<ObAiColumnMeta, 4> ai_columns;
  LOG_INFO("ai dml on_insert_row enter");
  if (OB_FAIL(collect_ai_columns(op, ctdef, ai_columns))) {
    LOG_WARN("fail to collect ai columns", K(ret));
  } else if (ai_columns.empty()) {
    // the table has no ai column
    LOG_INFO("ai dml on_insert_row no ai columns");
  } else if (OB_FAIL(check_write_protection(op, ctdef, ai_columns))) {
    LOG_WARN("ai column write is rejected", K(ret));
  } else {
    ObSQLSessionInfo *session = op.get_exec_ctx().get_my_session();
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (AI_COL_MODE_NONE != session->get_ai_column_internal_mode()) {
      // the internal modes manage the ai columns themselves
    } else {
      ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
      const ObTableSchema *table_schema = NULL;
      const uint64_t table_id = ctdef.das_base_ctdef_.index_tid_;
      ObString rowkey_predicate;
      ObString request_id;
      ObString db_name;
      ObSEArray<uint64_t, 4> column_ids;
      if (OB_ISNULL(schema_guard)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
        LOG_WARN("fail to get table schema", K(ret), K(table_id));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(fetch_db_name(*schema_guard, *table_schema,
                                       op.get_exec_ctx().get_allocator(), db_name))) {
        LOG_WARN("fail to fetch the database name", K(ret));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < ai_columns.count(); ++i) {
          if (OB_FAIL(column_ids.push_back(ai_columns.at(i).result_column_id_))) {
            LOG_WARN("fail to push column id", K(ret));
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(build_rowkey_predicate(op, ctdef.new_row_, ctdef.column_ids_,
                                                           *table_schema,
                                                           op.get_exec_ctx().get_allocator(),
                                                           rowkey_predicate))) {
          LOG_WARN("fail to build rowkey predicate", K(ret));
        } else if (OB_SUCC(ret) && OB_FAIL(add_pending_row(op, *table_schema, db_name,
                                                           rowkey_predicate,
                                                           column_ids, request_id))) {
          LOG_WARN("fail to add pending row", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObAiColumnDml::on_update_row(ObTableUpdateOp &op, const ObUpdCtDef &ctdef, ObUpdRtDef &rtdef)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObAiColumnMeta, 4> ai_columns;
  if (OB_FAIL(collect_ai_columns(op, ctdef, ai_columns))) {
    LOG_WARN("fail to collect ai columns", K(ret));
  } else if (ai_columns.empty()) {
    // the table has no ai column
  } else if (OB_FAIL(check_write_protection(op, ctdef, ai_columns))) {
    LOG_WARN("ai column write is rejected", K(ret));
  } else {
    ObSQLSessionInfo *session = op.get_exec_ctx().get_my_session();
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObSQLSessionInfo::AiColumnInternalMode mode = session->get_ai_column_internal_mode();
      if (ObSQLSessionInfo::AI_COL_MODE_PUBLISH == mode) {
        // the worker publishes a result, the row is only written when the
        // request id of the row is still the one of the published group
        ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
        const ObTableSchema *table_schema = NULL;
        const uint64_t table_id = ctdef.das_base_ctdef_.index_tid_;
        const ObString &publish_request_id = session->get_ai_column_publish_request_id();
        bool matched = true;
        if (OB_ISNULL(schema_guard)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
          LOG_WARN("fail to get table schema", K(ret), K(table_id));
        } else if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && matched && i < ai_columns.count(); ++i) {
            const ObAiColumnMeta &meta = ai_columns.at(i);
            int64_t index = OB_INVALID_INDEX;
            if (!has_exist_in_array(ctdef.column_ids_, meta.result_column_id_, &index)) {
              continue;
            }
            // the result column is assigned, the request column of the row
            // must still carry the request id of the published group
            int64_t request_index = OB_INVALID_INDEX;
            if (!has_exist_in_array(ctdef.column_ids_, meta.request_column_id_, &request_index)
                || request_index >= ctdef.old_row_.count()) {
              matched = false;
            } else {
              ObDatum *datum = NULL;
              if (OB_FAIL(ctdef.old_row_.at(request_index)->eval(op.get_eval_ctx(), datum))) {
                LOG_WARN("fail to eval request datum", K(ret));
              } else if (datum->is_null()
                         || 0 != datum->get_string().case_compare(publish_request_id)) {
                matched = false;
              }
            }
          }
          if (OB_SUCC(ret) && !matched) {
            // the row has been changed by a newer request, the late result
            // must not overwrite the new value
            LOG_INFO("skip the stale publish of an ai column", K(publish_request_id));
            rtdef.is_row_changed_ = false;
          }
        }
      } else if (ObSQLSessionInfo::AI_COL_MODE_INVALIDATE == mode) {
        // an internal session resets the result column, the task of the row
        // is created here
        ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
        const ObTableSchema *table_schema = NULL;
        const uint64_t table_id = ctdef.das_base_ctdef_.index_tid_;
        if (OB_ISNULL(schema_guard)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
          LOG_WARN("fail to get table schema", K(ret), K(table_id));
        } else if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          // the invalidation statement assigns the result column, the state
          // column, the request column and the spec column of the ai columns
          // it resets; the task of every assigned ai column is created here
          ObAiColumnDmlState::PendingRow pending_row;
          ObString request_id;
          ObString rowkey_predicate;
          for (int64_t i = 0; OB_SUCC(ret) && i < ai_columns.count(); ++i) {
            const ObAiColumnMeta &meta = ai_columns.at(i);
            int64_t index = OB_INVALID_INDEX;
            if (!has_exist_in_array(ctdef.column_ids_, meta.result_column_id_, &index)) {
              continue;
            } else if (OB_FAIL(pending_row.column_ids_.push_back(meta.result_column_id_))) {
              LOG_WARN("fail to push column id", K(ret));
            }
            // the request id is taken from the assigned request column, all
            // the ai columns of the row share one request id
            if (OB_SUCC(ret) && request_id.empty()) {
              int64_t request_index = OB_INVALID_INDEX;
              if (has_exist_in_array(ctdef.column_ids_, meta.request_column_id_, &request_index)
                  && request_index < ctdef.new_row_.count()) {
                ObDatum *datum = NULL;
                if (OB_FAIL(ctdef.new_row_.at(request_index)->eval(op.get_eval_ctx(), datum))) {
                  LOG_WARN("fail to eval request datum", K(ret));
                } else if (!datum->is_null()) {
                  request_id = datum->get_string();
                }
              }
            }
          }
          if (OB_SUCC(ret) && !pending_row.column_ids_.empty()) {
            if (request_id.empty()) {
              if (OB_FAIL(gen_request_id(op.get_exec_ctx().get_allocator(), request_id))) {
                LOG_WARN("fail to generate request id", K(ret));
              }
            }
            if (OB_SUCC(ret)) {
              if (OB_FAIL(build_rowkey_predicate(op, ctdef.new_row_, ctdef.column_ids_, *table_schema,
                                                 op.get_exec_ctx().get_allocator(), rowkey_predicate))) {
                LOG_WARN("fail to build rowkey predicate", K(ret));
              }
            }
          }
          if (OB_SUCC(ret) && !pending_row.column_ids_.empty()) {
            pending_row.table_id_ = table_id;
            pending_row.table_name_ = table_schema->get_table_name_str();
            pending_row.request_id_ = request_id;
            pending_row.rowkey_predicate_ = rowkey_predicate;
            if (OB_FAIL(fetch_db_name(*schema_guard, *table_schema,
                                      op.get_exec_ctx().get_allocator(), pending_row.db_name_))) {
              LOG_WARN("fail to fetch the database name", K(ret));
            } else if (OB_FAIL(create_tasks(op, pending_row, ai_columns))) {
              LOG_WARN("fail to create the ai column task", K(ret));
            }
          }
        }
      } else {
        // an ordinary user statement, the result of the row is invalidated
        // when one of the dependency columns or the row key changed
        if (!rtdef.is_row_changed_) {
          // the row is not changed at all, the result is still valid
        } else {
          ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
          const ObTableSchema *table_schema = NULL;
          const uint64_t table_id = ctdef.das_base_ctdef_.index_tid_;
          ObSEArray<uint64_t, 4> column_ids;
          if (OB_ISNULL(schema_guard)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
            LOG_WARN("fail to get table schema", K(ret), K(table_id));
          } else if (OB_ISNULL(table_schema)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            bool rowkey_modified = false;
            if (OB_FAIL(rowkey_changed(op, ctdef, *table_schema, rowkey_modified))) {
              LOG_WARN("fail to check the rowkey change", K(ret));
            }
            // when the row key itself was changed, the pending tasks of the
            // old row are removed, the new row gets fresh tasks
            if (OB_SUCC(ret) && rowkey_modified) {
              ObString old_predicate;
              if (OB_FAIL(build_rowkey_predicate(op, ctdef.old_row_, ctdef.column_ids_, *table_schema,
                                                 op.get_exec_ctx().get_allocator(), old_predicate))) {
                LOG_WARN("fail to build the old rowkey predicate", K(ret));
              } else {
                ObAiColumnDmlState &state = op.get_ai_column_dml_state();
                ObIAllocator &state_alloc = state.get_allocator();
                char *predicate = static_cast<char *>(state_alloc.alloc(old_predicate.length() + 1));
                if (OB_ISNULL(predicate)) {
                  ret = OB_ALLOCATE_MEMORY_FAILED;
                } else {
                  MEMCPY(predicate, old_predicate.ptr(), old_predicate.length());
                  predicate[old_predicate.length()] = '\0';
                  ObAiColumnDmlState::PendingDelete pending_delete;
                  pending_delete.table_id_ = table_id;
                  pending_delete.rowkey_predicate_.assign_ptr(
                      predicate, static_cast<int32_t>(old_predicate.length()));
                  if (OB_FAIL(state.pending_deletes_.push_back(pending_delete))) {
                    LOG_WARN("fail to push pending delete", K(ret));
                  }
                }
              }
            }
            if (OB_SUCC(ret)) {
              for (int64_t i = 0; OB_SUCC(ret) && i < ai_columns.count(); ++i) {
                const ObAiColumnMeta &meta = ai_columns.at(i);
                bool invalid = rowkey_modified;
                for (int64_t j = 0; OB_SUCC(ret) && !invalid && j < meta.spec_.dep_column_cnt_; ++j) {
                  bool changed = false;
                  if (OB_FAIL(column_value_changed(op, ctdef, meta.spec_.dep_column_ids_[j], changed))) {
                    LOG_WARN("fail to check the dependency column", K(ret));
                  } else if (changed) {
                    invalid = true;
                  }
                }
                if (OB_SUCC(ret) && invalid) {
                  if (OB_FAIL(column_ids.push_back(meta.result_column_id_))) {
                    LOG_WARN("fail to push column id", K(ret));
                  }
                }
              }
            }
            if (OB_SUCC(ret) && !column_ids.empty()) {
              ObString rowkey_predicate;
              ObString request_id;
              ObString db_name;
              if (OB_FAIL(build_rowkey_predicate(op, ctdef.new_row_, ctdef.column_ids_, *table_schema,
                                                 op.get_exec_ctx().get_allocator(), rowkey_predicate))) {
                LOG_WARN("fail to build rowkey predicate", K(ret));
              } else if (OB_FAIL(fetch_db_name(*schema_guard, *table_schema,
                                               op.get_exec_ctx().get_allocator(), db_name))) {
                LOG_WARN("fail to fetch the database name", K(ret));
              } else if (OB_FAIL(add_pending_row(op, *table_schema, db_name,
                                                 rowkey_predicate,
                                                 column_ids, request_id))) {
                LOG_WARN("fail to add pending row", K(ret));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAiColumnDml::on_delete_row(ObTableDeleteOp &op, const ObDelCtDef &ctdef, ObDelRtDef &rtdef)
{
  int ret = OB_SUCCESS;
  UNUSED(rtdef);
  ObSEArray<ObAiColumnMeta, 4> ai_columns;
  if (OB_FAIL(collect_ai_columns(op, ctdef, ai_columns))) {
    LOG_WARN("fail to collect ai columns", K(ret));
  } else if (ai_columns.empty()) {
    // the table has no ai column
  } else {
    ObSQLSessionInfo *session = op.get_exec_ctx().get_my_session();
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (AI_COL_MODE_NONE != session->get_ai_column_internal_mode()) {
      // the internal modes manage the ai columns themselves
    } else {
      ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
      const ObTableSchema *table_schema = NULL;
      const uint64_t table_id = ctdef.das_base_ctdef_.index_tid_;
      if (OB_ISNULL(schema_guard)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
        LOG_WARN("fail to get table schema", K(ret), K(table_id));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObString rowkey_predicate;
        if (OB_FAIL(build_rowkey_predicate(op, ctdef.old_row_, ctdef.column_ids_, *table_schema,
                                           op.get_exec_ctx().get_allocator(), rowkey_predicate))) {
          LOG_WARN("fail to build rowkey predicate", K(ret));
        } else {
          ObAiColumnDmlState &state = op.get_ai_column_dml_state();
          ObIAllocator &alloc = state.get_allocator();
          char *predicate = static_cast<char *>(alloc.alloc(rowkey_predicate.length() + 1));
          if (OB_ISNULL(predicate)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            MEMCPY(predicate, rowkey_predicate.ptr(), rowkey_predicate.length());
            predicate[rowkey_predicate.length()] = '\0';
            ObAiColumnDmlState::PendingDelete pending_delete;
            pending_delete.table_id_ = table_id;
            pending_delete.rowkey_predicate_.assign_ptr(predicate,
                                                        static_cast<int32_t>(rowkey_predicate.length()));
            if (OB_FAIL(state.pending_deletes_.push_back(pending_delete))) {
              LOG_WARN("fail to push pending delete", K(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAiColumnDml::create_tasks(ObTableModifyOp &op,
                                const ObAiColumnDmlState::PendingRow &pending_row,
                                const ObSEArray<ObAiColumnMeta, 4> &ai_columns)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObSqlString values;
  int64_t value_cnt = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < ai_columns.count(); ++i) {
    const ObAiColumnMeta &meta = ai_columns.at(i);
    bool selected = false;
    for (int64_t j = 0; OB_SUCC(ret) && !selected && j < pending_row.column_ids_.count(); ++j) {
      selected = pending_row.column_ids_.at(j) == meta.result_column_id_;
    }
    if (!selected) {
      continue;
    }
    // the payload of the task is the value of the prompt expression of the
    // ai column, it is evaluated by the database itself
    ObSqlString select_sql;
    ObString payload;
    if (OB_FAIL(select_sql.append_fmt("SELECT %.*s FROM `%.*s`.`%.*s` WHERE %.*s",
                                       static_cast<int32_t>(meta.spec_.prompt_expr_.length()),
                                       meta.spec_.prompt_expr_.ptr(),
                                       static_cast<int32_t>(pending_row.db_name_.length()),
                                       pending_row.db_name_.ptr(),
                                       static_cast<int32_t>(pending_row.table_name_.length()),
                                       pending_row.table_name_.ptr(),
                                       static_cast<int32_t>(pending_row.rowkey_predicate_.length()),
                                       pending_row.rowkey_predicate_.ptr()))) {
      LOG_WARN("fail to build the payload select", K(ret));
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        if (OB_FAIL(execute_nested_read(op, select_sql.ptr(), res))) {
          LOG_WARN("fail to select the payload", K(ret));
        } else if (OB_ISNULL(res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("the payload select has no result", K(ret));
        } else if (OB_FAIL(res.get_result()->next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("fail to read the payload", K(ret));
          }
        } else {
          ObObj value_obj;
          if (OB_FAIL(res.get_result()->get_obj(0, value_obj, NULL,
                                                &op.get_exec_ctx().get_allocator()))) {
            LOG_WARN("fail to get the payload", K(ret));
          } else if (!value_obj.is_null()) {
            payload = value_obj.get_string();
          }
          const int close_ret = res.get_result()->close();
          if (OB_SUCCESS != close_ret) {
            LOG_WARN("fail to close the payload result", K(close_ret));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (value_cnt > 0 && OB_FAIL(values.append(","))) {
      } else if (OB_FAIL(values.append_fmt("(%ld,", static_cast<int64_t>(pending_row.table_id_)))) {
      } else if (OB_FAIL(escape_sql_string(values, pending_row.request_id_))) {
      } else if (OB_FAIL(values.append_fmt(",%lu,", meta.result_column_id_))) {
      } else if (OB_FAIL(escape_sql_string(values, pending_row.rowkey_predicate_))) {
      } else if (OB_FAIL(values.append(","))) {
      } else if (OB_FAIL(escape_sql_string(values, payload))) {
      } else if (OB_FAIL(values.append_fmt(",1,0,now(6),%ld)",
                                          static_cast<int64_t>(pending_row.def_version_)))) {
      } else {
        ++value_cnt;
      }
    }
  }
  if (OB_SUCC(ret) && value_cnt > 0) {
    if (OB_FAIL(sql.append_fmt("INSERT INTO %s(table_id,request_id,column_id,rowkey_predicate,"
                                "payload,state,retry_count,eligible_ts,def_version) VALUES ",
                                AI_TASK_TABLE))) {
    } else if (OB_FAIL(sql.append(values.ptr(), values.length()))) {
    } else if (OB_FAIL(execute_nested_write(op, sql.ptr()))) {
      LOG_WARN("fail to insert the ai column task", K(ret));
    }
  }
  return ret;
}

int ObAiColumnDml::invalidate_row(ObTableModifyOp &op,
                                  const ObAiColumnDmlState::PendingRow &pending_row,
                                  const ObSEArray<ObAiColumnMeta, 4> &ai_columns)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObString request_id;
  if (OB_FAIL(gen_request_id(op.get_exec_ctx().get_allocator(), request_id))) {
    LOG_WARN("fail to generate request id", K(ret));
  } else if (OB_FAIL(sql.append_fmt("UPDATE `%.*s`.`%.*s` SET ",
                                     static_cast<int32_t>(pending_row.db_name_.length()),
                                     pending_row.db_name_.ptr(),
                                     static_cast<int32_t>(pending_row.table_name_.length()),
                                     pending_row.table_name_.ptr()))) {
    LOG_WARN("fail to build the invalidate sql", K(ret));
  } else {
    bool first = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < ai_columns.count(); ++i) {
      const ObAiColumnMeta &meta = ai_columns.at(i);
      bool selected = false;
      for (int64_t j = 0; OB_SUCC(ret) && !selected && j < pending_row.column_ids_.count(); ++j) {
        selected = pending_row.column_ids_.at(j) == meta.result_column_id_;
      }
      if (!selected) {
        continue;
      }
      const ObString &result_name = meta.result_column_name_;
      if (!first && OB_FAIL(sql.append(","))) {
      } else if (OB_FAIL(sql.append_fmt("`%.*s`=NULL", result_name.length(), result_name.ptr()))) {
      } else if (OB_FAIL(sql.append_fmt(",`%.*s`='%s'", static_cast<int32_t>(meta.state_column_name_.length()),
                                        meta.state_column_name_.ptr(), STATE_PENDING))) {
      } else if (OB_FAIL(sql.append_fmt(",`%.*s`=", static_cast<int32_t>(meta.request_column_name_.length()),
                                        meta.request_column_name_.ptr()))) {
      } else if (OB_FAIL(escape_sql_string(sql, request_id))) {
      } else if (OB_FAIL(sql.append_fmt(",`%.*s`='%ld'",
                                        static_cast<int32_t>(meta.spec_column_name_.length()),
                                        meta.spec_column_name_.ptr(),
                                        static_cast<int64_t>(meta.def_version_)))) {
      } else {
        first = false;
      }
    }
    // the rowkey columns are assigned to themselves: the hook of the nested
    // statement rebuilds the rowkey predicate from the assigned columns of
    // the row, the rowkey must be part of it
    if (OB_SUCC(ret)) {
      ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
      const ObTableSchema *table_schema = NULL;
      if (OB_ISNULL(schema_guard)
          || OB_FAIL(schema_guard->get_table_schema(pending_row.table_id_, table_schema))) {
        LOG_WARN("fail to get table schema", K(ret), K(pending_row.table_id_));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema is null", K(ret));
      } else {
        const ObRowkeyInfo &rowkey_info = table_schema->get_rowkey_info();
        for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_info.get_size(); ++i) {
          uint64_t rowkey_column_id = OB_INVALID_ID;
          const ObColumnSchemaV2 *rowkey_column = NULL;
          if (OB_FAIL(rowkey_info.get_column_id(i, rowkey_column_id))) {
            LOG_WARN("fail to get rowkey column id", K(ret));
          } else if (OB_ISNULL(rowkey_column = table_schema->get_column_schema(rowkey_column_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("rowkey column is missing", K(ret), K(rowkey_column_id));
          } else if (OB_FAIL(sql.append_fmt(",`%.*s`=`%.*s`",
                                            static_cast<int32_t>(rowkey_column->get_column_name_str().length()),
                                            rowkey_column->get_column_name_str().ptr(),
                                            static_cast<int32_t>(rowkey_column->get_column_name_str().length()),
                                            rowkey_column->get_column_name_str().ptr()))) {
            LOG_WARN("fail to append the rowkey assignment", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(sql.append_fmt(" WHERE %.*s",
                                               static_cast<int32_t>(pending_row.rowkey_predicate_.length()),
                                               pending_row.rowkey_predicate_.ptr()))) {
      LOG_WARN("fail to build the invalidate sql", K(ret));
    } else if (OB_SUCC(ret)) {
      // the internal mode invalidates the result, the hook of the nested
      // statement creates the task of the row
      ObSQLSessionInfo *session = op.get_exec_ctx().get_my_session();
      const ObSQLSessionInfo::AiColumnInternalMode old_mode = session->get_ai_column_internal_mode();
      session->set_ai_column_internal_mode(ObSQLSessionInfo::AI_COL_MODE_INVALIDATE);
      ret = execute_nested_write(op, sql.ptr());
      session->set_ai_column_internal_mode(old_mode);
      if (OB_SUCCESS != ret) {
        LOG_WARN("fail to invalidate the ai column", K(ret));
      }
    }
  }
  return ret;
}

int ObAiColumnDml::remove_tasks(ObTableModifyOp &op,
                                const ObSEArray<ObAiColumnDmlState::PendingDelete, 8> &pending_deletes)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (pending_deletes.empty()) {
    // nothing to remove
  } else if (OB_FAIL(sql.append_fmt("DELETE FROM %s WHERE ", AI_TASK_TABLE))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < pending_deletes.count(); ++i) {
      if (i > 0 && OB_FAIL(sql.append(" OR "))) {
      } else if (OB_FAIL(sql.append_fmt("table_id=%ld AND rowkey_predicate=",
                                        static_cast<int64_t>(pending_deletes.at(i).table_id_)))) {
      } else if (OB_FAIL(escape_sql_string(sql, pending_deletes.at(i).rowkey_predicate_))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(execute_nested_write(op, sql.ptr()))) {
      LOG_WARN("fail to remove the ai column tasks", K(ret));
    }
  }
  return ret;
}

int ObAiColumnDml::flush(ObTableModifyOp &op)
{
  LOG_INFO("ai dml flush enter");
  int ret = OB_SUCCESS;
  ObAiColumnDmlState &state = op.get_ai_column_dml_state();
  if (!state.has_pending_work()) {
    return ret;
  }
  // the tasks of the invalidated rows are created by the nested statement
  // that resets the result column
  ObSEArray<ObAiColumnDmlState::PendingRow, 8> pending_rows;
  for (int64_t i = 0; OB_SUCC(ret) && i < state.pending_rows_.count(); ++i) {
    if (OB_FAIL(pending_rows.push_back(state.pending_rows_.at(i)))) {
      LOG_WARN("fail to copy pending row", K(ret));
    }
  }
  ObSEArray<ObAiColumnDmlState::PendingDelete, 8> pending_deletes;
  for (int64_t i = 0; OB_SUCC(ret) && i < state.pending_deletes_.count(); ++i) {
    if (OB_FAIL(pending_deletes.push_back(state.pending_deletes_.at(i)))) {
      LOG_WARN("fail to copy pending delete", K(ret));
    }
  }
  state.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < pending_rows.count(); ++i) {
    ObAiColumnDmlState::PendingRow &pending_row = pending_rows.at(i);
    ObSchemaGetterGuard *schema_guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_;
    const ObTableSchema *table_schema = NULL;
    ObSEArray<ObAiColumnMeta, 4> ai_columns;
    if (OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(schema_guard->get_table_schema(pending_row.table_id_, table_schema))) {
      LOG_WARN("fail to get table schema", K(ret), K(pending_row.table_id_));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(collect_ai_columns_by_schema(op, *table_schema, ai_columns))) {
      LOG_WARN("fail to collect ai columns", K(ret));
    } else if (OB_SUCC(ret) && OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is null", K(ret));
    } else if (OB_SUCC(ret) && OB_FAIL(fetch_db_name(*schema_guard, *table_schema,
                                                     op.get_exec_ctx().get_allocator(),
                                                     pending_row.db_name_))) {
      LOG_WARN("fail to fetch the database name", K(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(invalidate_row(op, pending_row, ai_columns))) {
      LOG_WARN("fail to invalidate the ai column of the row", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(remove_tasks(op, pending_deletes))) {
    LOG_WARN("fail to remove the ai column tasks", K(ret));
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
