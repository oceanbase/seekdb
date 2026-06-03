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

#include "ob_redis_importer.h"
#include "lib/string/ob_sql_string.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "observer/ob_server_struct.h"
#include "sql/session/ob_basic_session_info.h"
#include "share/system_variable/ob_system_variable_alias.h"
#include "sql/engine/ob_exec_context.h"

#define USING_LOG_PREFIX SERVER

namespace oceanbase
{
using namespace share;
using namespace sql;
using namespace common;
using namespace obrpc;
namespace table
{

bool ObModuleDataArg::is_valid() const
{
  return op_ > ObModuleDataArg::INVALID_OP
      && op_ < ObModuleDataArg::MAX_OP
      && target_tenant_id_ != OB_INVALID_TENANT_ID
      && module_ > ObModuleDataArg::INVALID_MOD
      && module_ < ObModuleDataArg::MAX_MOD;
}

int ObRedisImporter::exec_op(table::ObModuleDataArg::ObInfoOpType op)
{
  UNUSED(op);
  return OB_NOT_SUPPORTED;
}

int ObRedisImporter::get_sql_uint_result(const char *sql, const char *col_name, uint64_t &sql_res)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(exec_ctx_.get_sql_proxy())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy must not null", K(ret), KP(exec_ctx_.get_sql_proxy()));
  } else {
    ObCommonSqlProxy *sql_proxy = exec_ctx_.get_sql_proxy();
    HEAP_VAR(ObMySQLProxy::MySQLResult, res)
    {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(sql_proxy->read(res, tenant_id_, sql))) {
        SHARE_LOG(WARN, "failed to read", K(ret), K(tenant_id_), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        SHARE_LOG(WARN, "failed to get sql result", K(ret));
      } else if (OB_FAIL(result->next())) {
        // should has one line
        LOG_WARN("fail to get next result", K(ret));
      } else {
        ObObjMeta meta;
        if (OB_FAIL(result->get_type(col_name, meta))) {
          LOG_WARN("fail to get type", K(ret), K(col_name));
        } else if (meta.is_number()) {
          common::number::ObNumber num;
          if (OB_FAIL(result->get_number(col_name, num))) {
            LOG_WARN("fail to get column in row. ", "column_name", col_name, K(ret));
          } else if (OB_FAIL(ObJsonBaseUtil::number_to_uint(num, sql_res))) {
            LOG_WARN("fail to convert number to uint", K(ret), K(num));
          }
        } else if (meta.is_int()) {
          int64_t int_res = 0;
          if (OB_FAIL(result->get_int(col_name, int_res))) {
            LOG_WARN("fail to get column in row. ", "column_name", col_name, K(ret));
          } else if (int_res < 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected sql res", K(ret), K(int_res));
          } else {
            sql_res = static_cast<uint64_t>(int_res);
          }
        }
      }
    }
  }
  return ret;
}

}  // end namespace table
}  // namespace oceanbase
