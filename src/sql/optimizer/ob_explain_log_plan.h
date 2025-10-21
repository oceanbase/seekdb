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

#ifndef _OB_EXPLAIN_LOG_PLAN_H
#define _OB_EXPLAIN_LOG_PLAN_H
#include "sql/optimizer/ob_log_plan.h"

namespace oceanbase
{
namespace sql
{
  class ObExplainLogPlan : public ObLogPlan
  {
  public:
    ObExplainLogPlan(ObOptimizerContext &ctx, const ObDMLStmt *explain_stmt)
      : ObLogPlan(ctx, explain_stmt)
    {}
    virtual ~ObExplainLogPlan() {}
  protected:
    virtual int generate_normal_raw_plan() override;
  private:
    int check_explain_generate_plan_with_outline(ObLogPlan *real_plan);
    DISALLOW_COPY_AND_ASSIGN(ObExplainLogPlan);
  };
}
}
#endif
