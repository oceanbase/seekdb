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

#include "observer/table_load/client/ob_table_direct_load_exec_context.h"
#include "observer/table/ob_table_rpc_processor.h"
#include "share/table/ob_table_rpc_proxy.h"

namespace oceanbase
{
namespace observer
{

/// @see RPC_S(PR5 direct_load, obrpc::OB_TABLE_API_DIRECT_LOAD, (table::ObTableDirectLoadRequest), table::ObTableDirectLoadResult);
class ObTableDirectLoadP : public ObTableRpcProcessor<obrpc::ObTableRpcProxy::ObRpc<obrpc::OB_TABLE_API_DIRECT_LOAD>>
{
  typedef ObTableRpcProcessor<obrpc::ObTableRpcProxy::ObRpc<obrpc::OB_TABLE_API_DIRECT_LOAD>> ParentType;
public:
  explicit ObTableDirectLoadP(const ObGlobalContext &gctx);
  virtual ~ObTableDirectLoadP() = default;
protected:
  int check_arg() override;
  int try_process() override;
  table::ObTableAPITransCb *new_callback(rpc::ObRequest *req) override;
  uint64_t get_request_checksum() override;
  virtual table::ObTableEntityType get_entity_type() override { return table::ObTableEntityType::ET_DYNAMIC; }
  virtual bool is_kv_processor() override { return false; }
private:
  ObTableDirectLoadExecContext exec_ctx_;
};

} // namespace observer
} // namespace oceanbase
