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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_
#define OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_

#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlan;
class ObExecContext;
class ObPhyOperator;

class ObExecutor
{
public:
  ObExecutor()
    : inited_(false),
      phy_plan_(NULL),
      execution_id_(common::OB_INVALID_ID)
  {
    /* add your code here */
  }
  ~ObExecutor() {};
  int init(ObPhysicalPlan *plan);
  void reset();
  int execute_plan(ObExecContext &ctx);
  int close(ObExecContext &ctx);
private:
  // disallow copy
  ObExecutor(const ObExecutor &other);
  ObExecutor &operator=(const ObExecutor &ohter);
private:
  int execute_remote_single_partition_plan(ObExecContext &ctx);
  int execute_distributed_plan(ObExecContext &ctx);
  int execute_static_cg_px_plan(ObExecContext &ctx);
private:
  bool inited_;
  ObPhysicalPlan *phy_plan_;
  // Used for distributed scheduler
  uint64_t execution_id_;
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_ */
