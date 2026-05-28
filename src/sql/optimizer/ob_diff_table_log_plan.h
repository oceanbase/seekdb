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

#ifndef OCEANBASE_SQL_OPTIMIZER_OB_DIFF_TABLE_LOG_PLAN_H_
#define OCEANBASE_SQL_OPTIMIZER_OB_DIFF_TABLE_LOG_PLAN_H_

#include "sql/optimizer/ob_log_plan.h"

namespace oceanbase
{
namespace sql
{

// Wraps a pre-computed ObRowStore (built by the resolver) into a plan
// whose root is ObLogValues. Same shape as ObHelpLogPlan — that's the
// established precedent for "command-style statement that produces a
// multi-row result through the regular SELECT plan/protocol path".
class ObDiffTableLogPlan : public ObLogPlan
{
public:
  ObDiffTableLogPlan(ObOptimizerContext &ctx, const ObDMLStmt *diff_stmt)
    : ObLogPlan(ctx, diff_stmt) {}
  virtual ~ObDiffTableLogPlan() {}
protected:
  int generate_normal_raw_plan();
private:
  DISALLOW_COPY_AND_ASSIGN(ObDiffTableLogPlan);
};

} // namespace sql
} // namespace oceanbase
#endif
