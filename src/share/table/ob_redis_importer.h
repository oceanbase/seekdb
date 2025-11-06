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

#ifndef OCEANBASE_SHARE_TABLE_OB_REDIS_IMPORTER_H_
#define OCEANBASE_SHARE_TABLE_OB_REDIS_IMPORTER_H_

#include "share/table/redis/ob_redis_common.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "observer/table/ob_table_mode_control.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace table
{
struct ObModuleDataArg
{
public:
  enum ObInfoOpType {
    INVALID_OP = -1,
    LOAD_INFO,
    CHECK_INFO,
    MAX_OP
  };
  enum ObExecModule {
    INVALID_MOD = -1,
    REDIS,
    TIMEZONE,
    GIS,
    MAX_MOD
  };
  ObModuleDataArg() : 
    op_(ObInfoOpType::INVALID_OP),
    target_tenant_id_(OB_INVALID_TENANT_ID),
    module_(ObExecModule::INVALID_MOD),
    file_path_()
  {}
  virtual ~ObModuleDataArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(op), K_(target_tenant_id), K_(module), K_(file_path));

  ObInfoOpType op_; // enum ObInfoOpType
  uint64_t target_tenant_id_;
  ObExecModule module_; // ObExecModule
  ObString file_path_;
};

class ObRedisImporter
{
public:
  explicit ObRedisImporter(uint64_t tenant_id, sql::ObExecContext& exec_ctx)
      : tenant_id_(tenant_id), exec_ctx_(exec_ctx), affected_rows_(0)
  {}
  virtual ~ObRedisImporter() {}
  int exec_op(table::ObModuleDataArg::ObInfoOpType op);
  OB_INLINE int64_t get_affected_rows() { return affected_rows_; }

private:
  int get_kv_mode(ObKvModeType &kv_mode_type);
  int get_tenant_memory_size(uint64_t &memory_size);
  int check_basic_info(bool &need_import);
  int get_sql_uint_result(const char *sql, const char *col_name, uint64_t &sql_res);
  int import_redis_info();
  int check_redis_info();

  uint64_t tenant_id_;
  sql::ObExecContext& exec_ctx_;
  int64_t affected_rows_;
};

}  // namespace table
}  // namespace oceanbase
#endif
