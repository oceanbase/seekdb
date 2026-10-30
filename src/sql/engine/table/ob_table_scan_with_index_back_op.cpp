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
#include "sql/engine/table/ob_table_scan_with_index_back_op.h"
#include "data_plane/access/ob_table_scan_access.h"
namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObTableScanWithIndexBackSpec, ObTableScanSpec), index_scan_tree_id_);

ObTableScanWithIndexBackSpec::ObTableScanWithIndexBackSpec(common::ObIAllocator &alloc,
                                                           const ObPhyOperatorType type)
  : ObTableScanSpec(alloc, type),
    index_scan_tree_id_(OB_INVALID_ID)
{
}

ObTableScanWithIndexBackSpec::~ObTableScanWithIndexBackSpec()
{
}
/*
int ObTableScanWithIndexBackOp::inner_open()
{
  int ret = OB_SUCCESS;
  // index scan needs to rely on some parameters of the table, so table scan param should be prepared first, then open index scan
  ObOperatorKit *op_kit = nullptr;
  if (OB_ISNULL(op_kit = ctx_.get_operator_kit(MY_SPEC.get_index_scan_tree_id()))
              || OB_ISNULL(op_kit->op_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get ObOperater from exec ctx failed", K(MY_SPEC.get_index_scan_tree_id()));
  } else if (FALSE_IT(index_scan_tree_ = op_kit->op_)) {
  } else if (OB_FAIL(prepare_scan_param())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("prepare scan param failed", K(ret));
    }
  } else if (OB_FAIL(open_index_scan())) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      LOG_WARN("open index scan tree failed", K(ret));
    }
  } else if (OB_FAIL(do_table_scan_with_index())) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      LOG_WARN("do table scan with index failed", K(ret));
    }
  }
  return ret;
}
*/

int ObTableScanWithIndexBackOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(index_scan_tree_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(index_scan_tree_->rescan())) {
  } else if (OB_FAIL(ObTableScanOp::inner_rescan())) {
  } else {
    is_index_end_ = false;
    if (OB_ISNULL(result_)) {
      // First rescan, table iterator not initialized
      if (OB_FAIL(do_table_scan_with_index())) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
        }
      }
    } else if (OB_FAIL(do_table_rescan_with_index())) {
      if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      }
    }
  }
  return ret;
}


int ObTableScanWithIndexBackOp::extract_range_from_index()
{
  return OB_SUCCESS;
}
int ObTableScanWithIndexBackOp::do_table_scan_with_index()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObTableScanWithIndexBackOp::do_table_rescan_with_index()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(extract_range_from_index())) {
  } else if (scan_param_.key_ranges_.count() <= 0) {
    //do nothing
    read_action_ = READ_ITER_END;
  } else if (OB_ISNULL(result_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(data_plane::table_scan_rescan(result_, scan_param_))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
    }
  } else {
    read_action_ = READ_ITERATOR;
  }
  return ret;
}

int ObTableScanWithIndexBackOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  ObNewRow *cur_row = NULL;
  bool need_continue = true;
  if (OB_FAIL(try_check_status())) {
  }
  while (OB_SUCC(ret) && need_continue) {
    switch (read_action_) {
    case READ_TABLE_PARTITION: {
      if (OB_FAIL(do_table_rescan_with_index())) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
        }
      }
      break;
    }
    case READ_ITERATOR: {
      if (OB_FAIL(result_->get_next_row(cur_row))) {
        if (OB_ITER_END != ret) {
        } else {
          // Reached the end of the iterator, read new data from storage
          read_action_ = READ_TABLE_PARTITION;
          ret = OB_SUCCESS;
        }
      } else {
        output_row_cnt_++;
        need_continue = false;
      }
      break;
    }
    case READ_ITER_END: {
      // Reached the end of the data, return OB_ITER_END to the caller
      ret = OB_ITER_END;
      need_continue = false;
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("read action is unexpected", K_(read_action));
      break;
    }
  }
  return ret;
}

int ObTableScanWithIndexBackOp::inner_close()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (index_scan_tree_ != NULL) {
    if (OB_FAIL(index_scan_tree_->close())) {
    }
  }
  tmp_ret = ret;
  if (OB_FAIL(ObTableScanOp::inner_close())) {
  }
  ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
