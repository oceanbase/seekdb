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
