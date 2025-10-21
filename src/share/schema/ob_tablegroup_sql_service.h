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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_TABLEGROUP_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_TABLEGROUP_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"

namespace oceanbase
{
namespace share
{
class ObDMLSqlSplicer;
namespace schema
{
class ObTablegroupSchema;

class ObTablegroupSqlService : public ObDDLSqlService
{
public:
  ObTablegroupSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObTablegroupSqlService() {}
  virtual int insert_tablegroup(const ObTablegroupSchema &tablegroup_schema,
                                common::ObISQLClient &sql_client,
                                const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_tablegroup(
          const ObTablegroupSchema &tablegroup_schema,
          const int64_t new_schema_version,
          common::ObISQLClient &sql_client,
          const common::ObString *ddl_stmt_str = NULL);
  virtual int update_tablegroup(ObTablegroupSchema &new_schema,
                                common::ObISQLClient &sql_client,
                                const common::ObString *ddl_stmt_str = NULL);
private:
  int add_tablegroup(common::ObISQLClient &sql_client,
                     const share::schema::ObTablegroupSchema &tablegroup,
                     const bool only_history);

  int gen_tablegroup_dml(const uint64_t exec_tenant_id,
                         const share::schema::ObTablegroupSchema &tablegroup_schema,
                         share::ObDMLSqlSplicer &dml);

  DISALLOW_COPY_AND_ASSIGN(ObTablegroupSqlService);
};


} //end of namespace share
} //end of namespace schema
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_TABLEGROUP_SQL_SERVICE_H_
