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

#ifndef OB_RECURSIVE_INNER_DATA_OP_
#define OB_RECURSIVE_INNER_DATA_OP_

#include "sql/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/allocator/ob_malloc.h"
#include "sql/engine/aggregate/ob_exec_hash_struct.h"
#include "ob_search_method_op.h"
#include "ob_fake_cte_table_op.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{
class ObRecursiveInnerDataOp {
  using ObTreeNode = ObSearchMethodOp::ObTreeNode;
  friend class ObRecursiveUnionAllOp;
  friend class ObRecursiveUnionAllSpec;
public:
  struct RowComparer;
  enum RecursiveUnionState {
    R_UNION_BEGIN,
    R_UNION_READ_LEFT,
    R_UNION_READ_RIGHT,
    R_UNION_END,
    R_UNION_STATE_COUNT
  };
  enum SearchStrategyType
  {
    DEPTH_FRIST, BREADTH_FRIST, BREADTH_FIRST_BULK
  };
public:
  explicit ObRecursiveInnerDataOp(ObEvalCtx &eval_ctx,
                                  ObExecContext &exec_ctx,
                                  const ExprFixedArray &left_output,
                                  const common::ObIArray<ObExpr *> &output_union_exprs) :
      state_(RecursiveUnionState::R_UNION_READ_LEFT),
      stored_row_buf_(ObModIds::OB_SQL_CTE_ROW),
      pump_operator_(nullptr),
      left_op_(nullptr),
      right_op_(nullptr),
      search_type_(SearchStrategyType::BREADTH_FRIST),
      result_output_(stored_row_buf_),
      cte_columns_(nullptr),
      bfs_pump_(stored_row_buf_, left_output),
      bfs_bulk_pump_(stored_row_buf_, left_output),
      eval_ctx_(eval_ctx),
      ctx_(exec_ctx),
      output_union_exprs_(output_union_exprs),
      max_recursion_depth_(0)
  {
  }
  ~ObRecursiveInnerDataOp() = default;

  inline void set_left_child(ObOperator* op) { left_op_ = op; };
  inline void set_right_child(ObOperator* op) { right_op_ = op; };
  inline void set_fake_cte_table(ObFakeCTETableOp* cte_table) { pump_operator_ = cte_table; };
  inline void set_search_strategy(ObRecursiveInnerDataOp::SearchStrategyType strategy)
  {
    search_type_ = strategy;
  }
  int add_sort_collation(ObSortFieldCollation sort_collation);
  int add_cycle_column(uint64_t index);
  int add_cmp_func(ObCmpFunc cmp_func);
  int get_next_row();
  int get_next_batch(const int64_t batch_size, ObBatchRows &brs);
  int rescan();
  int set_fake_cte_table_empty();
  int init();
  void set_cte_column_exprs(common::ObIArray<ObExpr *> *exprs) { cte_columns_ = exprs; }
  void set_batch_size(const int64_t batch_size) { batch_size_ = batch_size; };

private:
  void destroy();
  int try_get_left_rows(bool batch_mode, int64_t batch_size, int64_t &read_rows);
  int try_get_right_rows(bool batch_mode, int64_t batch_size, int64_t &read_rows);
  int try_format_output_row(int64_t &read_rows);
  int try_format_output_batch(int64_t batch_size, int64_t &read_rows);
  /**
   * The left son of the recursive union is called plan a, the right son is called plan b
   * plan a will produce the initial data, the recursive union itself controls the progress of the recursion,
   * the right son is the plan for recursive execution
   */
  int get_all_data_from_left_child();
  int get_all_data_from_left_batch();
  int get_all_data_from_right_child();
  int get_all_data_from_right_batch();
  // Breadth-first recursive, performing UNION ALL operation on the row
  int breadth_first_union(bool left_branch, bool &continue_search);
  // Breadth-first batch search recursion, performing UNION ALL operation on rows
  int breadth_first_bulk_union(bool left_branch);
  int start_new_level(bool left_branch);
  // Feed a row of data to the fake cte table operator, it will serve as the input for the next plan b
  int fake_cte_table_add_row(ObTreeNode &node);
  // Feed a batch of data to the fake cte table operator, it will serve as the input for the next plan b
  int fake_cte_table_add_bulk_rows(bool left_branch);
  // Set cte table column expr value
  int assign_to_cur_row(ObChunkDatumStore::StoredRow *stored_row);
  ObSearchMethodOp * get_search_method_bump() {
    if (SearchStrategyType::BREADTH_FIRST_BULK == search_type_) {
      return &bfs_bulk_pump_;
    } else {
      return &bfs_pump_;
    }
  };
  int check_recursive_depth();
  bool is_bulk_search() { return SearchStrategyType::BREADTH_FIRST_BULK == search_type_; };
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRecursiveInnerDataOp);
private:
  RecursiveUnionState state_;
  common::ObArenaAllocator stored_row_buf_;
  ObFakeCTETableOp* pump_operator_;
  ObOperator* left_op_;
  ObOperator* right_op_;
  // Mark depth-first or breadth-first
  SearchStrategyType search_type_;
  // The data to be output to the next operator, R in the pseudocode
  common::ObList<ObTreeNode, common::ObIAllocator> result_output_;
  common::ObIArray<ObExpr *> *cte_columns_;
  // Breadth-first
  ObBreadthFirstSearchOp bfs_pump_;
  // Breadth-first batch search
  ObBreadthFirstSearchBulkOp bfs_bulk_pump_;
  ObEvalCtx &eval_ctx_;
  ObExecContext &ctx_;
  const common::ObIArray<ObExpr *> &output_union_exprs_;
  int64_t batch_size_ = 1;
  uint64_t max_recursion_depth_;
};
} // end namespace sql
} // end namespace oceanbase

#endif
