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

#include "observer/ai_column/ob_ai_column_util.h"
#include <new>
#include "observer/ai_column/ob_ai_column_worker.h"

#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/string/ob_sql_string.h"
#include "observer/ai_service/ob_ai_service_proxy.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/ob_server_struct.h"
#include "share/ob_share_util.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/schema/ob_table_schema.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/das/ob_das_define.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/parser/parse_node.h"

#define USING_LOG_PREFIX SHARE

namespace oceanbase
{
namespace share
{

using namespace common;

int ObAiColumnUtil::insert_meta(const ObAiColumnMeta &meta, uint64_t table_id,
                                ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObDMLSqlSplicer sql;
  ObSqlString buffer;
  ObSqlString del_sql;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(sql.add_pk_column("table_id", static_cast<int64_t>(table_id)))) {
  } else if (OB_FAIL(sql.add_pk_column("column_id", static_cast<int64_t>(meta.column_id_)))) {
  } else if (OB_FAIL(sql.add_column("column_name", ObHexEscapeSqlStr(meta.column_name_)))) {
  } else if (OB_FAIL(sql.add_column("max_retries", meta.max_retries_))) {
  } else if (OB_FAIL(sql.add_column("state_column_id", static_cast<int64_t>(meta.state_column_id_)))) {
  } else if (OB_FAIL(sql.add_column("request_column_id", static_cast<int64_t>(meta.request_column_id_)))) {
  } else if (OB_FAIL(sql.add_column("result_spec_column_id", static_cast<int64_t>(meta.result_spec_column_id_)))) {
  } else if (OB_FAIL(sql.add_column("normalized_expr", ObHexEscapeSqlStr(meta.normalized_expr_)))) {
  } else if (OB_FAIL(sql.splice_insert_sql(OB_ALL_AI_COLUMN_TNAME, buffer))) {
  } else if (OB_FAIL(del_sql.assign_fmt("DELETE FROM %s WHERE table_id = %ld AND column_id = %ld",
                                         OB_ALL_AI_COLUMN_TNAME, static_cast<int64_t>(table_id),
                                         static_cast<int64_t>(meta.column_id_)))) {
  } else if (OB_FAIL(trans.write(del_sql.ptr(), affected_rows))) {
  } else if (OB_FAIL(trans.write(buffer.ptr(), affected_rows))) {
  } else if (1 != affected_rows) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected affected rows", K(ret), K(affected_rows), K(buffer));
  }
  return ret;
}

int ObAiColumnUtil::insert_metas(const ObIArray<ObAiColumnMeta> &metas, uint64_t table_id)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  // bisection showed transactional writes from inside a CREATE/ALTER statement
  // are swallowed by the DDL transaction machinery, while direct autocommit
  // writes through sql_proxy_ persist; use direct writes here
  if (metas.empty()) {
    // nothing to do
  } else if (OB_FAIL(ObAiColumnWorker::get_instance().ensure_started())) {
    // the worker must run for async AI columns; do not fail the DDL on start hiccups
    LOG_WARN("fail to ensure ai column worker started", K(ret));
    ret = OB_SUCCESS;
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < metas.count(); ++i) {
      const ObAiColumnMeta &meta = metas.at(i);
      ObDMLSqlSplicer sql;
      ObSqlString buffer;
      ObSqlString del_sql;
      if (OB_FAIL(sql.add_pk_column("table_id", static_cast<int64_t>(table_id)))) {
      } else if (OB_FAIL(sql.add_pk_column("column_id", static_cast<int64_t>(meta.column_id_)))) {
      } else if (OB_FAIL(sql.add_column("column_name", ObHexEscapeSqlStr(meta.column_name_)))) {
      } else if (OB_FAIL(sql.add_column("max_retries", meta.max_retries_))) {
      } else if (OB_FAIL(sql.add_column("state_column_id", static_cast<int64_t>(meta.state_column_id_)))) {
      } else if (OB_FAIL(sql.add_column("request_column_id", static_cast<int64_t>(meta.request_column_id_)))) {
      } else if (OB_FAIL(sql.add_column("result_spec_column_id", static_cast<int64_t>(meta.result_spec_column_id_)))) {
      } else if (OB_FAIL(sql.add_column("normalized_expr", ObHexEscapeSqlStr(meta.normalized_expr_)))) {
      } else if (OB_FAIL(sql.splice_insert_sql(OB_ALL_AI_COLUMN_TNAME, buffer))) {
      } else if (OB_FAIL(del_sql.assign_fmt("DELETE FROM %s WHERE table_id = %ld AND column_id = %ld",
                                            OB_ALL_AI_COLUMN_TNAME, static_cast<int64_t>(table_id),
                                            static_cast<int64_t>(meta.column_id_)))) {
      } else if (OB_FAIL(GCTX.sql_proxy_->write(del_sql.ptr(), affected_rows))) {
        LOG_WARN("fail to delete stale ai meta", K(ret), K(del_sql));
      } else if (OB_FAIL(GCTX.sql_proxy_->write(buffer.ptr(), affected_rows))) {
        LOG_WARN("fail to insert ai column meta", K(ret), K(buffer));
      } else if (1 != affected_rows) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected affected rows", K(ret), K(affected_rows), K(buffer));
      }
    }
  }
  return ret;
}

int ObAiColumnUtil::delete_metas_by_table(uint64_t table_id, ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString buffer;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(buffer.assign_fmt("DELETE FROM %s WHERE table_id = %lu",
                                       OB_ALL_AI_COLUMN_TNAME, table_id))) {
  } else if (OB_FAIL(trans.write(buffer.ptr(), affected_rows))) {
    LOG_WARN("fail to delete ai column metas", K(ret), K(buffer));
  }
  return ret;
}

int ObAiColumnUtil::remove_metas_of_table(uint64_t table_id)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString buffer;
  // direct autocommit write: transactional writes from the DROP TABLE
  // statement context are swallowed by the DDL transaction machinery
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(buffer.assign_fmt("DELETE FROM %s WHERE table_id = %lu",
                                       OB_ALL_AI_COLUMN_TNAME, table_id))) {
  } else if (OB_FAIL(GCTX.sql_proxy_->write(buffer.ptr(), affected_rows))) {
    LOG_WARN("fail to delete ai column metas", K(ret), K(buffer));
  }
  return ret;
}

int ObAiColumnUtil::select_metas_by_table(uint64_t table_id, ObIAllocator &allocator,
                                          ObIArray<ObAiColumnMeta> &metas)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObTimeoutCtx timeout_ctx;
  const int64_t default_timeout = config::internal_sql_execute_timeout();
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, default_timeout))) {
  } else if (OB_FAIL(sql.assign_fmt("SELECT column_id, column_name, max_retries, state_column_id,"
                                    " request_column_id, result_spec_column_id, normalized_expr"
                                    " FROM %s WHERE table_id = %lu",
                                    OB_ALL_AI_COLUMN_TNAME, table_id))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
        LOG_WARN("fail to select ai column metas", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int tmp_ret = OB_SUCCESS;
        while (OB_SUCC(ret) && OB_SUCCESS == (tmp_ret = result->next())) {
          ObAiColumnMeta meta;
          meta.table_id_ = table_id;
          EXTRACT_INT_FIELD_MYSQL(*result, "column_id", meta.column_id_, uint64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "state_column_id", meta.state_column_id_, uint64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "request_column_id", meta.request_column_id_, uint64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "result_spec_column_id", meta.result_spec_column_id_, uint64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "max_retries", meta.max_retries_, int64_t);
          ObString col_name;
          ObString norm_expr;
          EXTRACT_VARCHAR_FIELD_MYSQL(*result, "column_name", col_name);
          EXTRACT_VARCHAR_FIELD_MYSQL(*result, "normalized_expr", norm_expr);
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(ob_write_string(allocator, col_name, meta.column_name_))) {
          } else if (OB_FAIL(ob_write_string(allocator, norm_expr, meta.normalized_expr_))) {
          } else if (OB_FAIL(meta.expr_.parse_from(meta.normalized_expr_, allocator))) {
            LOG_WARN("fail to parse normalized AI column expression", K(ret), K(table_id),
                     K(meta.column_id_));
          } else if (OB_FAIL(metas.push_back(meta))) {
          }
        }
        if (OB_SUCC(ret) && OB_ITER_END != tmp_ret) {
          ret = tmp_ret;
          LOG_WARN("fail to iterate ai column metas", K(ret), K(table_id));
        }
      }
    }
  }
  return ret;
}


static int set_add_column_type(AlterColumnSchema &col)
{
  col.alter_type_ = OB_DDL_ADD_COLUMN;
  col.set_origin_column_name(col.get_column_name_str());
  return OB_SUCCESS;
}

static int build_hidden_column_names(const ObString &col_name,
                                     ObString &state_name,
                                     ObString &request_name,
                                     ObString &spec_name,
                                     ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObStringBuffer sbuf(&allocator), rbuf(&allocator), pbuf(&allocator);
  if (OB_FAIL(sbuf.append(OB_AI_STATE_COLUMN_PREFIX))) {
  } else if (OB_FAIL(sbuf.append(col_name))) {
  } else if (OB_FAIL(rbuf.append(OB_AI_REQUEST_COLUMN_PREFIX))) {
  } else if (OB_FAIL(rbuf.append(col_name))) {
  } else if (OB_FAIL(pbuf.append(OB_AI_RESULT_SPEC_COLUMN_PREFIX))) {
  } else if (OB_FAIL(pbuf.append(col_name))) {
  } else if (OB_FAIL(ob_write_string(allocator, sbuf.string(), state_name))) {
  } else if (OB_FAIL(ob_write_string(allocator, rbuf.string(), request_name))) {
  } else if (OB_FAIL(ob_write_string(allocator, pbuf.string(), spec_name))) {
  } else {
    if (state_name.length() > OB_MAX_COLUMN_NAME_LENGTH) {
      state_name.set_length(OB_MAX_COLUMN_NAME_LENGTH);
    }
    if (request_name.length() > OB_MAX_COLUMN_NAME_LENGTH) {
      request_name.set_length(OB_MAX_COLUMN_NAME_LENGTH);
    }
    if (spec_name.length() > OB_MAX_COLUMN_NAME_LENGTH) {
      spec_name.set_length(OB_MAX_COLUMN_NAME_LENGTH);
    }
  }
  return ret;
}

static int fill_hidden_column_schema(ObColumnSchemaV2 &col, uint64_t column_id,
                                     const ObString &name, ObObjType data_type,
                                     int64_t length)
{
  int ret = OB_SUCCESS;
  col.reset();
  col.set_column_id(column_id);
  col.set_data_type(data_type);
  col.set_is_hidden(true);
  col.set_rowkey_position(0);
  col.set_index_position(0);
  col.set_not_part_key();
  if (ObVarcharType == data_type) {
    col.set_charset_type(CHARSET_UTF8MB4);
    col.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    col.set_data_length(static_cast<int32_t>(length));
    col.set_length_semantics(LS_BYTE);
  } else {
    col.set_charset_type(CHARSET_BINARY);
    col.set_collation_type(CS_TYPE_BINARY);
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(col.set_column_name(name))) {
    }
  }
  return ret;
}

int ObAiColumnUtil::append_hidden_columns_for_create(schema::ObTableSchema &schema, ObAiColumnMeta &meta)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc("AiColHiddenName");
  ObString state_name, request_name, spec_name;
  uint64_t max_used = schema.get_max_used_column_id();
  ObColumnSchemaV2 state_col, request_col, spec_col;
  if (OB_FAIL(build_hidden_column_names(meta.column_name_, state_name, request_name, spec_name, tmp_alloc))) {
  } else if (OB_FAIL(fill_hidden_column_schema(state_col, ++max_used, state_name, ObInt32Type, -1))) {
  } else if (OB_FAIL(fill_hidden_column_schema(request_col, ++max_used, request_name, ObVarcharType, 128))) {
  } else if (OB_FAIL(fill_hidden_column_schema(spec_col, ++max_used, spec_name, ObVarcharType, 4096))) {
  } else if (OB_FAIL(schema.add_column(state_col))) {
  } else if (OB_FAIL(schema.add_column(request_col))) {
  } else if (OB_FAIL(schema.add_column(spec_col))) {
  } else {
    schema.set_max_used_column_id(max_used);
    meta.state_column_id_ = state_col.get_column_id();
    meta.request_column_id_ = request_col.get_column_id();
    meta.result_spec_column_id_ = spec_col.get_column_id();
  }
  return ret;
}

int ObAiColumnUtil::append_hidden_columns_for_alter(schema::AlterTableSchema &schema, const ObAiColumnMeta &meta)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc("AiColHiddenName");
  ObString state_name, request_name, spec_name;
  AlterColumnSchema state_col, request_col, spec_col;
  // pre-assign placeholder ids so arg validation passes; the ddl service
  // re-assigns every ADD_COLUMN id sequentially from max_used later
  uint64_t max_used = schema.get_max_used_column_id();
  uint64_t add_cnt = 0;
  ObTableSchema::const_column_iterator iter = schema.column_begin();
  for (; iter != schema.column_end(); ++iter) {
    const ObColumnSchemaV2 *col = *iter;
    if (OB_NOT_NULL(col)) {
      const AlterColumnSchema *alter_col = static_cast<const AlterColumnSchema *>(col);
      if (OB_DDL_ADD_COLUMN == alter_col->alter_type_) {
        ++add_cnt;
      }
    }
  }
  uint64_t next_id = max_used + add_cnt + 1;
  if (OB_FAIL(build_hidden_column_names(meta.column_name_, state_name, request_name, spec_name, tmp_alloc))) {
  } else if (OB_FAIL(fill_hidden_column_schema(state_col, next_id++, state_name, ObInt32Type, -1))) {
  } else if (OB_FAIL(fill_hidden_column_schema(request_col, next_id++, request_name, ObVarcharType, 128))) {
  } else if (OB_FAIL(fill_hidden_column_schema(spec_col, next_id++, spec_name, ObVarcharType, 4096))) {
  } else if (OB_FAIL(set_add_column_type(state_col))) {
  } else if (OB_FAIL(set_add_column_type(request_col))) {
  } else if (OB_FAIL(set_add_column_type(spec_col))) {
  } else if (OB_FAIL(schema.add_alter_column(state_col, true /*need_allocate*/))) {
  } else if (OB_FAIL(schema.add_alter_column(request_col, true))) {
  } else if (OB_FAIL(schema.add_alter_column(spec_col, true))) {
  } else {
    schema.set_max_used_column_id(next_id - 1);
  }
  return ret;
}

int ObAiColumnUtil::fill_meta_ids_from_schema(const schema::ObTableSchema &schema, ObAiColumnMeta &meta)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc("AiColHiddenName");
  ObString state_name, request_name, spec_name;
  if (OB_FAIL(build_hidden_column_names(meta.column_name_, state_name, request_name, spec_name, tmp_alloc))) {
  } else {
    const ObColumnSchemaV2 *col = nullptr;
    if (OB_ISNULL(col = schema.get_column_schema(state_name))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ai hidden state column not found", K(ret), K(state_name));
    } else {
      meta.state_column_id_ = col->get_column_id();
    }
    if (OB_SUCC(ret) && OB_ISNULL(col = schema.get_column_schema(request_name))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ai hidden request column not found", K(ret), K(request_name));
    } else if (OB_SUCC(ret)) {
      meta.request_column_id_ = col->get_column_id();
    }
    if (OB_SUCC(ret) && OB_ISNULL(col = schema.get_column_schema(spec_name))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ai hidden spec column not found", K(ret), K(spec_name));
    } else if (OB_SUCC(ret)) {
      meta.result_spec_column_id_ = col->get_column_id();
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(schema.get_column_schema(meta.column_name_))) {
      meta.column_id_ = schema.get_column_schema(meta.column_name_)->get_column_id();
    }
  }
  return ret;
}

int ObAiColumnUtil::gen_request_id(ObIAllocator &allocator, ObString &request_id)
{
  int ret = OB_SUCCESS;
  static volatile int64_t seq = 0;
  const int64_t now_us = ObTimeUtility::current_time();
  const int64_t id = ATOMIC_FAA(&seq, 1);
  const int64_t rand_val = ObRandom::rand(0, INT64_MAX);
  char buf[128];
  int64_t pos = 0;
  if (OB_FAIL(databuff_printf(buf, sizeof(buf), "ai_req_%ld_%ld_%ld", now_us, id, rand_val))) {
  } else if (OB_FAIL(ob_write_string(allocator, ObString(buf), request_id))) {
  }
  return ret;
}

}  // end namespace share
}  // end namespace oceanbase

// ==================== DML hooks ====================

namespace oceanbase
{
namespace share
{

using namespace oceanbase::sql;

static int sql_escape_str(ObIAllocator &allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  ObStringBuffer buf(&allocator);
  if (OB_FAIL(buf.append("'"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < src.length(); ++i) {
      char c = src.ptr()[i];
      if ('\'' == c) {
        if (OB_FAIL(buf.append("''"))) {
        }
      } else {
        char one[2] = {c, '\0'};
        if (OB_FAIL(buf.append(one))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(buf.append("'"))) {
    } else if (OB_FAIL(ob_write_string(allocator, buf.string(), dst))) {
    }
  }
  return ret;
}

static int datum_to_sql_literal(ObIAllocator &allocator, const ObDatum &datum,
                                const ObObjMeta &meta, ObString &lit)
{
  int ret = OB_SUCCESS;
  ObObj obj;
  if (datum.is_null()) {
    ret = OB_ERR_UNEXPECTED;  // rowkey should not be null
  } else if (OB_FAIL(datum.to_obj(obj, meta))) {
  } else if (meta.is_int()) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld", obj.get_int());
    if (n <= 0) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      ret = ob_write_string(allocator, ObString(buf), lit);
    }
  } else if (ob_is_uint_tc(meta.get_type())) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lu", obj.get_uint64());
    if (n <= 0) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      ret = ob_write_string(allocator, ObString(buf), lit);
    }
  } else if (meta.is_varchar() || meta.is_char() || meta.is_text() || meta.is_json()) {
    ObString val = obj.get_string();
    ObString quoted;
    if (OB_FAIL(sql_escape_str(allocator, val, quoted))) {
    } else {
      lit = quoted;
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "unsupported rowkey column type for AI column");
  }
  return ret;
}

// Projectors map storage-column positions to cells of UPDATE's old+assign row.
// Split UPDATE, INSERT and DELETE rows already use the DML column order.
static int build_rowkey_pred(ObAiColumnOpCtx &ctx, const UIntFixedArray &cids,
                             const ObChunkDatumStore::StoredRow *stored_row,
                             ObIAllocator &allocator, ObString &pred,
                             const IntFixedArray *projector = nullptr)
{
  int ret = OB_SUCCESS;
  ObSqlString predicate;
  pred.reset();
  if (OB_ISNULL(stored_row) || OB_ISNULL(ctx.schema_)
      || ctx.rowkey_column_ids_.empty()
      || ctx.rowkey_column_ids_.count() != ctx.rowkey_column_names_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid row for AI column rowkey", K(ret), K(ctx.table_id_), KP(stored_row));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < ctx.rowkey_column_ids_.count(); ++i) {
    const uint64_t cid = ctx.rowkey_column_ids_.at(i);
    int64_t pos = -1;
    for (int64_t j = 0; j < cids.count(); ++j) {
      if (cids.at(j) == cid) {
        pos = j;
        break;
      }
    }
    if (pos >= 0 && OB_NOT_NULL(projector)) {
      pos = pos < projector->count() ? projector->at(pos) : -1;
    }
    const schema::ObColumnSchemaV2 *col = ctx.schema_->get_column_schema(cid);
    if (pos < 0 || pos >= stored_row->cnt_ || OB_ISNULL(col)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid AI column rowkey projection", K(ret), K(ctx.table_id_), K(cid),
               K(pos), "row_cells", stored_row->cnt_);
    } else {
      ObString lit;
      const ObString &name = ctx.rowkey_column_names_.at(i);
      if (OB_FAIL(datum_to_sql_literal(allocator, stored_row->cells()[pos],
                                      col->get_meta_type(), lit))) {
        LOG_WARN("fail to format AI column rowkey", K(ret), K(ctx.table_id_), K(cid));
      } else if (OB_FAIL(predicate.append_fmt("%s`%.*s` = %.*s", 0 == i ? "" : " AND ",
                     static_cast<int>(name.length()), name.ptr(),
                     static_cast<int>(lit.length()), lit.ptr()))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (predicate.empty()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ob_write_string(allocator, predicate.string(), pred))) {
    }
  }
  return ret;
}

static int append_insert_task_sql(ObAiColumnOpCtx &ctx, uint64_t table_id,
                                  const ObString &pred, const ObString &request_id,
                                  const ObSEArray<int64_t, 2> &meta_idxs,
                                  ObIAllocator &allocator, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  const int64_t now_us = ObTimeUtility::current_time();
  if (pred.empty() || request_id.empty() || meta_idxs.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid AI column task key", K(ret), K(table_id));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < meta_idxs.count(); ++i) {
    const ObAiColumnMeta &meta = ctx.metas_.at(meta_idxs.at(i));
    ObString esc_pred, esc_req, esc_col;
    if (OB_FAIL(sql_escape_str(allocator, pred, esc_pred))) {
    } else if (OB_FAIL(sql_escape_str(allocator, request_id, esc_req))) {
    } else if (OB_FAIL(sql_escape_str(allocator, meta.column_name_, esc_col))) {
    }
    if (OB_SUCC(ret)) {
      if (0 == sql.length()) {
        if (OB_FAIL(sql.assign_fmt(
                "INSERT INTO oceanbase.%s (table_id, rowkey_predicate, column_id, request_id,"
                " column_name, state, retry_count, eligible_ts) VALUES (%lu, %.*s, %lu, %.*s, %.*s, %ld, 0, %ld)",
                OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                static_cast<int>(esc_pred.length()), esc_pred.ptr(),
                static_cast<uint64_t>(meta.column_id_),
                static_cast<int>(esc_req.length()), esc_req.ptr(),
                static_cast<int>(esc_col.length()), esc_col.ptr(),
                static_cast<int64_t>(AI_TASK_PENDING), now_us))) {
        }
      } else if (OB_FAIL(sql.append_fmt(
                ", (%lu, %.*s, %lu, %.*s, %.*s, %ld, 0, %ld)",
                table_id,
                static_cast<int>(esc_pred.length()), esc_pred.ptr(),
                static_cast<uint64_t>(meta.column_id_),
                static_cast<int>(esc_req.length()), esc_req.ptr(),
                static_cast<int>(esc_col.length()), esc_col.ptr(),
                static_cast<int64_t>(AI_TASK_PENDING), now_us))) {
      }
    }
  }
  return ret;
}

static int build_delete_task_sql(uint64_t table_id, const ObString &pred,
                                 uint64_t column_id, ObIAllocator &allocator,
                                 ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  ObString escaped_pred;
  if (pred.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(sql_escape_str(allocator, pred, escaped_pred))) {
  } else if (OB_FAIL(sql.assign_fmt(
                 "DELETE FROM oceanbase.%s WHERE table_id = %lu AND rowkey_predicate = %.*s",
                 OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                 static_cast<int>(escaped_pred.length()), escaped_pred.ptr()))) {
  } else if (OB_INVALID_ID != column_id
             && OB_FAIL(sql.append_fmt(" AND column_id = %lu", column_id))) {
  }
  return ret;
}

int ObAiColumnUtil::flush_pending_sqls(sql::ObTableModifyOp &op)
{
  int ret = OB_SUCCESS;
  ObAiColumnOpCtx *ctx = static_cast<ObAiColumnOpCtx *>(op.get_ai_col_ctx());
  if (OB_NOT_NULL(ctx)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < ctx->pending_sqls_.count(); ++i) {
      ObSqlString terminated_sql;
      if (OB_FAIL(terminated_sql.assign(ctx->pending_sqls_.at(i)))) {
      } else if (OB_FAIL(exec_write_on_op(op, terminated_sql.ptr()))) {
      }
      if (OB_FAIL(ret)) {
        // Hook SQL changes results and request ownership in the user's
        // transaction. A failure must roll back the statement, not lose tasks.
        LOG_WARN("failed to execute AI column hook SQL", K(ret), K(ctx->table_id_),
                 "statement_index", i, "statement_count", ctx->pending_sqls_.count());
      }
    }
    ctx->clear_pending_sqls();
  }
  return ret;
}

int ObAiColumnUtil::exec_write_on_op(sql::ObTableModifyOp &op, const char *sql)
{
  int ret = OB_SUCCESS;
  // full FK-cascade pattern: save the outer statement session state
  // (begin_nested_session), flag the exec ctx so the nested statement's
  // end_stmt skips the outer start_stmt marker, execute, then restore.
  // Without the save/restore the nested statement's trans_result reset
  // discards the outer statement's writes at commit time.
  ObExecContext &exec_ctx = op.get_exec_ctx();
  const bool saved_cascading = exec_ctx.get_das_ctx().is_fk_cascading_;
  exec_ctx.get_das_ctx().is_fk_cascading_ = true;
  if (OB_FAIL(op.open_inner_conn())) {
  } else if (OB_FAIL(op.begin_nested_session(false))) {
  } else {
    int exec_ret = op.execute_write(sql);
    if (OB_SUCCESS != exec_ret) {
      ret = exec_ret;
    }
    int end_ret = op.end_nested_session();
    if (OB_SUCCESS != end_ret) {
      if (OB_SUCCESS == ret) {
        ret = end_ret;
      }
    }
  }
  exec_ctx.get_das_ctx().is_fk_cascading_ = saved_cascading;
  return ret;
}


int ObAiColumnOpCtx::add_pending_sql(const ObString &sql)
{
  int ret = OB_SUCCESS;
  ObString copy;
  if (sql.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ob_write_string(sql_allocator_, sql, copy))) {
  } else if (OB_FAIL(pending_sqls_.push_back(copy))) {
  }
  return ret;
}

void ObAiColumnOpCtx::clear_pending_sqls()
{
  pending_sqls_.reset();
  sql_allocator_.reset();
}
// resolve the real user table id: update/delete ctdefs carry a placeholder
// table id (-2); the tablet loc meta carries the real one
static uint64_t resolve_hook_table_id(const sql::ObDMLBaseCtDef &ctdef,
                                      const sql::ObDASTabletLoc *tablet_loc)
{
  uint64_t tid = ctdef.das_base_ctdef_.table_id_;
  if ((OB_INVALID_ID == tid || tid >= (1ULL << 62)) && OB_NOT_NULL(tablet_loc)
      && OB_NOT_NULL(tablet_loc->loc_meta_)) {
    tid = tablet_loc->loc_meta_->ref_table_id_;
  }
  return tid;
}

int ObAiColumnUtil::load_op_ctx(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                                uint64_t table_id_hint, ObAiColumnOpCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ctx = static_cast<ObAiColumnOpCtx *>(op.get_ai_col_ctx());
  const uint64_t table_id = OB_INVALID_ID != table_id_hint
      ? table_id_hint : ctdef.das_base_ctdef_.table_id_;
  if (OB_NOT_NULL(ctx)) {
    if (ctx->table_id_ != table_id) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI column context table mismatch", K(ret), K(table_id), K(ctx->table_id_));
    }
  } else if (OB_ISNULL(ctx = new (std::nothrow) ObAiColumnOpCtx())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ctx->table_id_ = table_id;
    ObSchemaGetterGuard *guard = nullptr;
    const schema::ObTableSchema *table_schema = nullptr;
    bool has_ai_columns = false;
    if (OB_INVALID_ID == table_id || table_id >= (1ULL << 62)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (is_inner_table(table_id)) {
      // Inner/bootstrap DML cannot contain AI columns. Avoid querying metadata
      // before its table exists, and avoid recursively processing task writes.
    } else if (OB_ISNULL(op.get_exec_ctx().get_sql_ctx())
        || OB_ISNULL(guard = op.get_exec_ctx().get_sql_ctx()->schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(guard->get_table_schema(table_id, table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
    } else {
      ctx->schema_ = table_schema;
      const int64_t prefix_len = strlen(OB_AI_REQUEST_COLUMN_PREFIX);
      for (schema::ObTableSchema::const_column_iterator it = table_schema->column_begin();
           !has_ai_columns && it != table_schema->column_end(); ++it) {
        const schema::ObColumnSchemaV2 *col = *it;
        if (OB_NOT_NULL(col) && col->is_hidden()) {
          const ObString &name = col->get_column_name_str();
          has_ai_columns = name.length() >= prefix_len
              && 0 == MEMCMP(name.ptr(), OB_AI_REQUEST_COLUMN_PREFIX, prefix_len);
        }
      }
    }
    if (OB_SUCC(ret) && has_ai_columns) {
      ObIAllocator &alloc = ctx->meta_allocator_;
      const schema::ObDatabaseSchema *db_schema = nullptr;
      if (OB_FAIL(select_metas_by_table(table_id, alloc, ctx->metas_))) {
      } else if (ctx->metas_.empty()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(guard->get_database_schema(table_schema->get_database_id(), db_schema))) {
      } else if (OB_ISNULL(db_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ob_write_string(alloc, db_schema->get_database_name(), ctx->db_name_))) {
      }
      const ObRowkeyInfo &rk_info = table_schema->get_rowkey_info();
      if (OB_SUCC(ret) && rk_info.get_size() <= 0) {
        ret = OB_ERR_UNEXPECTED;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < rk_info.get_size(); ++i) {
        const ObRowkeyColumn *rk_col = rk_info.get_column(i);
        const schema::ObColumnSchemaV2 *col = OB_ISNULL(rk_col) ? nullptr
            : table_schema->get_column_schema(rk_col->column_id_);
        ObString name;
        if (OB_ISNULL(col)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ob_write_string(alloc, col->get_column_name_str(), name))) {
        } else if (OB_FAIL(ctx->rowkey_column_ids_.push_back(col->get_column_id()))) {
        } else if (OB_FAIL(ctx->rowkey_column_names_.push_back(name))) {
        }
      }
      for (int64_t m = 0; OB_SUCC(ret) && m < ctx->metas_.count(); ++m) {
        ObSEArray<uint64_t, 4> dep_ids;
        const ObAiColumnMeta &meta = ctx->metas_.at(m);
        if (!meta.is_valid() || OB_ISNULL(table_schema->get_column_schema(meta.column_id_))) {
          ret = OB_ERR_UNEXPECTED;
        }
        for (int64_t a = 0; OB_SUCC(ret) && a < meta.expr_.arg_columns_.count(); ++a) {
          const schema::ObColumnSchemaV2 *col = table_schema->get_column_schema(
              meta.expr_.arg_columns_.at(a));
          if (OB_ISNULL(col)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(dep_ids.push_back(col->get_column_id()))) {
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(ctx->dep_column_ids_.push_back(dep_ids))) {
        }
      }
      ctx->has_ai_col_ = OB_SUCC(ret);
    }
    if (OB_SUCC(ret)) {
      // Publish only a fully initialized cache. Failed loads remain retryable.
      op.set_ai_col_ctx(ctx);
    } else {
      LOG_WARN("fail to load AI column context", K(ret), K(table_id));
      delete ctx;
      ctx = nullptr;
    }
  }
  return ret;
}

int ObAiColumnUtil::after_insert_row(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                                     const sql::ObDMLModifyRowNode &row,
                                     const sql::ObDASTabletLoc *tablet_loc)
{
  int ret = OB_SUCCESS;
  ObAiColumnOpCtx *ctx = nullptr;
  if (OB_FAIL(load_op_ctx(op, ctdef, resolve_hook_table_id(ctdef, tablet_loc), ctx))) {
  } else if (!ctx->has_ai_col_) {
    // nothing to do
  } else {
    ObArenaAllocator alloc("AiColHookRow");
    ObString pred;
    ObString request_id;
    if (OB_FAIL(build_rowkey_pred(*ctx, ctdef.column_ids_, row.new_row_, alloc, pred))) {
    } else if (OB_FAIL(gen_request_id(alloc, request_id))) {
    } else {
      ObSEArray<int64_t, 2> meta_idxs;
      for (int64_t m = 0; OB_SUCC(ret) && m < ctx->metas_.count(); ++m) {
        if (OB_FAIL(meta_idxs.push_back(m))) {
        }
      }
      ObSqlString sql;
      if (OB_SUCC(ret) && OB_FAIL(append_insert_task_sql(*ctx, ctx->table_id_, pred,
                                                         request_id, meta_idxs, alloc, sql))) {
      } else if (OB_SUCC(ret) && OB_FAIL(ctx->add_pending_sql(sql.string()))) {
      }
    }
  }
  return ret;
}

int ObAiColumnUtil::after_update_row(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                                     const sql::ObUpdRtDef &rtdef,
                                     const sql::ObDMLModifyRowNode &row,
                                     const sql::ObDASTabletLoc *tablet_loc)
{
  int ret = OB_SUCCESS;
  ObAiColumnOpCtx *ctx = nullptr;
  if (OB_FAIL(load_op_ctx(op, ctdef, resolve_hook_table_id(ctdef, tablet_loc), ctx))) {
  } else if (!ctx->has_ai_col_) {
    // nothing to do
  } else if (!rtdef.is_row_changed_) {
  } else {
    ObArenaAllocator alloc("AiColHookRow");
    ObString old_pred;
    ObString new_pred;
    const ObUpdCtDef &upd_ctdef = static_cast<const ObUpdCtDef &>(ctdef);
    const ObDASUpdCtDef &dupd_ctdef = upd_ctdef.dupd_ctdef_;
    const UIntFixedArray &updated_ids = dupd_ctdef.updated_column_ids_;
    if (OB_NOT_NULL(row.full_row_)) {
      // Immediate PK updates (including IGNORE) and non-PK updates only
      // materialize full_row_. Its front cells are old values, not new keys.
      if (OB_FAIL(build_rowkey_pred(*ctx, dupd_ctdef.column_ids_, row.full_row_, alloc,
                                   old_pred, &dupd_ctdef.old_row_projector_))) {
      } else if (OB_FAIL(build_rowkey_pred(*ctx, dupd_ctdef.column_ids_, row.full_row_, alloc,
                                          new_pred, &dupd_ctdef.new_row_projector_))) {
      }
    } else if (OB_FAIL(build_rowkey_pred(*ctx, ctdef.column_ids_, row.old_row_, alloc, old_pred))) {
    } else if (OB_FAIL(build_rowkey_pred(*ctx, ctdef.column_ids_, row.new_row_, alloc, new_pred))) {
    }
    const bool rowkey_changed = OB_SUCC(ret) && 0 != old_pred.compare(new_pred);
    if (OB_SUCC(ret) && rowkey_changed) {
      ObSqlString del_sql;
      if (OB_FAIL(build_delete_task_sql(ctx->table_id_, old_pred, OB_INVALID_ID, alloc, del_sql))) {
      } else if (OB_FAIL(ctx->add_pending_sql(del_sql.string()))) {
      }
    }
    // decide invalidation per meta
    if (OB_SUCC(ret)) {
      ObSEArray<int64_t, 2> meta_idxs;
      for (int64_t m = 0; OB_SUCC(ret) && m < ctx->metas_.count(); ++m) {
        bool invalidate = rowkey_changed;
        if (!invalidate) {
          const ObSEArray<uint64_t, 4> &dep_ids = ctx->dep_column_ids_.at(m);
          for (int64_t d = 0; !invalidate && d < dep_ids.count(); ++d) {
            for (int64_t u = 0; u < updated_ids.count(); ++u) {
              if (dep_ids.at(d) == updated_ids.at(u)) {
                invalidate = true;
                break;
              }
            }
          }
        }
        if (invalidate && OB_FAIL(meta_idxs.push_back(m))) {
        }
      }
      if (OB_SUCC(ret) && meta_idxs.count() > 0) {
        // NULL the AI result columns via a pending UPDATE in the user's
        // transaction: the buffered row does not carry the result columns
        // (only the SET columns do), so patching its cells is a no-op and
        // the visible invalidation must go through storage
        ObSqlString null_sql;
        ObString null_copy;
        if (OB_FAIL(null_sql.assign_fmt(
                "UPDATE `%.*s`.`%.*s` SET ",
                static_cast<int>(ctx->db_name_.length()), ctx->db_name_.ptr(),
                static_cast<int>(strlen(ctx->schema_->get_table_name())),
                ctx->schema_->get_table_name()))) {
        }
        for (int64_t mi = 0; OB_SUCC(ret) && mi < meta_idxs.count(); ++mi) {
          const ObAiColumnMeta &meta = ctx->metas_.at(meta_idxs.at(mi));
          if (OB_FAIL(null_sql.append_fmt("%s`%.*s` = NULL",
                  0 == mi ? "" : ",",
                  static_cast<int>(meta.column_name_.length()), meta.column_name_.ptr()))) {
          }
        }
        if (OB_SUCC(ret) && !new_pred.empty()) {
          if (OB_FAIL(null_sql.append_fmt(" WHERE %.*s",
                  static_cast<int>(new_pred.length()), new_pred.ptr()))) {
          } else if (OB_FAIL(ctx->add_pending_sql(null_sql.string()))) {
          }
        }
        // create the regeneration task group with a fresh request id
        ObString request_id;
        if (OB_FAIL(gen_request_id(alloc, request_id))) {
        } else {
          ObSqlString ins_sql;
          if (OB_FAIL(append_insert_task_sql(*ctx, ctx->table_id_, new_pred,
                                             request_id, meta_idxs, alloc, ins_sql))) {
          } else if (OB_FAIL(ctx->add_pending_sql(ins_sql.string()))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObAiColumnUtil::after_delete_row(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                                     const sql::ObDMLModifyRowNode &row,
                                     const sql::ObDASTabletLoc *tablet_loc)
{
  int ret = OB_SUCCESS;
  ObAiColumnOpCtx *ctx = nullptr;
  if (OB_FAIL(load_op_ctx(op, ctdef, resolve_hook_table_id(ctdef, tablet_loc), ctx))) {
  } else if (!ctx->has_ai_col_) {
    // nothing to do
  } else {
    ObString old_pred;
    ObArenaAllocator alloc("AiColHookRow");
    if (OB_FAIL(build_rowkey_pred(*ctx, ctdef.column_ids_, row.old_row_, alloc, old_pred))) {
      if (OB_ERR_UNEXPECTED == ret) {
        ret = OB_SUCCESS;
      }
    } else if (!old_pred.empty()) {
      ObSqlString del_sql;
      ObString esc_old;
      if (OB_FAIL(sql_escape_str(alloc, old_pred, esc_old))) {
      } else if (OB_FAIL(del_sql.assign_fmt(
                     "DELETE FROM oceanbase.%s WHERE table_id = %lu AND rowkey_predicate = %.*s",
                     OB_ALL_AI_COLUMN_TASK_TNAME, ctx->table_id_,
                     static_cast<int>(esc_old.length()), esc_old.ptr()))) {
      } else if (OB_FAIL(ctx->add_pending_sql(del_sql.string()))) {
      }
    }
  }
  return ret;
}

}  // end namespace share
}  // end namespace oceanbase


static int append_sql_escaped_util(ObStringBuffer &buf, const ObString &src)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < src.length(); ++i) {
    char c = src.ptr()[i];
    if ('\'' == c) {
      ret = buf.append("''");
    } else {
      char one[2] = {c, '\0'};
      ret = buf.append(one);
    }
  }
  return ret;
}

static int sql_quote_escaped(ObIAllocator &allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  ObStringBuffer buf(&allocator);
  if (OB_FAIL(buf.append("'"))) {
  } else if (OB_FAIL(append_sql_escaped_util(buf, src))) {
  } else if (OB_FAIL(buf.append("'"))) {
  } else {
    dst = buf.string();
  }
  return ret;
}

static int append_sql_escaped(ObStringBuffer &buf, const ObString &src)
{
  return append_sql_escaped_util(buf, src);
}

// ==================== strict read & RETRY ====================


namespace oceanbase
{
namespace share
{

static int resolve_table_names(uint64_t table_id, ObIAllocator &allocator,
                               ObString &db_name, ObString &table_name)
{
  int ret = OB_SUCCESS;
  schema::ObSchemaGetterGuard guard;
  const schema::ObTableSchema *table_schema = nullptr;
  const schema::ObDatabaseSchema *db_schema = nullptr;
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(guard.get_table_schema(table_id, table_schema)) || OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not found", K(ret), K(table_id));
  } else if (OB_FAIL(ob_write_string(allocator, table_schema->get_table_name(), table_name))) {
  } else if (OB_FAIL(guard.get_database_schema(table_schema->get_database_id(), db_schema))
             || OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = ob_write_string(allocator, db_schema->get_database_name(), db_name);
  }
  return ret;
}

// readiness: no row of the business table may hold NULL in the result column
static int strict_read_check_column(uint64_t table_id, uint64_t column_id,
                                    const ObString &column_name,
                                    ObIAllocator &allocator, bool &violated)
{
  int ret = OB_SUCCESS;
  violated = false;
  ObString db_name, table_name;
  ObSqlString sql;
  if (OB_FAIL(resolve_table_names(table_id, allocator, db_name, table_name))) {
  } else if (OB_FAIL(sql.assign_fmt(
                 "SELECT 1 FROM `%.*s`.`%.*s` WHERE `%.*s` IS NULL LIMIT 1",
                 static_cast<int>(db_name.length()), db_name.ptr(),
                 static_cast<int>(table_name.length()), table_name.ptr(),
                 static_cast<int>(column_name.length()), column_name.ptr()))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
        LOG_WARN("strict read readiness scan failed", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int next_ret = result->next();
        if (OB_ITER_END != next_ret) {
          violated = true;
        }
      }
    }
  }
  return ret;
}

int ObAiColumnUtil::check_strict_read(sql::ObSQLSessionInfo &session, sql::ObDMLStmt &stmt)
{
  int ret = OB_SUCCESS;
  bool strict = false;
  int tmp_ret = session.get_sys_variable(SYS_VAR_AI_COLUMN_STRICT_READ, strict);
  if (OB_SUCCESS != tmp_ret) {
    // sys vars may be unavailable (e.g. during bootstrap): treat as off
    LOG_WARN("fail to get ai_column_strict_read, treat as off", K(tmp_ret));
    ret = OB_SUCCESS;
  } else if (!strict) {
    // feature off
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObArenaAllocator arena("AiColStrict");
    ObSEArray<uint64_t, 4> checked_tables;
    ObSEArray<ObSEArray<ObAiColumnMeta, 2>, 4> metas_cache;
    ObSEArray<sql::ObDMLStmt *, 8> work;
    if (OB_FAIL(work.push_back(&stmt))) {
    }
    while (OB_SUCC(ret) && work.count() > 0) {
      sql::ObDMLStmt *cur = work.at(work.count() - 1);
      work.pop_back();
      if (OB_ISNULL(cur)) {
        // skip
      } else {
        ObSEArray<sql::ObSelectStmt *, 4> children;
        if (OB_FAIL(cur->get_subquery_stmts(children))) {
          LOG_WARN("fail to get subquery stmts", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < children.count(); ++i) {
            if (OB_FAIL(work.push_back(children.at(i)))) {
            }
          }
        }
        if (OB_SUCC(ret)) {
          ObSEArray<sql::ObColumnRefRawExpr *, 16> cols;
          if (OB_FAIL(cur->get_column_exprs(cols))) {
            LOG_WARN("fail to get column exprs", K(ret));
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < cols.count(); ++i) {
            sql::ObColumnRefRawExpr *col = cols.at(i);
            uint64_t cid = col->get_column_id();
            // col->get_table_id() is the TableItem id local to this query
            // block; map it to the real schema table id via the table item
            uint64_t tid = OB_INVALID_ID;
            sql::TableItem *titem = cur->get_table_item_by_id(col->get_table_id());
            if (OB_NOT_NULL(titem) && titem->is_basic_table()) {
              tid = titem->ref_id_;
            }
            if (OB_INVALID_ID == tid || OB_INVALID_ID == cid) {
              continue;
            }
            int64_t cached = -1;
            for (int64_t t = 0; t < checked_tables.count(); ++t) {
              if (checked_tables.at(t) == tid) {
                cached = t;
                break;
              }
            }
            if (cached < 0) {
              ObSEArray<ObAiColumnMeta, 2> metas;
              int load_ret = select_metas_by_table(tid, arena, metas);
              if (OB_SUCCESS != load_ret) {
                // meta table not usable (e.g. during bootstrap): no AI columns
                LOG_WARN("fail to load metas in strict check, treat as none",
                         K(load_ret), K(tid));
                metas.reset();
                ret = OB_SUCCESS;
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(checked_tables.push_back(tid))) {
                } else if (OB_FAIL(metas_cache.push_back(metas))) {
                } else {
                  cached = checked_tables.count() - 1;
                }
              }
            }
            if (OB_SUCC(ret) && cached >= 0) {
              for (int64_t m = 0; OB_SUCC(ret) && m < metas_cache.at(cached).count(); ++m) {
                const ObAiColumnMeta &meta = metas_cache.at(cached).at(m);
                if (meta.column_id_ != cid) {
                  continue;
                }
                bool violated = false;
                if (OB_FAIL(strict_read_check_column(tid, cid, meta.column_name_, arena,
                                                     violated))) {
                } else if (violated) {
                  ret = OB_AI_COLUMN_NOT_READY;
                  LOG_USER_ERROR(OB_AI_COLUMN_NOT_READY, tid, cid);
                }
                break;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

// ---- RETRY ----

static int retry_translate_where(void *where_node, const ObString &result_col_name,
                                 ObStringBuffer &sql, ObIAllocator &allocator);

static int retry_translate_operand(ParseNode *node, const ObString &result_col_name,
                                   ObStringBuffer &sql, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_COLUMN_REF == node->type_) {
    if (node->num_child_ < 3 || OB_ISNULL(node->children_[2])
        || OB_ISNULL(node->children_[2]->str_value_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ParseNode *name_node = node->children_[2];
      ObString col(name_node->str_len_, name_node->str_value_);
      if (0 == col.case_compare("status") && !name_node->is_input_quoted_) {
        // pseudo column outside a comparison: not supported
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "the status pseudo column only supports = comparisons");
      } else if (OB_FAIL(sql.append("`"))) {
      } else if (OB_FAIL(sql.append(col))) {
      } else if (OB_FAIL(sql.append("`"))) {
      }
    }
  } else if (T_VARCHAR == node->type_) {
    if (OB_ISNULL(node->str_value_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObString lit(node->str_len_, node->str_value_);
      ObString quoted;
      if (OB_FAIL(sql_quote_escaped(allocator, lit, quoted))) {
      } else if (OB_FAIL(sql.append(quoted))) {
      }
    }
  } else if (T_NUMBER == node->type_ || T_INT == node->type_) {
    if (T_INT == node->type_) {
      char buf[32];
      int n = snprintf(buf, sizeof(buf), "%ld", node->value_);
      if (n <= 0) {
        ret = OB_SIZE_OVERFLOW;
      } else if (OB_FAIL(sql.append(buf, static_cast<uint64_t>(n)))) {
      }
    } else if (OB_NOT_NULL(node->str_value_)) {
      ObString num(node->str_len_, node->str_value_);
      if (OB_FAIL(sql.append(num))) {
      }
    }
  } else if (T_NULL == node->type_) {
    if (OB_FAIL(sql.append("NULL"))) {
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "unsupported expression in RETRY WHERE");
  }
  return ret;
}

static int retry_translate_where(void *where_ptr, const ObString &result_col_name,
                                 ObStringBuffer &sql, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ParseNode *node = static_cast<ParseNode *>(where_ptr);
  if (OB_ISNULL(node)) {
    // no where: all rows
  } else if (T_WHERE_CLAUSE == node->type_) {
    if (node->num_child_ < 1 || OB_ISNULL(node->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = retry_translate_where(node->children_[0], result_col_name, sql, allocator);
    }
  } else if (T_OP_AND == node->type_ || T_OP_OR == node->type_) {
    if (2 != node->num_child_ || OB_ISNULL(node->children_[0]) || OB_ISNULL(node->children_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(sql.append("("))) {
    } else if (OB_FAIL(retry_translate_where(node->children_[0], result_col_name, sql, allocator))) {
    } else if (OB_FAIL(sql.append(T_OP_AND == node->type_ ? " AND " : " OR "))) {
    } else if (OB_FAIL(retry_translate_where(node->children_[1], result_col_name, sql, allocator))) {
    } else if (OB_FAIL(sql.append(")"))) {
    }
  } else if (T_OP_NOT == node->type_) {
    if (1 != node->num_child_ || OB_ISNULL(node->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(sql.append("(NOT "))) {
    } else if (OB_FAIL(retry_translate_where(node->children_[0], result_col_name, sql, allocator))) {
    } else if (OB_FAIL(sql.append(")"))) {
    }
  } else if (T_OP_EQ == node->type_ && 2 == node->num_child_
             && OB_NOT_NULL(node->children_[0]) && OB_NOT_NULL(node->children_[1])
             && ((T_COLUMN_REF == node->children_[0]->type_
                  && T_VARCHAR == node->children_[1]->type_)
                 || (T_COLUMN_REF == node->children_[1]->type_
                     && T_VARCHAR == node->children_[0]->type_))) {
    // status pseudo column equality
    ParseNode *col_node = T_COLUMN_REF == node->children_[0]->type_
        ? node->children_[0] : node->children_[1];
    ParseNode *val_node = T_COLUMN_REF == node->children_[0]->type_
        ? node->children_[1] : node->children_[0];
    ParseNode *name_node = col_node->children_[2];
    ObString col(name_node->str_len_, name_node->str_value_);
    ObString val(val_node->str_len_, val_node->str_value_);
    if (0 != col.case_compare("status")) {
      // ordinary column = string
      if (OB_FAIL(retry_translate_operand(col_node, result_col_name, sql, allocator))) {
      } else if (OB_FAIL(sql.append(" = "))) {
      } else if (OB_FAIL(retry_translate_operand(val_node, result_col_name, sql, allocator))) {
      }
    } else if (name_node->is_input_quoted_) {
      // backticked `status`: physical column
      if (OB_FAIL(retry_translate_operand(col_node, result_col_name, sql, allocator))) {
      } else if (OB_FAIL(sql.append(" = "))) {
      } else if (OB_FAIL(retry_translate_operand(val_node, result_col_name, sql, allocator))) {
      }
    } else if (0 == val.case_compare("READY")) {
      if (OB_FAIL(sql.append("(`"))) {
      } else if (OB_FAIL(sql.append(result_col_name))) {
      } else if (OB_FAIL(sql.append("` IS NOT NULL)"))) {
      }
    } else if (0 == val.case_compare("FAILED") || 0 == val.case_compare("PENDING")) {
      if (OB_FAIL(sql.append("(`"))) {
      } else if (OB_FAIL(sql.append(result_col_name))) {
      } else if (OB_FAIL(sql.append("` IS NULL)"))) {
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "unsupported status value in RETRY WHERE");
    }
  } else if (T_OP_EQ == node->type_ || T_OP_NE == node->type_
             || T_OP_LT == node->type_ || T_OP_LE == node->type_
             || T_OP_GT == node->type_ || T_OP_GE == node->type_) {
    if (2 != node->num_child_ || OB_ISNULL(node->children_[0]) || OB_ISNULL(node->children_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(retry_translate_operand(node->children_[0], result_col_name, sql, allocator))) {
    } else if (OB_FAIL(sql.append(T_OP_EQ == node->type_ ? " = " : T_OP_NE == node->type_ ? " != "
                          : T_OP_LT == node->type_ ? " < " : T_OP_LE == node->type_ ? " <= "
                          : T_OP_GT == node->type_ ? " > " : " >= "))) {
    } else if (OB_FAIL(retry_translate_operand(node->children_[1], result_col_name, sql, allocator))) {
    }
  } else if (T_OP_IS == node->type_) {
    if (2 != node->num_child_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(retry_translate_operand(node->children_[0], result_col_name, sql, allocator))) {
    } else if (OB_FAIL(sql.append(node->value_ ? " IS NOT NULL" : " IS NULL"))) {
    }
  } else if (T_OP_IN == node->type_) {
    if (2 != node->num_child_ || OB_ISNULL(node->children_[0]) || OB_ISNULL(node->children_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(retry_translate_operand(node->children_[0], result_col_name, sql, allocator))) {
    } else if (OB_FAIL(sql.append(" IN ("))) {
    } else {
      ParseNode *list = node->children_[1];
      for (int64_t i = 0; OB_SUCC(ret) && i < list->num_child_; ++i) {
        if (i > 0 && OB_FAIL(sql.append(", "))) {
        } else if (OB_FAIL(retry_translate_operand(list->children_[i], result_col_name, sql,
                                                   allocator))) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(sql.append(")"))) {
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "unsupported expression in RETRY WHERE");
  }
  return ret;
}

int ObAiColumnUtil::exec_ai_retry(uint64_t table_id, const ObString &column_name,
                                  void *where_node)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator arena("AiColRetry");
  int64_t affected_rows = 0;
  ObString db_name, table_name;
  ObSEArray<ObAiColumnMeta, 2> metas;
  const ObAiColumnMeta *target = nullptr;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(resolve_table_names(table_id, arena, db_name, table_name))) {
  } else if (OB_FAIL(select_metas_by_table(table_id, arena, metas))) {
  } else {
    for (int64_t m = 0; nullptr == target && m < metas.count(); ++m) {
      if (0 == metas.at(m).column_name_.case_compare(column_name)) {
        target = &metas.at(m);
      }
    }
    if (OB_ISNULL(target)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "retrying a column that is not an AI column");
    }
  }
  // translate the where clause
  ObStringBuffer where_sql(&arena);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(retry_translate_where(where_node, target->column_name_, where_sql, arena))) {
    } else {
    }
  }
  // select rowkeys of the matching rows
  ObSEArray<ObString, 16> rowkey_preds;
  if (OB_SUCC(ret)) {
    schema::ObSchemaGetterGuard guard;
    const schema::ObTableSchema *table_schema = nullptr;
    if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(guard))) {
    } else if (OB_FAIL(guard.get_table_schema(table_id, table_schema)) || OB_ISNULL(table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
    } else {
      const ObRowkeyInfo &rk_info = table_schema->get_rowkey_info();
      ObSqlString sql;
      if (OB_FAIL(sql.assign_fmt("SELECT "))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < rk_info.get_size(); ++i) {
          const ObRowkeyColumn *rk_col = rk_info.get_column(i);
          const schema::ObColumnSchemaV2 *col = table_schema->get_column_schema(rk_col->column_id_);
          if (OB_ISNULL(col)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(sql.append_fmt("%s`%.*s` AS rk%ld", i == 0 ? "" : ",",
                                            static_cast<int>(col->get_column_name_str().length()),
                                            col->get_column_name_str().ptr(), i))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql.append_fmt(" FROM `%.*s`.`%.*s`",
                static_cast<int>(db_name.length()), db_name.ptr(),
                static_cast<int>(table_name.length()), table_name.ptr()))) {
        } else if (where_sql.length() > 0
                   && OB_FAIL(sql.append_fmt(" WHERE %.*s",
                          static_cast<int>(where_sql.length()), where_sql.ptr()))) {
        }
      }
      if (OB_SUCC(ret)) {
        SMART_VAR(ObMySQLProxy::MySQLResult, res) {
          ObMySQLResult *result = nullptr;
          if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
          } else if (OB_ISNULL(result = res.get_result())) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            int tmp_ret = OB_SUCCESS;
            while (OB_SUCCESS == (tmp_ret = result->next())) {
              ObStringBuffer pred(&arena);
              bool pred_ok = true;
              for (int64_t i = 0; pred_ok && i < rk_info.get_size(); ++i) {
                char alias[16];
                snprintf(alias, sizeof(alias), "rk%ld", i);
                const ObRowkeyColumn *rk_col = rk_info.get_column(i);
                const schema::ObColumnSchemaV2 *col =
                    table_schema->get_column_schema(rk_col->column_id_);
                if (i > 0 && OB_FAIL(pred.append(" AND "))) {
                } else if (OB_FAIL(pred.append("`"))) {
                } else if (OB_FAIL(pred.append(col->get_column_name_str()))) {
                } else if (OB_FAIL(pred.append("` = "))) {
                } else {
                  ObObjMeta meta = col->get_meta_type();
                  if (meta.is_int()) {
                    int64_t v = 0;
                    if (OB_FAIL(result->get_int(alias, v))) {
                    } else {
                      char buf[32];
                      int n = snprintf(buf, sizeof(buf), "%ld", v);
                      if (n <= 0 || OB_FAIL(pred.append(buf, static_cast<uint64_t>(n)))) {
                      }
                    }
                  } else if (ob_is_uint_tc(meta.get_type())) {
                    uint64_t v = 0;
                    if (OB_FAIL(result->get_uint(alias, v))) {
                    } else {
                      char buf[32];
                      int n = snprintf(buf, sizeof(buf), "%lu", v);
                      if (n <= 0 || OB_FAIL(pred.append(buf, static_cast<uint64_t>(n)))) {
                      }
                    }
                  } else {
                    ObString v;
                    if (OB_FAIL(result->get_varchar(alias, v))) {
                    } else {
                      ObString quoted;
                      if (OB_FAIL(sql_quote_escaped(arena, v, quoted))) {
                      } else if (OB_FAIL(pred.append(quoted))) {
                      }
                    }
                  }
                }
              }
              if (OB_SUCC(ret) && pred_ok) {
                ObString pred_str = pred.string();
                ObString copy;
                if (OB_FAIL(ob_write_string(arena, pred_str, copy))) {
                } else if (OB_FAIL(rowkey_preds.push_back(copy))) {
                }
              }
            }
            if (OB_ITER_END != tmp_ret) {
              ret = tmp_ret;
            }
          }
        }
      }
    }
  }
  // invalidate results + recreate tasks; direct autocommit writes because
  // transactional writes from the ALTER TABLE statement context are swallowed
  // by the DDL transaction machinery
  if (OB_SUCC(ret) && rowkey_preds.count() > 0) {
    ObSqlString upd_sql;
    if (OB_FAIL(upd_sql.assign_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = NULL",
            static_cast<int>(db_name.length()), db_name.ptr(),
            static_cast<int>(table_name.length()), table_name.ptr(),
            static_cast<int>(target->column_name_.length()), target->column_name_.ptr()))) {
    } else if (where_sql.length() > 0
               && OB_FAIL(upd_sql.append_fmt(" WHERE %.*s",
                      static_cast<int>(where_sql.length()), where_sql.ptr()))) {
    } else if (OB_FAIL(GCTX.sql_proxy_->write(upd_sql.ptr(), affected_rows))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_preds.count(); ++i) {
      const ObString &pred = rowkey_preds.at(i);
      ObString request_id;
      if (OB_FAIL(gen_request_id(arena, request_id))) {
      } else {
        ObSqlString task_sql;
        ObStringBuffer pb(&arena), qb(&arena), cb(&arena);
        if (OB_FAIL(append_sql_escaped(pb, pred))) {
        } else if (OB_FAIL(append_sql_escaped(qb, request_id))) {
        } else if (OB_FAIL(append_sql_escaped(cb, target->column_name_))) {
        } else if (OB_FAIL(task_sql.assign_fmt(
                "INSERT INTO oceanbase.%s (table_id, rowkey_predicate, column_id, request_id,"
                " column_name, state, retry_count, eligible_ts) VALUES (%lu, '%.*s', %lu, '%.*s', '%.*s', 1, 0, %ld)",
                OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                static_cast<int>(pb.length()), pb.ptr(),
                target->column_id_,
                static_cast<int>(qb.length()), qb.ptr(),
                static_cast<int>(cb.length()), cb.ptr(),
                ObTimeUtility::current_time()))) {
        } else if (OB_FAIL(GCTX.sql_proxy_->write(task_sql.ptr(), affected_rows))) {
          LOG_WARN("retry task insert failed", K(ret), K(task_sql));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    // make sure the worker is running
    int tmp = ObAiColumnWorker::get_instance().ensure_started();
    if (OB_SUCCESS != tmp) {
      LOG_WARN("fail to ensure worker started", K(tmp));
    }
  }
  return ret;
}

}  // end namespace share
}  // end namespace oceanbase
