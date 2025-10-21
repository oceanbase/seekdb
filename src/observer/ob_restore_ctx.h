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

#ifndef OCEANBASE_OBSERVER_OB_RESTORE_CTX_H_
#define OCEANBASE_OBSERVER_OB_RESTORE_CTX_H_

#include "observer/ob_restore_sql_modifier.h"
#include "share/ob_common_rpc_proxy.h"

namespace oceanbase
{
namespace obrpc
{
class ObCommonRpcProxy;
}
namespace common
{
class ObServerConfig;
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace sql
{
class ObSql;
}
namespace observer
{
class ObVTIterCreator;

class ObRestoreCtx
{
public:
  ObRestoreCtx()
    : schema_service_(NULL),
      sql_client_(NULL),
      ob_sql_(NULL),
      vt_iter_creator_(NULL),
      ls_table_operator_(NULL),
      server_config_(NULL),
      rs_rpc_proxy_(NULL)
  {}
  ~ObRestoreCtx() {}
  bool is_valid()
  {
    return NULL != schema_service_
        && NULL != sql_client_
        && NULL != ob_sql_
        && NULL != vt_iter_creator_
        && NULL != ls_table_operator_
        && NULL != server_config_
        && NULL != rs_rpc_proxy_;
  }
  share::schema::ObMultiVersionSchemaService *schema_service_;
  common::ObMySQLProxy *sql_client_;
  sql::ObSql *ob_sql_;
  ObVTIterCreator *vt_iter_creator_;
  const share::ObLSTableOperator *ls_table_operator_;
  common::ObServerConfig *server_config_;
  obrpc::ObCommonRpcProxy *rs_rpc_proxy_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRestoreCtx);
};

} // end namespace observer
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_RESTORE_CTX_H_
