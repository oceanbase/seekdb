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

#include "ob_search_method_op.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;

int ObSearchMethodOp::reuse()
{
  input_rows_.reuse();
  return OB_SUCCESS;
}

int ObSearchMethodOp::add_row(const ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  ObChunkDatumStore::LastStoredRow last_row(allocator_);
  if (input_rows_.empty() && 0 == input_rows_.get_capacity()
      && OB_FAIL(input_rows_.reserve(INIT_ROW_COUNT))) {
    LOG_WARN("Failed to pre allocate array", K(ret));
  } else if (OB_UNLIKELY(exprs.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exprs empty", K(ret));
  } else if (OB_FAIL(last_row.save_store_row(exprs, eval_ctx, ROW_EXTRA_SIZE))) {
    LOG_WARN("save store row failed", K(ret));
  } else if (OB_ISNULL(last_row.store_row_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stored_row of last_stored_row is null", K(ret));
  } else if (OB_FAIL(input_rows_.push_back(last_row.store_row_))) {
    LOG_WARN("Push new row to result input error", K(ret));
  } else {
  }
  return ret;
}

int ObSearchMethodOp::is_same_row(ObChunkDatumStore::StoredRow &row_1st,
                                  ObChunkDatumStore::StoredRow &row_2nd, bool &is_cycle)
{
  int ret = OB_SUCCESS;
  const ObDatum *cells_1st = row_1st.cells();
  const ObDatum *cells_2nd = row_2nd.cells();
  int cmp_ret = 0;
  if (OB_UNLIKELY(0 == row_1st.cnt_ || 0 == row_2nd.cnt_)
      || OB_ISNULL(cells_1st) || OB_ISNULL(cells_2nd)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Inconformity row schema", K(ret), K(row_1st), K(row_2nd));
  } else {
    // detect whole row
    is_cycle = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < left_output_.count(); i++) {
      if (OB_FAIL(left_output_.at(i)->basic_funcs_->null_first_cmp_(cells_1st[i], cells_2nd[i], cmp_ret))) {
        LOG_WARN("failed to compare", K(ret), K(i));
      } else if (0 != cmp_ret) {
        is_cycle = false;
        break;
      }
    }
    if (is_cycle) {
      ret = OB_ERR_CYCLE_FOUND_IN_RECURSIVE_CTE;
      LOG_WARN("Cycle detected while executing recursive WITH query", K(ret));
    }
  }
  return ret;
}

void ObBreadthFirstSearchOp::destroy()
{
  search_queue_.reset();
}


int ObBreadthFirstSearchOp::reuse()
{
  ObSearchMethodOp::reuse();
  current_parent_node_ = &bst_root_;
  last_node_level_ = 0;
  bst_root_.child_num_ = 0;
  bst_root_.children_ = nullptr;
  bst_root_.parent_ = nullptr;
  bst_root_.stored_row_ = nullptr;
  search_queue_.reset();
  search_results_.reuse();
  return OB_SUCCESS;
}

int ObBreadthFirstSearchOp::init_new_nodes(ObBFSTreeNode *last_bstnode, int64_t child_num)
{
  int ret = OB_SUCCESS;
  void* childs_ptr = nullptr;
  // Initialize tree node memory
  if (OB_UNLIKELY(0 == child_num)) {
    //do nothing
  } else if (OB_ISNULL(last_bstnode)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Last bst node can not be null", K(ret));
  } else if (OB_UNLIKELY(last_bstnode->child_num_ != 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Last bst node can not be ini twice", K(ret), KPC(last_bstnode));
  } else if (OB_ISNULL(childs_ptr = allocator_.alloc(sizeof(ObBFSTreeNode*) * child_num))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("Alloc memory for row failed", "size", child_num * sizeof(ObBFSTreeNode*), K(ret));
  } else {
    last_bstnode->children_ = (ObBFSTreeNode**)childs_ptr;
    last_bstnode->child_num_ = child_num;
  }
  return ret;
}

int ObBreadthFirstSearchOp::is_breadth_cycle_node(ObTreeNode &node)
{
  int ret = OB_SUCCESS;
  ObBFSTreeNode* tmp = current_parent_node_;
  ObChunkDatumStore::StoredRow* row = node.stored_row_;
  if (OB_ISNULL(tmp) || OB_ISNULL(row)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The last_bstnode and row an not be null", K(ret), KPC(row));
  } else {
    // bst_root_ 's row_ is empty
    while(OB_SUCC(ret) && OB_NOT_NULL(tmp) && OB_NOT_NULL(tmp->stored_row_)) {
      ObChunkDatumStore::StoredRow* row_1st = row;
      ObChunkDatumStore::StoredRow* row_2nd = tmp->stored_row_;
      // From Bianque's perspective, the detection of cycle takes up most of the time in hierarchical queries, and it is particularly slow.
      if (OB_FAIL(is_same_row(*row_1st, *row_2nd, node.is_cycle_))) {
        LOG_WARN("Failed to compare the two row", K(ret), KPC(row_1st), KPC(row_2nd));
      } else if (node.is_cycle_) {
        break;
      } else {
        tmp = tmp->parent_;
      }
    }
  }
  return ret;
}

int ObBreadthFirstSearchOp::get_next_nocycle_node(ObList<ObTreeNode,
                                                    common::ObIAllocator> &result_output,
                                                    ObTreeNode &nocycle_node)
{
  int ret = OB_SUCCESS;
  ObTreeNode node;
  bool got_row = false;
  while(OB_SUCC(ret) && !search_queue_.empty()) {
    if (OB_FAIL(search_queue_.pop_front(node))) {
      LOG_WARN("Get row from hold queue failed", K(ret));
    } else if (OB_FAIL(result_output.push_back(node))) {
      LOG_WARN("Failed to push row to output ", K(ret));
    } else if (node.is_cycle_) {
      if (OB_FAIL(recycle_rows_.push_back(node.stored_row_))) {
        LOG_WARN("Failed to push back rows", K(ret));
      }
    } else {
      got_row = true;
      nocycle_node = node;
      break;
    }
  }// end while
  if (OB_SUCC(ret) && !got_row) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObBreadthFirstSearchOp::update_parent_node(ObTreeNode &node)
{
  int ret = OB_SUCCESS;
  current_parent_node_ = node.in_bstree_node_;
  last_node_level_ = node.tree_level_;
  return ret;
}

int ObBreadthFirstSearchOp::add_result_rows()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_new_nodes(current_parent_node_, input_rows_.count()))) {
    LOG_WARN("Failed to init new bst node", K(ret));
  } else {
    ARRAY_FOREACH(input_rows_, i) {
      void* ptr = nullptr;
      ObBFSTreeNode* tmp = nullptr;
      ObTreeNode node;
      node.stored_row_ = input_rows_.at(i);
      // breadth search tree
      if (OB_ISNULL(ptr = allocator_.alloc(sizeof(ObBFSTreeNode)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("Alloc memory for row failed", "size", sizeof(ObBFSTreeNode), K(ret));
      } else {
        tmp = new(ptr) ObBFSTreeNode();
        tmp->stored_row_ = input_rows_.at(i);
        tmp->parent_ = current_parent_node_;
        current_parent_node_->children_[i] = tmp;
        node.in_bstree_node_ = tmp;
        node.tree_level_ = last_node_level_ + 1;
        if (OB_FAIL(is_breadth_cycle_node(node))) {
          LOG_WARN("Find cycle failed", K(ret));
        } else if (OB_FAIL(search_results_.push_back(node))) {
          LOG_WARN("Push back data to layer_results failed", K(ret));
        } else {
          LOG_DEBUG("Result node", K(node));
        }
      }
    }
  }
  input_rows_.reuse();
  return ret;
}

int ObBreadthFirstSearchOp::finish_add_row(bool sort)
{
  int ret = OB_SUCCESS;
  if (!search_queue_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The last result still has residual", K(search_queue_), K(ret));
  } else {
    ARRAY_FOREACH(search_results_, i) {
      if (OB_FAIL(search_queue_.push_back(search_results_.at(i)))) {
        LOG_WARN("Push back failed", K(ret));
      }
    }
  }
  search_results_.reuse();
  return ret;
}

int ObBreadthFirstSearchBulkOp::reuse()
{
  ObSearchMethodOp::reuse();
  cur_recursion_depth_ = 0;
  bst_root_.parent_ = nullptr;
  bst_root_.stored_row_ = nullptr;
  search_results_.reuse();
  cur_iter_groups_.reuse();
  last_iter_groups_.reuse();
  free_last_iter_mem();
  return OB_SUCCESS;
}

void ObBreadthFirstSearchBulkOp::destroy()
{
  free_input_rows_mem();
  free_last_iter_mem();
  if (OB_NOT_NULL(mem_context_)) {
    DESTROY_CONTEXT(mem_context_);
    mem_context_ = nullptr;
  }
}


int ObBreadthFirstSearchBulkOp::get_next_nocycle_bulk(
      ObList<ObTreeNode, common::ObIAllocator> &result_output,
      ObArray<ObChunkDatumStore::StoredRow *> &fake_table_bulk_rows)
{
  int ret = OB_SUCCESS;
  int rows_cnt = search_results_.count();

  ObTreeNode node;
  ARRAY_FOREACH(search_results_, i) {
    if (FALSE_IT(node = search_results_.at(i))) {
      LOG_WARN("Get row from hold queue failed", K(ret));
    } else if (OB_FAIL(result_output.push_back(node))) {
      LOG_WARN("Failed to push row to output ", K(ret));
    } else if (node.is_cycle_) {
      if (OB_FAIL(recycle_rows_.push_back(node.stored_row_))) {
        LOG_WARN("Failed to push back rows", K(ret));
      }
    } else if (OB_FAIL(fake_table_bulk_rows.push_back(node.stored_row_))) {
      LOG_WARN("Failed to push back rows", K(ret));
    }
  }

  search_results_.reuse();
  if (OB_SUCC(ret) && fake_table_bulk_rows.empty()) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObBreadthFirstSearchBulkOp::update_search_depth(uint64_t max_recursive_depth)
{
  int ret = OB_SUCCESS;
  cur_recursion_depth_++;
  if (cur_recursion_depth_ > max_recursive_depth) {
    ret = OB_ERR_CTE_MAX_RECURSION_DEPTH;
    LOG_USER_ERROR(OB_ERR_CTE_MAX_RECURSION_DEPTH, cur_recursion_depth_);
    LOG_WARN("Recursive query aborted after too many iterations", K(ret), K(cur_recursion_depth_));
  }
  // warning for infinite recursion or large memory
  if (OB_SUCC(ret) && cur_recursion_depth_ > 1000) {
    if (0 == (cur_recursion_depth_ & (cur_recursion_depth_ - 1))) {
      LOG_WARN("Attention !! Recursion depth too large", K(cur_recursion_depth_));
    }
  }
  return ret;
}

int ObBreadthFirstSearchBulkOp::add_result_rows(bool left_branch)
{
  int ret = OB_SUCCESS;
  free_last_iter_mem();
  ARRAY_FOREACH(input_rows_, i) {
    ObTreeNode tree_node;
    if (OB_ISNULL(input_rows_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected input row", K(ret));
    } else if (FALSE_IT(tree_node.stored_row_ = input_rows_.at(i))) {
    } else if (OB_FAIL(search_results_.push_back(tree_node))) {
      LOG_WARN("Failed to push back result rows", K(ret));
    } else if (OB_FAIL(last_iter_input_rows_.push_back(input_rows_.at(i)))) {
      LOG_WARN("Failed to push back last iter input rows");
    } else {
      LOG_DEBUG("Result node", K(tree_node));
    }
  }
  input_rows_.reuse();
  return ret;
}

// if mysql mode, free last iter memory
int ObBreadthFirstSearchBulkOp::add_row(const ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(malloc_allocator_)) {
    if (OB_FAIL(init_mem_context())) {
      LOG_WARN("Failed to init mem context", K(ret));
    } else if (OB_ISNULL(malloc_allocator_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Malloc allocator not init in mysql mode", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    ObIAllocator *allocator = nullptr;
    allocator = malloc_allocator_;

    ObChunkDatumStore::StoredRow *store_row = NULL;
    if (input_rows_.empty() && 0 == input_rows_.get_capacity()
        && OB_FAIL(input_rows_.reserve(INIT_ROW_COUNT))) {
      LOG_WARN("Failed to pre allocate array", K(ret));
    } else if (OB_UNLIKELY(exprs.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("exprs empty", K(ret));
    } else if (OB_FAIL(save_to_store_row(*allocator, exprs, eval_ctx, store_row))) {
      LOG_WARN("save store row failed", K(ret));
    } else if (OB_ISNULL(store_row)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("stored_row  is null", K(ret));
    } else if (OB_FAIL(input_rows_.push_back(store_row))) {
      LOG_WARN("Push new row to result input error", K(ret));
    }
  }
  return ret;
}

int ObBreadthFirstSearchBulkOp::init_mem_context()
{
  int ret = OB_SUCCESS;
  lib::ContextParam param;
  param.set_mem_attr(MTL_ID(), "CTESearchBulk", ObCtxIds::WORK_AREA);
  if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
    LOG_WARN("create entity failed", K(ret));
  } else if (OB_ISNULL(mem_context_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null memory entity returned", K(ret));
  } else {
    malloc_allocator_ = &mem_context_->get_malloc_allocator();
  }
  return ret;
}

//RCTE operator may encounter -4013 memory problem when it want to allocate a very large memory for a new round of iteration data
//at that time, memory is stored in input_rows_, so we have to free them before reset allocator 
void ObBreadthFirstSearchBulkOp::free_input_rows_mem()
{
  if (OB_ISNULL(malloc_allocator_)) {
    // do nothing
  } else {
    for (int64_t i = 0; i < input_rows_.size(); ++i) {
      if (OB_NOT_NULL(input_rows_.at(i))) {
        malloc_allocator_->free(input_rows_.at(i));
      }
    }
    input_rows_.reset();
  }
}

void ObBreadthFirstSearchBulkOp::free_last_iter_mem()
{
  if (OB_ISNULL(malloc_allocator_)) {
    // do nothing
  } else {
    for (int64_t i = 0; i < last_iter_input_rows_.size(); ++i) {
      if (OB_NOT_NULL(last_iter_input_rows_.at(i))) {
        malloc_allocator_->free(last_iter_input_rows_.at(i));
      }
    }
    last_iter_input_rows_.reset();
  }
}

int ObBreadthFirstSearchBulkOp::save_to_store_row(ObIAllocator &allocator,
                                                  const ObIArray<ObExpr *> &exprs,
                                                  ObEvalCtx &eval_ctx,
                                                  ObChunkDatumStore::StoredRow *&store_row)
{
  int ret = OB_SUCCESS;
  int64_t row_size;
  if (OB_FAIL(ObChunkDatumStore::row_copy_size(exprs, eval_ctx, row_size))) {
    LOG_WARN("failed to calc copy size", K(ret));
  } else {
    int64_t head_size = sizeof(ObChunkDatumStore::StoredRow);
    int64_t buffer_len = row_size + head_size + ROW_EXTRA_SIZE;
    char *buf;
    if (OB_ISNULL(buf = reinterpret_cast<char *>(allocator.alloc(buffer_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc buf failed", K(ret));
    } else if (OB_FAIL(ObChunkDatumStore::StoredRow::build(store_row, exprs, eval_ctx, buf,
                                                           buffer_len,
                                                           static_cast<int32_t>(ROW_EXTRA_SIZE)))) {
      LOG_WARN("failed to build stored row", K(ret), K(buffer_len), K(row_size));
    }
  }
  return ret;
}
