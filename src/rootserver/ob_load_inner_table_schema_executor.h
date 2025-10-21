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

#ifndef OCEANBASE_ROOTSERVER_OB_LOAD_INNER_TABLE_SCHEMA_EXECUTOR_H_
#define OCEANBASE_ROOTSERVER_OB_LOAD_INNER_TABLE_SCHEMA_EXECUTOR_H_

#include "share/ob_rpc_struct.h"
#include "rootserver/ob_rs_async_rpc_proxy.h"
#include "share/ob_srv_rpc_proxy.h"

namespace oceanbase
{
namespace share
{
class ObLoadInnerTableSchemaInfo;
}
namespace rootserver
{

class ObLoadInnerTableSchemaExecutor
{
public:
  static int load_inner_table_schema(const obrpc::ObLoadTenantTableSchemaArg &arg);
  static int load_core_schema_version(const uint64_t tenant_id, common::ObISQLClient &client, const int64_t core_schema_version);
private:
  static int load_inner_table_schema(const obrpc::ObLoadTenantTableSchemaArg &arg,
      const share::ObLoadInnerTableSchemaInfo &info);

public:
  ObLoadInnerTableSchemaExecutor() : tenant_id_(OB_INVALID_TENANT_ID), rpc_proxy_(nullptr), inited_(false),
    args_(), next_arg_index_(0), load_rpc_timeout_(0), parallel_count_(0) {}
  int init(ObIArray<ObTableSchema> &table_schemas, const uint64_t tenant_id,
      const int64_t max_cpu, obrpc::ObSrvRpcProxy *rpc_proxy);
  int execute();
private:
  int init_args_(ObIArray<ObTableSchema> &table_schemas);
  int append_arg(const ObIArray<int64_t> &insert_idx, const share::ObLoadInnerTableSchemaInfo &info);
  int call_next_arg_(ObLoadTenantTableSchemaProxy& proxy);

private:
  static const int64_t LOAD_ROWS_PER_BATCH = 1000;
  static const int64_t LOAD_ROWS_PER_INSERT = 100;
  static const int64_t WAIT_THREAD_FREE_TIME = 10_ms;
  static const int64_t THREAD_PER_CPU = 1; // should equal to the default value of parameter cpu_quota_concurrency
private:
  uint64_t tenant_id_;
  obrpc::ObSrvRpcProxy *rpc_proxy_;
private:
  bool inited_;
  ObArray<obrpc::ObLoadTenantTableSchemaArg> args_;
  int64_t next_arg_index_;
  int64_t load_rpc_timeout_;
  int64_t parallel_count_;
  ObArenaAllocator allocator_;
  ObArray<share::ObLoadInnerTableSchemaInfo> infos_;
};
}
}

#endif // OCEANBASE_ROOTSERVER_OB_LOAD_INNER_TABLE_SCHEMA_EXECUTOR_H_
