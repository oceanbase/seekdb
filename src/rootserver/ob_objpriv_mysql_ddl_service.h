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

#ifndef _OCEANBASE_ROOTSERVER_OB_OBJPRIV_MYSQL_DDL_SERVICE_H_
#define _OCEANBASE_ROOTSERVER_OB_OBJPRIV_MYSQL_DDL_SERVICE_H_

#include "rootserver/ob_ddl_service.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_service.h"


namespace oceanbase
{
namespace rootserver
{
class ObObjPrivMysqlDDLService
{
public:
ObObjPrivMysqlDDLService(ObDDLService *ddl_service)
    : ddl_service_(ddl_service)
  {}
  virtual ~ObObjPrivMysqlDDLService() {}
  int grant_object(const share::schema::ObObjMysqlPrivSortKey &object_key,
                   const ObPrivSet priv_set,
                   const uint64_t option,
                   share::schema::ObSchemaGetterGuard &schema_guard,
                   const common::ObString &grantor = "",
                   const common::ObString &grantor_host = "");
  int revoke_object(const share::schema::ObObjMysqlPrivSortKey &object_key,
                    const ObPrivSet priv_set,
                    const common::ObString &grantor = "",
                    const common::ObString &grantor_host = "");
private:
  ObDDLService *ddl_service_;
};
}
}
#endif // _OCEANBASE_ROOTSERVER_OB_OBJPRIV_MYSQL_DDL_SERVICE_H_
