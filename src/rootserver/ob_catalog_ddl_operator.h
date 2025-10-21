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

#ifndef OCEANBASE_ROOTSERVER_OB_CATALOG_DDL_OPERATOR_H_
#define OCEANBASE_ROOTSERVER_OB_CATALOG_DDL_OPERATOR_H_

#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_catalog_sql_service.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
// namespace share
// {
// class ObCatalogSqlService;
// }

namespace rootserver
{
class ObCatalogDDLOperator
{
public:
  ObCatalogDDLOperator(share::schema::ObMultiVersionSchemaService &schema_service,
                       common::ObMySQLProxy &sql_proxy)
    : schema_service_(schema_service),
      sql_proxy_(sql_proxy)
  {}
  virtual ~ObCatalogDDLOperator() {}

  int handle_catalog_function(share::schema::ObCatalogSchema &schema,
                              common::ObMySQLTransaction &trans,
                              const share::schema::ObSchemaOperationType ddl_type,
                              const common::ObString &ddl_stmt_str,
                              bool if_exist,
                              bool if_not_exist,
                              share::schema::ObSchemaGetterGuard &schema_guard,
                              uint64_t user_id);
  int grant_or_revoke_after_ddl(ObCatalogSchema &schema,
                                ObMySQLTransaction &trans,
                                const share::schema::ObSchemaOperationType ddl_type,
                                ObSchemaGetterGuard &schema_guard,
                                uint64_t user_id);
  int grant_revoke_catalog(const ObCatalogPrivSortKey &catalog_priv_key,
                           const ObPrivSet priv_set,
                           const bool grant,
                           const common::ObString &ddl_stmt_str,
                           common::ObMySQLTransaction &trans);
private:
  share::schema::ObMultiVersionSchemaService &schema_service_;
  common::ObMySQLProxy &sql_proxy_;
};

}//end namespace rootserver
}//end namespace oceanbase
#endif //OCEANBASE_ROOTSERVER_OB_CATALOG_DDL_OPERATOR_H_
