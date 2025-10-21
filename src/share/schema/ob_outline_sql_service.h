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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_OUTLINE_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_OUTLINE_SQL_SERVICE_H_

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
class ObOutlineInfo;

class ObOutlineSqlService : public ObDDLSqlService
{
public:
  ObOutlineSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObOutlineSqlService() {}

  virtual int insert_outline(const ObOutlineInfo &outline_info,
                             common::ObISQLClient &sql_client,
                             const common::ObString *ddl_stmt_str = NULL);
  virtual int replace_outline(const ObOutlineInfo &outline_info,
                             common::ObISQLClient &sql_client,
                             const common::ObString *ddl_stmt_str = NULL);
  virtual int alter_outline(const ObOutlineInfo &outline_info,
                             common::ObISQLClient &sql_client,
                             const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_outline(const uint64_t tenant_id,
                             const uint64_t database_id,
                             const uint64_t outline_id,
                             const int64_t new_schema_version,
                             common::ObISQLClient &sql_client,
                             const common::ObString *ddl_stmt_str = NULL);
private:
  int add_outline(common::ObISQLClient &sql_client, const ObOutlineInfo &outline_info,
                  const bool only_history = false);
private:
  DISALLOW_COPY_AND_ASSIGN(ObOutlineSqlService);
};


} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_OUTLINE_SQL_SERVICE_H_
