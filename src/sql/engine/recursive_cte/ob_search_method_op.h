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

#ifndef OB_DEPTH_FIRST_SEARCH_OP_H_
#define OB_DEPTH_FIRST_SEARCH_OP_H_

#include "sql/engine/aggregate/ob_exec_hash_struct.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/list/ob_list.h"
#include "share/datum/ob_datum_funcs.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/sort/ob_sort_basic_info.h"
#include "lib/rc/context.h"

namespace oceanbase
{
namespace sql
{

class ObSearchMethodOp
{
public:
  typedef struct _BreadthFirstSearchTreeNode {
    _BreadthFirstSearchTreeNode() :
      child_num_(0),
      stored_row_(nullptr),
      children_(nullptr),
      parent_(nullptr)
    {}
    int64_t child_num_;
    ObChunkDatumStore::StoredRow* stored_row_;
    struct _BreadthFirstSearchTreeNode** children_;
    struct _BreadthFirstSearchTreeNode* parent_;
    TO_STRING_KV("row ", stored_row_, "child_num_", child_num_);
  } ObBFSTreeNode;

  typedef struct _TreeNode
  {
    _TreeNode() :
      is_cycle_(false),
      tree_level_(0),
      stored_row_(nullptr),
      in_bstree_node_(nullptr)
    {
    }
    bool is_cycle_;
    uint64_t tree_level_;
    ObChunkDatumStore::StoredRow* stored_row_;
    ObBFSTreeNode* in_bstree_node_;
    TO_STRING_KV("is tree level", tree_level_, "is cycle", is_cycle_, "row", stored_row_);
  } ObTreeNode;
  // input row's initial size, starting at 128.
  static const int64_t INIT_ROW_COUNT = 1<<7l;
  // The size of the hash table for detecting cycles on the depth-first path, a tree height of 32 is sufficient.
  static const int64_t CTE_SET_NUM = 1<<5l;
public:
  explicit ObSearchMethodOp(common::ObIAllocator &allocator, const ExprFixedArray &left_output)
    : allocator_(allocator), input_rows_(),
  left_output_(left_output), last_node_level_(UINT64_MAX) {};
  virtual ~ObSearchMethodOp() = default;

  virtual int empty() = 0;
  virtual int reuse();

  virtual int add_row(const ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx);
  // Use line content for comparison, if there is the same data then consider this node as a loop
  int is_same_row(ObChunkDatumStore::StoredRow &row_1st, ObChunkDatumStore::StoredRow &row_2nd,
                  bool &is_cycle);
  int64_t count() { return input_rows_.count(); }
  virtual uint64_t get_last_node_level() { return last_node_level_; }
  const static int64_t ROW_EXTRA_SIZE = 0;
protected:
  // hard code seed, 24bit max prime number
  static const int64_t HASH_SEED = 16777213;
  common::ObIAllocator &allocator_;
  common::ObArray<ObChunkDatumStore::StoredRow *> input_rows_;
  common::ObArray<ObChunkDatumStore::StoredRow*> recycle_rows_;
  const ExprFixedArray &left_output_;
  // Record the current query row's level in the tree
  uint64_t last_node_level_;
};

/**
 * Since it is necessary to determine the existence of a cycle, the entire tree will be saved in memory using breadth-first search;
 * Use depth-first search whenever possible instead of breadth-first search.
 */
class ObBreadthFirstSearchOp : public ObSearchMethodOp
{
public:
  ObBreadthFirstSearchOp(common::ObIAllocator &allocator, const ExprFixedArray &left_output) :
    ObSearchMethodOp(allocator, left_output), bst_root_(),
    current_parent_node_(&bst_root_), search_queue_(allocator), search_results_() {
      last_node_level_ = 0;
    }
  virtual ~ObBreadthFirstSearchOp() = default;

  virtual int reuse() override;
  virtual void destroy();
  virtual int empty() override { return input_rows_.empty() && search_queue_.empty()
                                        && search_results_.empty(); }

  int add_result_rows();
  int finish_add_row(bool sort);
  int get_next_nocycle_node(common::ObList<ObTreeNode, common::ObIAllocator> &result_output,
                            ObTreeNode &nocycle_node);
  int update_parent_node(ObTreeNode &node);

private:
  int init_new_nodes(ObBFSTreeNode *last_bstnode, int64_t child_num);
  int is_breadth_cycle_node(ObTreeNode &node);

private:
  // breadth first search root node
  ObBFSTreeNode bst_root_;
  /**
   *            A
   *      AA         AB
   *  AAA  AAB    ABA   ABB
   *  For example, during a query process, current_parent_node_ points to AA
   *  search_queue_ contains AA AB as the query level
   *  search_results_ contains AAA AAB as the query result level
   */
  ObBFSTreeNode* current_parent_node_;
  common::ObList<ObTreeNode, common::ObIAllocator> search_queue_;
  common::ObArray<ObTreeNode> search_results_;
};

class ObBreadthFirstSearchBulkOp : public ObSearchMethodOp
{
public:
  ObBreadthFirstSearchBulkOp(common::ObIAllocator &allocator,
                             const ExprFixedArray &left_output) :
    ObSearchMethodOp(allocator, left_output), bst_root_(),
    search_results_(), cur_recursion_depth_(0), cur_iter_groups_(), last_iter_groups_(),
    max_buffer_cnt_(0), result_output_buffer_(nullptr), mem_context_(nullptr),
    malloc_allocator_(nullptr) {}
  virtual ~ObBreadthFirstSearchBulkOp() = default;

  virtual int reuse() override;
  virtual void destroy();
  virtual int empty() override { return input_rows_.empty() && search_results_.empty(); }

  int add_result_rows(bool left_branch);
  int get_next_nocycle_bulk(ObList<ObTreeNode, common::ObIAllocator> &result_output,
                          ObArray<ObChunkDatumStore::StoredRow *> &fake_table_bulk_rows);
  int update_search_depth(uint64_t max_recursive_depth);
  int sort_result_output_nodes(int64_t rows_cnt);
  int add_row(const ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx);
  int init_mem_context();
  void free_input_rows_mem();
  void free_last_iter_mem();

private:
  int save_to_store_row(ObIAllocator &allocator, const ObIArray<ObExpr *> &exprs,
                        ObEvalCtx &eval_ctx, ObChunkDatumStore::StoredRow *&store_row);

private:
  // breadth first search root node
  ObBFSTreeNode bst_root_;
  common::ObArray<ObTreeNode> search_results_;
  uint64_t cur_recursion_depth_;
  common::ObArray<ObBFSTreeNode *> cur_iter_groups_;
  common::ObArray<ObBFSTreeNode *> last_iter_groups_;
  int64_t max_buffer_cnt_;
  ObTreeNode ** result_output_buffer_;
  lib::MemoryContext mem_context_;
  ObIAllocator *malloc_allocator_;
  // for mysql mode, free last iter memory because not need check cycle
  common::ObArray<ObChunkDatumStore::StoredRow *> last_iter_input_rows_;
};

}
}

#endif
