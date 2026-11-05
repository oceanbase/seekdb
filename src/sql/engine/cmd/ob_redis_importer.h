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

#include "share/ob_module_data_arg.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
namespace oceanbase { namespace table { enum class ObKvModeType; } }
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace table
{

class ObRedisImporter
{
public:
  explicit ObRedisImporter(sql::ObExecContext& exec_ctx)
      : exec_ctx_(exec_ctx), affected_rows_(0)
  {}
  virtual ~ObRedisImporter() {}
  int exec_op(table::ObModuleDataArg::ObInfoOpType op);
  OB_INLINE int64_t get_affected_rows() { return affected_rows_; }

private:
  int get_sql_uint_result(const char *sql, const char *col_name, uint64_t &sql_res);
  int get_tenant_memory_size(uint64_t &memory_size);

  sql::ObExecContext& exec_ctx_;
  int64_t affected_rows_;
};

}  // namespace table
}  // namespace oceanbase
#endif
