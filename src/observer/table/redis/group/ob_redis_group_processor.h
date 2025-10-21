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
#include "observer/table/group/ob_i_table_struct.h"
#include "observer/table/redis/cmd/ob_redis_cmd.h"
#include "observer/table/redis/ob_redis_context.h"
namespace oceanbase
{
namespace table
{
class ObRedisGroupOpProcessor : public ObITableOpProcessor
{
public:
  ObRedisGroupOpProcessor() 
    : ObITableOpProcessor(),
      allocator_(nullptr),
      processor_entity_factory_("RedisProrEntFac", MTL_ID())
  {}

  ObRedisGroupOpProcessor(
      ObTableGroupType op_type,
      ObTableGroupCtx *group_ctx,
      ObIArray<ObITableOp *> *ops,
      ObTableCreateCbFunctor *functor)
      : ObITableOpProcessor(op_type, group_ctx, ops, functor),
        allocator_(nullptr),
        processor_entity_factory_("RedisProrEntFac", MTL_ID())
  {}
  virtual ~ObRedisGroupOpProcessor() {}
  virtual int init(ObTableGroupCtx &group_ctx, ObIArray<ObITableOp*> *ops) override;
  virtual int process() override;
  int is_valid();

private:
  int init_batch_ctx(ObRedisBatchCtx &batch_ctx);
  int set_group_need_dist_das(ObRedisBatchCtx &batch_ctx);
  int end_trans( ObRedisBatchCtx &redis_ctx, bool need_snapshot, bool is_rollback);
private:
  common::ObIAllocator *allocator_;
  table::ObTableEntityFactory<table::ObTableEntity> processor_entity_factory_;
};

}  // namespace table
}  // namespace oceanbase
