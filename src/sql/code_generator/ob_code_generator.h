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

#ifndef OCEANBASE_SQL_CODE_GENERATOR_OB_CODE_GENERATOR_
#define OCEANBASE_SQL_CODE_GENERATOR_OB_CODE_GENERATOR_

#include "sql/engine/expr/ob_expr.h"
#include "lib/container/ob_iarray.h"


namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace sql
{
class ObCodeGeneratorImpl;
class ObPhysicalPlan;
class ObLogPlan;
class ObRawExpr;
class ObLogicalOperator;
class ObRawExprUniqueSet;

class ObCodeGenerator
{
public:
  ObCodeGenerator(bool use_jit,
                  uint64_t min_cluster_version,
                  DatumParamStore *param_store)
    : use_jit_(use_jit),
      min_cluster_version_(min_cluster_version),
      param_store_(param_store)
  {}
  virtual ~ObCodeGenerator() {}
  //Generate execution plan
  //@param [in]  log_plan logical execution plan
  //@param [out] phy_plan physical execution plan
  int generate(const ObLogPlan &log_plan, ObPhysicalPlan &phy_plan);

  // detect batch row count for vectorized execution.
  static int detect_batch_size(
      const ObLogPlan &log_plan, int64_t &batch_size);

private:
  //Generate expression
  //@param [in]  log_plan logical execution plan
  //@param [out] phy_plan physical execution plan, will initialize rt_exprs_ and frame_info_ in the physical object
  int generate_exprs(const ObLogPlan &log_plan,
                     ObPhysicalPlan &phy_plan,
                     const uint64_t cur_cluster_version);
  //Generate physical operators
  //@param [in]  log_plan logical execution plan
  //@param [out] phy_plan physical execution plan
  int generate_operators(const ObLogPlan &log_plan,
                         ObPhysicalPlan &phy_plan,
                         const uint64_t cur_cluster_version);

  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObCodeGenerator);
private:
  //TODO shengle remove
  bool use_jit_;
  uint64_t min_cluster_version_;
  // All parameterized constant objects
  DatumParamStore *param_store_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_CODE_GENERATOR_OB_CODE_GENERATOR_ */
