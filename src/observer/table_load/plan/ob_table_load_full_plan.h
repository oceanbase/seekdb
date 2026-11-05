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

#pragma once

#include "observer/table_load/plan/ob_table_load_plan.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadFullPlan final : public ObTableLoadPlan
{
public:
  ObTableLoadFullPlan(ObTableLoadStoreCtx *store_ctx) : ObTableLoadPlan(store_ctx) {}
  virtual ~ObTableLoadFullPlan() = default;
  int generate() override;

  static int create_plan(ObTableLoadStoreCtx *store_ctx, ObIAllocator &allocator,
                         ObTableLoadPlan *&plan);

private:
  int alloc_channel(ObTableLoadTableOp *up_table_op, ObTableLoadTableOp *down_table_op,
                    ObTableLoadTableChannel *&table_channel) override
  {
    return OB_ERR_UNEXPECTED;
  }
};

} // namespace observer
} // namespace oceanbase
