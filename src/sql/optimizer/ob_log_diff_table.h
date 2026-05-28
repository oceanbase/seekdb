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

#ifndef OB_LOG_DIFF_TABLE_H
#define OB_LOG_DIFF_TABLE_H

#include "sql/optimizer/ob_logical_operator.h"

namespace oceanbase
{
namespace sql
{

// Leaf logical operator for DIFF TABLE. Carries no child; output rows are
// produced at execute time by the corresponding physical operator
// (ObDiffTableOp), which scans the underlying tablets and emits the
// classified diff stream. Modeled after ObLogValues.
class ObLogDiffTable : public ObLogicalOperator
{
public:
  ObLogDiffTable(ObLogPlan &plan)
    : ObLogicalOperator(plan)
  {}
  virtual ~ObLogDiffTable() {}

  virtual int compute_op_parallel_and_server_info() override
  {
    return set_parallel_and_server_info_for_match_all();
  }

  virtual int get_card_without_filter(double &card) override
  {
    card = 1.0;
    return common::OB_SUCCESS;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObLogDiffTable);
};

} // namespace sql
} // namespace oceanbase

#endif // OB_LOG_DIFF_TABLE_H
