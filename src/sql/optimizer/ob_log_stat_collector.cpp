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

#include "sql/optimizer/ob_log_stat_collector.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;


const char *ObLogStatCollector::get_name() const
{
  static const char *name[1] =
  {
    "STATISTICS COLLECTOR",
  };
  return name[0];
}

int ObLogStatCollector::set_sort_keys(const common::ObIArray<OrderItem> &order_keys)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sort_keys_.assign(order_keys))) {
    LOG_WARN("failed to set sort keys", K(ret));
  } else { /* do nothing */ }
  return ret;
}

int ObLogStatCollector::get_op_exprs(ObIArray<ObRawExpr*> &all_exprs)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < sort_keys_.count(); i++) {
    if (OB_ISNULL(sort_keys_.at(i).expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret));
    } else if (OB_FAIL(all_exprs.push_back(sort_keys_.at(i).expr_))) {
      LOG_WARN("failed to push back exprs", K(ret));
    } else { /*do nothing*/ }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
      LOG_WARN("failed to get op exprs", K(ret));
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObLogStatCollector::inner_replace_op_exprs(ObRawExprReplacer &replacer)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < sort_keys_.count(); i++) {
    if (OB_ISNULL(sort_keys_.at(i).expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret));
    } else if (OB_FAIL(replace_expr_action(replacer, sort_keys_.at(i).expr_))) {
      LOG_WARN("failed to replace sort key expr", K(ret));
    }
  }
  return ret;
}
