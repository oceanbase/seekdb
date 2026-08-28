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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_DDL_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_DDL_SQL_SERVICE_H_

#include "lib/utility/ob_macro_utils.h"
#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace share
{
class ObDMLSqlSplicer;
namespace schema
{

struct ObSchemaOperation;
class ObDDLSqlService
{
public:
  ObDDLSqlService(ObSchemaService &schema_service)
    : schema_service_(schema_service){}
  virtual ~ObDDLSqlService() {}
  // Do nothing, simply push the schema version once
  int log_nop_operation(const ObSchemaOperation &schema_operation,
                        const int64_t new_schema_version,
                        const common::ObString &ddl_sql_str,
                        common::ObISQLClient &sql_client);

protected:
  virtual int log_operation(ObSchemaOperation &ddl_operation,
                            common::ObISQLClient &sql_client,
                            common::ObSqlString *public_sql_string = NULL);
  int log_operation_dml(
      const ObSchemaOperation &ddl_operation,
      share::ObDMLSqlSplicer &ddl_operation_dml);
  int gen_ddl_operation_dml(
      const ObSchemaOperation &ddl_operation,
      share::ObDMLSqlSplicer &ddl_operation_dml);
private:
  uint64_t fill_schema_id(const uint64_t schema_id);

protected:
  ObSchemaService &schema_service_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDDLSqlService);
};

struct TSILastOper {
  uint64_t last_operation_schema_version_;
  
  TSILastOper():
      last_operation_schema_version_(OB_INVALID_VERSION)
  {}
};

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_DDL_SQL_SERVICE_H_
