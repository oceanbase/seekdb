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

#ifndef OB_UDF_SQL_SERVICE_H_
#define OB_UDF_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"

namespace oceanbase
{
namespace common
{
class ObString;
class ObISQLClient;
}
namespace share
{
namespace schema
{
class ObUDF;

class ObUDFSqlService : public ObDDLSqlService
{
public:
  ObUDFSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObUDFSqlService() {}

  virtual int insert_udf(const ObUDF &udf_info,
                         common::ObISQLClient *sql_client,
                         const common::ObString *ddl_stmt_str = NULL);
  /*virtual int replace_udf(const ObUDFInfo &udf_info,
    common::ObISQLClient *sql_client,
    const common::ObString *ddl_stmt_str = NULL);
    virtual int alter_udf(const ObUDFInfo &udf_info, 
    common::ObISQLClient *sql_client, 
    const common::ObString *ddl_stmt_str = NULL); */
  virtual int delete_udf(const uint64_t tenant_id,
                         const common::ObString &name,
                         const int64_t new_schema_version,
                         common::ObISQLClient *sql_client,
                         const common::ObString *ddl_stmt_str = NULL);

  virtual int drop_udf(const ObUDF &udf_info,
                       const int64_t new_schema_version,
                       common::ObISQLClient *sql_client,
                       const common::ObString *ddl_stmt_str = NULL);

private:
  int add_udf(common::ObISQLClient &sql_client, 
              const ObUDF &udf_info,
              const bool only_history = false);
private:
  DISALLOW_COPY_AND_ASSIGN(ObUDFSqlService);
};


} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif
