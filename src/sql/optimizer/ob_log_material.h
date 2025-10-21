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

#ifndef OCEANBASE_SQL_OB_LOG_MATERIAL_H_
#define OCEANBASE_SQL_OB_LOG_MATERIAL_H_

#include "sql/optimizer/ob_logical_operator.h"

namespace oceanbase
{
namespace sql
{
  class ObLogMaterial : public ObLogicalOperator
  {
  public:
    ObLogMaterial(ObLogPlan &plan) : ObLogicalOperator(plan)
    {}
    virtual ~ObLogMaterial() {}
    virtual int est_cost() override;
    virtual int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost) override;
    virtual bool is_block_op() const override { return true; }
  private:
    DISALLOW_COPY_AND_ASSIGN(ObLogMaterial);
  };
}
}



#endif // OCEANBASE_SQL_OB_LOG_MATERIAL_H_
