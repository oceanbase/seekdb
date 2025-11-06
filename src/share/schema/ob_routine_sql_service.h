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

#ifndef OCEANBASE_SRC_SHARE_SCHEMA_OB_ROUTINE_SQL_SERVICE_H_
#define OCEANBASE_SRC_SHARE_SCHEMA_OB_ROUTINE_SQL_SERVICE_H_
#include "share/schema/ob_ddl_sql_service.h"

namespace oceanbase
{
namespace common
{
class ObString;
class ObISQLClient;
} // namespace common
namespace share
{
class ObDMLSqlSplicer;
namespace schema
{
class ObRoutineInfo;
class ObRoutineParam;
class ObPackageInfo;

class ObRoutineSqlService : public ObDDLSqlService
{
public:
  explicit ObRoutineSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObRoutineSqlService() {}

  int create_routine(ObRoutineInfo &routine_info,
                     common::ObISQLClient *sql_client,
                     const common::ObString *ddl_stmt_str = NULL);
  int update_routine(ObRoutineInfo& routine_info,
                     ObISQLClient *sql_client);
  int replace_routine(ObRoutineInfo& routine_info,
                      const ObRoutineInfo *old_routine_info,
                      const int64_t del_param_schema_version,
                      common::ObISQLClient *sql_client,
                      const common::ObString *ddl_stmt_str = NULL);
  int drop_routine(const ObRoutineInfo &routine_info,
                   const int64_t new_schema_version,
                   common::ObISQLClient &sql_client,
                   const common::ObString *ddl_stmt_str = NULL);
  int create_package(ObPackageInfo &package_info,
                     common::ObISQLClient *sql_client,
                     bool is_replace,
                     const common::ObString *ddl_stmt_str = NULL);
  int alter_package(const ObPackageInfo &package_info,
                    common::ObISQLClient *sql_client,
                    const common::ObString *ddl_stmt_str);
  int drop_package(const uint64_t tenant_id,
                   const uint64_t database_id,
                   const uint64_t package_id,
                   const int64_t new_schema_version,
                   common::ObISQLClient &sql_client,
                   const common::ObString *ddl_stmt_str = NULL);
  int add_routine(common::ObISQLClient &sql_client,
                  const ObRoutineInfo &routine_info,
                  bool is_replace = false,
                  bool only_history = false);
private:
  int gen_package_dml(const uint64_t exec_tenant_id,
                      const ObPackageInfo &package_info,
                      ObDMLSqlSplicer &dml);
  int add_package(common::ObISQLClient &sql_client,
                  const ObPackageInfo &package_info,
                  bool is_replace,
                  bool only_history = false);
  int del_package(common::ObISQLClient &sql_client,
                  const uint64_t tenant_id,
                  const uint64_t package_id,
                  int64_t new_schema_version);
  int gen_routine_dml(const uint64_t exec_tenant_id,
                      const ObRoutineInfo &routine_info,
                      ObDMLSqlSplicer &dml,
                      bool is_replace = false);
  int del_routine(common::ObISQLClient &sql_client,
                  const ObRoutineInfo &routine_info,
                  int64_t new_schema_version);
  int gen_routine_param_dml(const uint64_t exec_tenant_id,
                            const ObRoutineParam &routine_param,
                            ObDMLSqlSplicer &dml);
  int add_routine_params(common::ObISQLClient &sql_client,
                         ObRoutineInfo &routine_info,
                         bool only_history = false);
  int del_routine_params(common::ObISQLClient &sql_client,
                         const ObRoutineInfo &routine_info,
                         int64_t new_schema_version);
private:
  DISALLOW_COPY_AND_ASSIGN(ObRoutineSqlService);
};
} //end of schema
} //end of share
} //end of oceanbase
#endif /* OCEANBASE_SRC_SHARE_SCHEMA_OB_ROUTINE_SQL_SERVICE_H_ */
