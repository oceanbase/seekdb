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

#ifndef SQL_RESOLVER_CMD_OB_TRIGGER_STORAGE_CACHE_STMT_H_
#define SQL_RESOLVER_CMD_OB_TRIGGER_STORAGE_CACHE_STMT_H_

#include "sql/resolver/cmd/ob_cmd_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObTriggerStorageCacheStmt : public ObCMDStmt
{
public:
  ObTriggerStorageCacheStmt() : 
      ObCMDStmt(stmt::T_TRIGGER_STORAGE_CACHE),
      rpc_arg_()
  {}
  virtual ~ObTriggerStorageCacheStmt() {}
  obrpc::ObTriggerStorageCacheArg &get_rpc_arg() { return rpc_arg_; }
  void set_storage_cache_op(const obrpc::ObTriggerStorageCacheArg::ObStorageCacheOp op) { rpc_arg_.op_ = op; }
  void set_tenant_id(const uint64_t tenant_id) { rpc_arg_.tenant_id_ = tenant_id; }
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
public:
  obrpc::ObTriggerStorageCacheArg rpc_arg_;
};
} /* sql */
} /* oceanbase */

#endif //OCEANBASE_SQL_OB_TENANT_SNAPSHOT_STMT_H_
