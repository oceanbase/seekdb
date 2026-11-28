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

#define USING_LOG_PREFIX SQL_OPT
#include <inttypes.h>
#include "sql/optimizer/ob_log_granule_iterator.h"

using namespace oceanbase;
using namespace sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

const char *ObLogGranuleIterator::get_name() const
{
  static const char *gi_type[5] =
  {
    "LIGHT PARTITION ITERATOR",
    "PX PARTITION ITERATOR",
    "PX BLOCK ITERATOR",
    "PX PARTITION HASH JOIN-FILTER",
    "PX BLOCK HASH JOIN-FILTER"
  };
  const char *result = nullptr;
  bool is_part_gi = is_partition_gi();
  int64_t index = 0;
  if (is_part_gi) {
    index = 1;
  } else {
    index = 2;
  }
  if (bf_info_.is_inited_ && index > 0) {
    index += 2;
  }
  result = gi_type[index];
  return result;
}

int ObLogGranuleIterator::get_op_exprs(ObIArray<ObRawExpr*> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogGranuleIterator::get_plan_item_info(PlanText &plan_text, 
                                             ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  bool has_first = false;
  static const int64_t FLAG_NEED_PRINT_COUNT = 8;
  static const int32_t MAX_GI_FLAG_NAME_LENGTH = 30;
  static const char gi_flag_name[FLAG_NEED_PRINT_COUNT][MAX_GI_FLAG_NAME_LENGTH] =
      { "affinitize", "partition wise", "access all", "param down",
        "force partition granule", "slave mapping",
        "desc", "asc" };
  bool gi_flag[FLAG_NEED_PRINT_COUNT] =
      { affinitize(), pwj_gi(), access_all(), with_param_down(),
        force_partition_granule(), slave_mapping_granule(),
        desc_order(), asc_order() };
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
  }
  BEGIN_BUF_PRINT;
  for (int64_t i = 0; OB_SUCC(ret) && i < FLAG_NEED_PRINT_COUNT; ++i) {
    if (!gi_flag[i]) {
      continue;
    } else if (has_first && OB_FAIL(BUF_PRINTF(", "))) {
    } else if (OB_FAIL(BUF_PRINTF("%.*s", 
                                  MAX_GI_FLAG_NAME_LENGTH, 
                                  gi_flag_name[i]))) {
    } else {
      has_first = true;
    }
  }
  END_BUF_PRINT(plan_item.special_predicates_,
                plan_item.special_predicates_len_);
  if (OB_SUCC(ret) && 
      get_join_filter_info().is_inited_ &&
      OB_INVALID_ID != get_join_filter_info().filter_id_) {
    BEGIN_BUF_PRINT;
    if (OB_FAIL(BUF_PRINTF(":RF%04" PRId64 "", get_join_filter_info().filter_id_))) {
    }
    END_BUF_PRINT(plan_item.object_alias_, 
                  plan_item.object_alias_len_);
  }
  return ret;
}

int ObLogGranuleIterator::allocate_expr_post(ObAllocExprContext &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::allocate_expr_post(ctx))) {
  } else if (NULL != tablet_id_expr_ &&
            OB_FAIL(get_plan()->get_optimizer_context().get_all_exprs().append(tablet_id_expr_))) {
  }
  return ret;
}

int ObLogGranuleIterator::compute_op_ordering()
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *child = NULL;
  if (OB_ISNULL(child = get_child(ObLogicalOperator::first_child))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObLogicalOperator::compute_op_ordering())) {
  } else if (!child->is_exchange_allocated() && child->get_is_range_order() &&
             OB_FAIL(set_range_order())) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogGranuleIterator::set_range_order()
{
  int ret = OB_SUCCESS;
  const ObDMLStmt *stmt = NULL;
  common::ObIArray<OrderItem> &op_ordering = get_op_ordering();
  if (OB_ISNULL(stmt = get_plan()->get_stmt()) || OB_ISNULL(stmt->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (affinitize()) {
    ObLogicalOperator *child = get_child(first_child);
    if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (child->get_op_ordering().count() <= 0) {
      //do nothing
    } else {
      common::ObIArray<OrderItem> &child_ordering = child->get_op_ordering();
      bool is_asc_order = is_ascending_direction(child_ordering.at(0).order_type_);
      if (is_asc_order) {
        add_flag(GI_ASC_ORDER);
      } else {
        add_flag(GI_DESC_ORDER);
      }
    }
  } else if (!op_ordering.empty()) {
    // Suppose (range) partition order is asc, so first order is same partition order
    bool is_asc_order = is_ascending_direction(op_ordering.at(0).order_type_);
    bool used = true;
    if (OB_FAIL(check_op_orderding_used_by_parent(used))) {
    } else if (!used) {
      //do nothing
    } else if (is_asc_order) {
      add_flag(GI_ASC_ORDER);
    } else {
      add_flag(GI_DESC_ORDER);
    }
  }
  return ret;
}

int ObLogGranuleIterator::est_cost()
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *child = NULL;
  if (OB_ISNULL(child = get_child(ObLogicalOperator::first_child))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    card_ = child->get_card();
    op_cost_ = 0;
    cost_ = op_cost_ + child->get_cost();
  }
  return ret;
}

bool ObLogGranuleIterator::is_partition_gi() const
{
  return ObGranuleUtil::is_partition_granule_flag(gi_attri_flag_) || parallel_ == 1;
}

void ObLogGranuleIterator::add_flag(uint64_t attri)
{
  gi_attri_flag_ |= attri;
}

}
}
