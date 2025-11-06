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

#ifndef OBDEV_SRC_SQL_ENGINE_DML_OB_FOREIGN_KEY_CHECKER_H_
#define OBDEV_SRC_SQL_ENGINE_DML_OB_FOREIGN_KEY_CHECKER_H_
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/das/ob_das_scan_op.h"
#include "ob_dml_ctx_define.h"

namespace oceanbase
{
namespace sql
{

class ObForeignKeyChecker
{
public:
  ObForeignKeyChecker(ObEvalCtx &eval_ctx, const ObForeignKeyCheckerCtdef &checker_ctdef)
    : eval_ctx_(eval_ctx),
      das_scan_rtdef_(),
      checker_ctdef_(checker_ctdef),
      das_ref_(eval_ctx, eval_ctx.exec_ctx_),
      table_loc_(nullptr),
      local_tablet_loc_(nullptr),
      se_rowkey_dist_ctx_(nullptr),
      table_rowkey_(),
      batch_distinct_fk_cnt_(0),
      allocator_(nullptr)
  {
  }

  ~ObForeignKeyChecker() {
  }

  TO_STRING_KV(K_(das_scan_rtdef),
               K_(checker_ctdef),
               KPC_(table_loc),
               KPC_(local_tablet_loc),
               KPC_(se_rowkey_dist_ctx),
               K_(table_rowkey),
               K_(batch_distinct_fk_cnt),
               K_(clear_exprs));

  int reset();
  int reuse();
  int do_fk_check_single_row(const ObIArray<ObForeignKeyColumn> &columns,
                             const ObExprPtrIArray &row,
                             bool &has_result);
  
  int do_fk_check_batch(bool &all_has_result);
  
  int init_foreign_key_checker(int64_t estimate_row, 
                               const ObExprFrameInfo *expr_frame_info,
                               ObForeignKeyCheckerCtdef &fk_ctdef,
                               const ObExprPtrIArray &row,
                               ObIAllocator *allocator);
  
  int build_fk_check_das_task(const ObIArray<ObForeignKeyColumn> &columns,
                                    const ObExprPtrIArray &row,
                                    bool &need_check);
private:
  int init_das_scan_rtdef();
  int calc_lookup_tablet_loc(ObDASTabletLoc *&tablet_loc);
  int get_das_scan_op(ObDASTabletLoc *tablet_loc, ObDASScanOp *&das_scan_op);
  // map fk column to the rowkey column of parent/parent index table
  int build_table_range(const ObIArray<ObForeignKeyColumn> &columns,
                        const ObExprPtrIArray &row,
                        ObNewRange &lookup_range,
                        bool &need_check);
  int build_index_table_range(const ObIArray<ObForeignKeyColumn> &columns,
                              const ObExprPtrIArray &row,
                              ObNewRange &lookup_range,
                              bool &need_check);
  int build_index_table_range_need_shadow_column(const ObIArray<ObForeignKeyColumn> &columns,
                                                const ObExprPtrIArray &row,
                                                ObNewRange &lookup_range,
                                                bool &need_check);
  int build_primary_table_range(const ObIArray<ObForeignKeyColumn> &columns,
                                const ObExprPtrIArray &row,
                                ObNewRange &lookup_range,
                                bool &need_check);
  int check_need_shadow_columns(const ObIArray<ObForeignKeyColumn> &columns,
                                const ObExprPtrIArray &row,
                                bool &need_shadow_columns);
  int check_fk_columns_has_null(const ObIArray<ObForeignKeyColumn> &columns,
                                const ObExprPtrIArray &row,
                                bool &is_all_null,
                                bool &has_null);
  int get_scan_result_count(int64_t &get_row_count);
  int init_clear_exprs(ObForeignKeyCheckerCtdef &fk_ctdef, const ObExprPtrIArray &row);
  int check_fk_column_type(const ObObjMeta &col_obj_meta,
                           const ObObjMeta &dst_obj_meta,
                           const ObPrecision col_precision,
                           const ObPrecision dst_precision,
                           bool &need_extra_cast);
public:
  ObEvalCtx &eval_ctx_; // used for expression evaluation
  ObDASScanRtDef das_scan_rtdef_;
  // Store calculation table loc and the partition key, primary key expr, etc., needed for constructing das-task
  const ObForeignKeyCheckerCtdef  &checker_ctdef_;
  // Foreign key check back-table construction for das-task use
  ObDASRef das_ref_;
  ObDASTableLoc *table_loc_;
  ObDASTabletLoc *local_tablet_loc_; // The table used for foreign key check is a non-partitioned table when used
  SeRowkeyDistCtx *se_rowkey_dist_ctx_;
  ObRowkey table_rowkey_;
  int64_t batch_distinct_fk_cnt_;
  ObSEArray<ObExpr *, 4> clear_exprs_;
  ObIAllocator *allocator_;
};

} // namespace sql
} // namespace oceanbase
#endif /* OBDEV_SRC_SQL_ENGINE_DML_OB_CONFLICT_ROW_CHECKER_H_ */
