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

#ifndef OCEANBASE_ROOTSERVER_OB_LOCATION_DDL_OPERATOR_H_
#define OCEANBASE_ROOTSERVER_OB_LOCATION_DDL_OPERATOR_H_

#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_location_sql_service.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace rootserver
{
class ObLocationDDLOperator
{
public:
  ObLocationDDLOperator(share::schema::ObMultiVersionSchemaService &schema_service,
                      common::ObMySQLProxy &sql_proxy)
    : schema_service_(schema_service),
      sql_proxy_(sql_proxy)
  {}
  virtual ~ObLocationDDLOperator() {}
  int create_location(const ObString &ddl_str,
                      const uint64_t user_id,
                      share::schema::ObLocationSchema &schema,
                      common::ObMySQLTransaction &trans);
  int alter_location(const ObString &ddl_str,
                     share::schema::ObLocationSchema &schema,
                     common::ObMySQLTransaction &trans);
  int drop_location(const ObString &ddl_str,
                    share::schema::ObLocationSchema &schema,
                    common::ObMySQLTransaction &trans);
private:
  share::schema::ObMultiVersionSchemaService &schema_service_;
  common::ObMySQLProxy &sql_proxy_;
};

}//end namespace rootserver
}//end namespace oceanbase
#endif //OCEANBASE_ROOTSERVER_OB_LOCATION_DDL_OPERATOR_H_
