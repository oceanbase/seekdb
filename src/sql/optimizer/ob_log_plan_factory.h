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

#ifndef _OB_LOG_PLAN_FACTORY_H
#define _OB_LOG_PLAN_FACTORY_H 1
#include "share/ob_define.h"
#include "lib/list/ob_obj_store.h"
namespace oceanbase
{
namespace common
{
class ObIAllocator;
}  // namespace common

namespace sql
{
class ObLogPlan;
class ObOptimizerContext;
class ObDMLStmt;
class ObLogPlanFactory
{
public:
  explicit ObLogPlanFactory(common::ObIAllocator &allocator);
  ~ObLogPlanFactory();
  ObLogPlan *create(ObOptimizerContext &ctx, const ObDMLStmt &stmt);
  void destroy();
private:
  common::ObIAllocator &allocator_;
  common::ObObjStore<ObLogPlan*, common::ObIAllocator&, true> plan_store_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogPlanFactory);
};
}
}
#endif // _OB_LOG_PLAN_FACTORY_H
