/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/recursive_cte/ob_recursive_union_all_op.h"
#include "sql/engine/expr/ob_datum_cast.h"


namespace oceanbase
{
using namespace common;
namespace sql
{
const int64_t ObRecursiveUnionAllSpec::UNUSED_POS = -2;
int ObRecursiveUnionAllOp::inner_close()
{
  int ret = OB_SUCCESS;
  inner_data_.destroy();
  return ret;
}

ObRecursiveUnionAllSpec::ObRecursiveUnionAllSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      output_union_exprs_(alloc),
      pump_operator_id_(OB_INVALID_ID),
      strategy_(ObRecursiveInnerDataOp::SearchStrategyType::BREADTH_FRIST)
{
}

ObRecursiveUnionAllSpec::~ObRecursiveUnionAllSpec()
{
}

OB_SERIALIZE_MEMBER((ObRecursiveUnionAllSpec, ObOpSpec),
                    output_union_exprs_,
                    pump_operator_id_,
                    strategy_);

int ObRecursiveUnionAllOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_data_.rescan())){
    LOG_WARN("Failed to rescan inner data", K(ret));
  } else if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("Operator rescan failed", K(ret));
  }
  return ret;
}

int ObRecursiveUnionAllOp::inner_open()
{
  int ret = OB_SUCCESS;
  ObOperatorKit *op_kit = nullptr;
  if (OB_ISNULL(left_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Left op is null", K(ret));
  } else if (OB_FAIL(inner_data_.init())) {
    LOG_WARN("Failed to create hash filter", K(ret));
  } else if (OB_ISNULL(op_kit = ctx_.get_operator_kit(MY_SPEC.pump_operator_id_))
              || OB_ISNULL(op_kit->op_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get ObOperator from exec ctx failed", K(MY_SPEC.pump_operator_id_), K(op_kit), K(MY_SPEC.strategy_));
  } else {
    inner_data_.set_left_child(left_);
    inner_data_.set_right_child(right_);
    LOG_DEBUG("recursive union all inner open", K(MY_SPEC.output_), K(MY_SPEC.left_->output_),
                K(MY_SPEC.right_->output_));
    inner_data_.set_fake_cte_table(static_cast<ObFakeCTETableOp *>(op_kit->op_));
    inner_data_.set_search_strategy(MY_SPEC.strategy_);
    if (MY_SPEC.is_vectorized()) {
      inner_data_.set_batch_size(MY_SPEC.max_batch_size_);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.get_left()->get_output_count(); i++) {
      const ObExpr *expr = MY_SPEC.get_left()->output_.at(i);
      if(OB_ISNULL(expr)
        || OB_ISNULL(expr->basic_funcs_)
        || OB_ISNULL(expr->basic_funcs_->null_first_cmp_)
        || OB_ISNULL(expr->basic_funcs_->null_last_cmp_)
        || OB_ISNULL(expr->basic_funcs_->default_hash_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("left output expr is null or basic_funcs_ is null", K(ret));
      }
    }
  }
  return ret;
}

int ObRecursiveUnionAllOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_FAIL(try_check_status())) {
    LOG_WARN("Failed to check physical plan status", K(ret));
  } else if (OB_FAIL(inner_data_.get_next_row())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("Failed to get next sort row from recursive inner data", K(ret));
    }
  }
  return ret;
}

int ObRecursiveUnionAllOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  int64_t batch_size = std::min(max_row_cnt, MY_SPEC.max_batch_size_);
  if (OB_FAIL(try_check_status())) {
    LOG_WARN("Failed to check physical plan status", K(ret));
  } else if (OB_FAIL(inner_data_.get_next_batch(batch_size, brs_))) {
    LOG_WARN("Failed to get next sort row from recursive inner data", K(ret));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
