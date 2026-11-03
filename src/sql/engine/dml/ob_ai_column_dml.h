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

#pragma once

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/string/ob_sql_string.h"
#include "lib/string/ob_string.h"
#include "share/schema/ob_ai_column_spec.h"
#include "sql/engine/dml/ob_dml_ctx_define.h"

namespace oceanbase
{
namespace sql
{

class ObTableModifyOp;
class ObTableInsertOp;
class ObTableUpdateOp;
class ObTableDeleteOp;
struct ObInsCtDef;
struct ObUpdCtDef;
struct ObDelCtDef;
struct ObInsRtDef;
struct ObUpdRtDef;
struct ObDelRtDef;

// The description of one AI column of a table.  The three hidden companion
// columns are located by their name, the ids kept in the spec are not
// reliable because the ALTER path may leave them unfilled.
struct ObAiColumnMeta
{
public:
  ObAiColumnMeta()
      : table_id_(OB_INVALID_ID),
        result_column_id_(OB_INVALID_ID),
        state_column_id_(OB_INVALID_ID),
        request_column_id_(OB_INVALID_ID),
        spec_column_id_(OB_INVALID_ID),
        def_version_(0),
        spec_()
  {}
  ~ObAiColumnMeta() {}

  uint64_t table_id_;
  uint64_t result_column_id_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t spec_column_id_;
  int64_t def_version_;
  common::ObString result_column_name_;
  common::ObString state_column_name_;
  common::ObString request_column_name_;
  common::ObString spec_column_name_;
  share::schema::ObAiColumnSpec spec_;
};

// The pending work of the AI column DML hook of one statement.  The rows are
// invalidated at the end of the statement, so that the business DML and the
// task bookkeeping share the life cycle of the business transaction.
class ObAiColumnDmlState
{
public:
  // One row that has to be invalidated, all the AI columns of the row share
  // one request id.
  struct PendingRow
  {
    uint64_t table_id_;
    int64_t def_version_;
    common::ObString db_name_;
    common::ObString table_name_;
    common::ObString rowkey_predicate_;
    common::ObString request_id_;
    common::ObSEArray<uint64_t, 4> column_ids_;

    PendingRow() : table_id_(OB_INVALID_ID), def_version_(0) {}
  };
  // One row that has been deleted, the pending tasks of the row are removed.
  struct PendingDelete
  {
    uint64_t table_id_;
    common::ObString rowkey_predicate_;
  };

  ObAiColumnDmlState();
  ~ObAiColumnDmlState();

  bool has_pending_work() const
  {
    return !pending_rows_.empty() || !pending_deletes_.empty();
  }
  void reset();

  common::ObIAllocator &get_allocator() { return allocator_; }

public:
  common::ObSEArray<PendingRow, 8> pending_rows_;
  common::ObSEArray<PendingDelete, 8> pending_deletes_;

private:
  common::ObArenaAllocator allocator_;
};

// The DML hook of the asynchronous AI column feature.
//
// The hook is called by the insert, update and delete operators.  It decides
// whether the AI result of a row becomes stale and records the work that has
// to be done when the statement ends.  The result column of a stale row is
// reset and the pending task is created with nested SQL, so that the task
// rows are visible inside the business transaction and disappear with it.
class ObAiColumnDml
{
public:
  // The states of the hidden state column.
  static const char *const STATE_PENDING;
  static const char *const STATE_READY;
  static const char *const STATE_FAILED;

  // Called after the row of an INSERT has been processed.
  static int on_insert_row(ObTableInsertOp &op, const ObInsCtDef &ctdef, ObInsRtDef &rtdef);
  // Called after the row of an UPDATE has been processed.
  static int on_update_row(ObTableUpdateOp &op, const ObUpdCtDef &ctdef, ObUpdRtDef &rtdef);
  // Called after the row of a DELETE has been processed.
  static int on_delete_row(ObTableDeleteOp &op, const ObDelCtDef &ctdef, ObDelRtDef &rtdef);
  // Executes the pending work of the statement.
  static int flush(ObTableModifyOp &op);

  // Generates a request id that identifies one change of one row.
  static int gen_request_id(common::ObIAllocator &alloc, common::ObString &request_id);
  // Builds the name of a hidden companion column of an AI column.  The name
  // is deep copied into `alloc`.
  static int build_hidden_col_name(const common::ObString &col_name,
                                   const char *prefix,
                                   common::ObIAllocator &alloc,
                                   common::ObString &name);
  // Appends the SQL literal of a datum to the buffer.
  static int append_sql_literal(common::ObSqlString &buf,
                                 const common::ObObj &obj,
                                 common::ObCollationType coll_type);

private:
  // Collects the AI columns of the table that is modified by the statement.
  static int collect_ai_columns(ObTableModifyOp &op,
                                 const ObDMLBaseCtDef &ctdef,
                                 common::ObSEArray<ObAiColumnMeta, 4> &ai_columns);
  // Collects the AI columns of one table schema, the column names are deep
  // copied into the allocator of the execution context.
  static int collect_ai_columns_by_schema(
      ObTableModifyOp &op,
      const share::schema::ObTableSchema &table_schema,
      common::ObSEArray<ObAiColumnMeta, 4> &ai_columns);
  // Rejects the write of an AI result column by an ordinary user session.
  static int check_write_protection(ObTableModifyOp &op,
                                    const ObDMLBaseCtDef &ctdef,
                                    const common::ObSEArray<ObAiColumnMeta, 4> &ai_columns);
  // Builds the predicate that identifies the row, e.g. "`id` = 2".
  static int build_rowkey_predicate(ObTableModifyOp &op,
                                    const ExprFixedArray &row,
                                    const UIntFixedArray &column_ids,
                                    const share::schema::ObTableSchema &table_schema,
                                    common::ObIAllocator &alloc,
                                    common::ObString &predicate);
  // True when the value of the column changed between the old and the new row.
  static int column_value_changed(ObTableModifyOp &op,
                                   const ObDMLBaseCtDef &ctdef,
                                   uint64_t column_id,
                                   bool &changed);
  // True when the row key of the row changed.
  static int rowkey_changed(ObTableModifyOp &op,
                             const ObDMLBaseCtDef &ctdef,
                             const share::schema::ObTableSchema &table_schema,
                             bool &changed);
  // Records the invalidation of one row.
  static int add_pending_row(ObTableModifyOp &op,
                              const share::schema::ObTableSchema &table_schema,
                              const common::ObString &db_name,
                              const common::ObString &rowkey_predicate,
                              const common::ObSEArray<uint64_t, 4> &column_ids,
                              common::ObString &request_id);
  // Creates the task rows of the pending invalidations.
  static int create_tasks(ObTableModifyOp &op,
                          const ObAiColumnDmlState::PendingRow &pending_row,
                          const common::ObSEArray<ObAiColumnMeta, 4> &ai_columns);
  // Resets the result column of the row and records the new request id.
  static int invalidate_row(ObTableModifyOp &op,
                            const ObAiColumnDmlState::PendingRow &pending_row,
                            const common::ObSEArray<ObAiColumnMeta, 4> &ai_columns);
  // Removes the pending tasks of the deleted rows.
  static int remove_tasks(ObTableModifyOp &op,
                          const common::ObSEArray<ObAiColumnDmlState::PendingDelete, 8> &pending_deletes);
};

} // namespace sql
} // namespace oceanbase
