
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

#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_processor.h"
#include "share/table/ob_table_rpc_proxy.h"
#include "ob_table_rpc_processor.h"
#include "sql/plan_cache/ob_cache_object_factory.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "ob_table_op_wrapper.h"
#include "redis/ob_redis_service.h"

namespace oceanbase
{
namespace observer
{
/// @see RPC_S(PR5 redis_execute_v2, obrpc::OB_REDIS_EXECUTE_V2, (table::ObRedisRpcRequest),
/// table::ObRedisResult);
class ObRedisExecuteV2P : public ObTableRpcProcessor<obrpc::ObTableRpcProxy::ObRpc<obrpc::OB_REDIS_EXECUTE_V2> >
{
  typedef ObTableRpcProcessor<obrpc::ObTableRpcProxy::ObRpc<obrpc::OB_REDIS_EXECUTE_V2>> ParentType;

public:
  explicit ObRedisExecuteV2P(const ObGlobalContext &gctx);
  virtual ~ObRedisExecuteV2P() = default;
  virtual int deserialize() override;
  virtual int before_process();
  virtual int try_process() override;
  virtual int before_response(int error_code) override;
  virtual int response(const int retcode) override;

protected:
  virtual int check_arg() override;
  virtual void reset_ctx() override;
  virtual uint64_t get_request_checksum() override;
  virtual table::ObTableEntityType get_entity_type() override { return table::ObTableEntityType::ET_REDIS; }
  virtual bool is_kv_processor() override { return true; }

private:
  int init_redis_ctx();
  void init_redis_common(table::ObRedisCtx &ctx);

private:
  table::ObTableEntityFactory<table::ObTableEntity> default_entity_factory_;
  table::ObRedisSingleCtx redis_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObRedisExecuteV2P);
};

}  // end namespace observer
}  // end namespace oceanbase
