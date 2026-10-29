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

#ifndef OCEANBASE_OBSERVER_AI_COLUMN_OB_AI_COLUMN_UTIL_H_
#define OCEANBASE_OBSERVER_AI_COLUMN_OB_AI_COLUMN_UTIL_H_

#include "share/ai_column/ob_ai_column_struct.h"

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
class ObISQLClient;
}
namespace sql
{
class ObTableModifyOp;
class ObDMLBaseCtDef;
struct ObDMLModifyRowNode;
class ObUpdRtDef;
class ObSQLSessionInfo;
class ObDMLStmt;
class ObExecContext;
struct ObDASTabletLoc;
}
namespace share
{
namespace schema
{
class ObTableSchema;
class AlterTableSchema;
}

// per-execution cached ai-column info of one business table (owned by the
// exec ctx allocator; referenced from ObTableModifyOp via opaque pointer)
struct ObAiColumnOpCtx
{
  ObAiColumnOpCtx() : has_ai_col_(false), table_id_(common::OB_INVALID_ID), schema_(nullptr) {}
  bool has_ai_col_;
  uint64_t table_id_;
  // the business table's database name (for fully-qualified hook SQL)
  common::ObString db_name_;
  const schema::ObTableSchema *schema_;
  ObSEArray<ObAiColumnMeta, 2> metas_;
  // per meta: dependency column ids resolved against the table schema
  ObSEArray<ObSEArray<uint64_t, 4>, 2> dep_column_ids_;
  ObSEArray<uint64_t, 4> rowkey_column_ids_;
  ObSEArray<ObString, 4> rowkey_column_names_;
  // task-table statements collected while rows stream through the operator;
  // nested SQL is only safe after the DAS tasks ran, so they flush later.
  // each collected string lives in its own heap arena owned by this ctx
  // (flush_pending_sqls destroys them), immune to allocator recycling
  ObSEArray<ObString, 8> pending_sqls_;
  ObSEArray<ObArenaAllocator *, 8> sql_arenas_;
  ObArenaAllocator &acquire_sql_arena();
};


// SQL-level helpers around __all_ai_column / __all_ai_column_task.
// All functions run on the global inner sql proxy (GCTX.sql_proxy_).
class ObAiColumnUtil
{
public:
  // insert one row per meta into __all_ai_column (meta.normalized_expr_ must be filled)
  static int insert_metas(const common::ObIArray<ObAiColumnMeta> &metas, uint64_t table_id);
  static int insert_meta(const ObAiColumnMeta &meta, uint64_t table_id,
                         common::ObMySQLTransaction &trans);
  static int delete_metas_by_table(uint64_t table_id, common::ObMySQLTransaction &trans);
  // delete metas of a table after drop; standalone transaction
  static int remove_metas_of_table(uint64_t table_id);

  // read all ai column metas of a table; strings are deep-copied into allocator
  static int select_metas_by_table(uint64_t table_id, common::ObIAllocator &allocator,
                                   common::ObIArray<ObAiColumnMeta> &metas);

  // request ids group the task rows of one business row for one invalidation event
  static int gen_request_id(common::ObIAllocator &allocator, common::ObString &request_id);

  // CREATE TABLE path: append the 3 hidden columns with fresh column ids and fill
  // meta.{state,request,result_spec}_column_id accordingly.
  static int append_hidden_columns_for_create(schema::ObTableSchema &schema, ObAiColumnMeta &meta);
  // ALTER TABLE ADD COLUMN path: append 3 hidden add-column schemas (ids are
  // assigned later by the ddl service).
  static int append_hidden_columns_for_alter(schema::AlterTableSchema &schema, const ObAiColumnMeta &meta);
  // fill meta hidden column ids by looking up the deterministic hidden column names
  static int fill_meta_ids_from_schema(const schema::ObTableSchema &schema, ObAiColumnMeta &meta);

  // ---- DML hooks (called from the table modify operators, primary ctdef only) ----
  // insert: create one request group of task rows for the new row.
  // tablet_loc carries the real table id: DML ctdefs only have a placeholder
  static int after_insert_row(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                              const sql::ObDMLModifyRowNode &row,
                              const sql::ObDASTabletLoc *tablet_loc);
  // update: drop tasks of the old rowkey; invalidate + re-task when dependency
  // columns (or the rowkey) changed; null the result cells of the buffered new row.
  // tablet_loc carries the real table id: the update ctdef only has a placeholder
  static int after_update_row(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                              const sql::ObUpdRtDef &rtdef, const sql::ObDMLModifyRowNode &row,
                              const sql::ObDASTabletLoc *tablet_loc);
  // delete: drop tasks of the deleted row (ctdef table id may be a placeholder)
  static int after_delete_row(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                              const sql::ObDMLModifyRowNode &row,
                              const sql::ObDASTabletLoc *tablet_loc);

  static int load_op_ctx(sql::ObTableModifyOp &op, const sql::ObDMLBaseCtDef &ctdef,
                         uint64_t table_id_hint, ObAiColumnOpCtx *&ctx);
  // run the task-table statements the hooks collected, on the op's
  // user-transaction inner connection; call once per statement after the
  // DAS tasks executed (nested SQL is unsafe before that)
  static int flush_pending_sqls(sql::ObTableModifyOp &op);
  // run one write sql on the op's user-transaction inner connection
  static int exec_write_on_op(sql::ObTableModifyOp &op, const char *sql);

  // ---- strict read (ai_column_strict_read session var) ----
  // reject statements referencing an AI column that is not fully ready (11120)
  static int check_strict_read(sql::ObSQLSessionInfo &session, sql::ObDMLStmt &stmt);

  // ---- ALTER TABLE ... RETRY AI COLUMN ----
  // where_node: raw T_WHERE_CLAUSE parse node (may contain the status pseudo column)
  static int exec_ai_retry(uint64_t table_id, const common::ObString &column_name,
                           void *where_node);
};

}  // end namespace share
}  // end namespace oceanbase

#endif  // OCEANBASE_OBSERVER_AI_COLUMN_OB_AI_COLUMN_UTIL_H_
