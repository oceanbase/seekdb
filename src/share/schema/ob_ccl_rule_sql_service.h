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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_CCL_RULE_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_CCL_RULE_SQL_SERVICE_H_

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
class ObCCLRuleSchema;

class ObCCLRuleSqlService : public ObDDLSqlService
{
public:
  ObCCLRuleSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObCCLRuleSqlService() {}

  virtual int insert_ccl_rule(const ObCCLRuleSchema &ccl_rule_schema,
                              common::ObISQLClient &sql_client,
                              const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_ccl_rule(const ObCCLRuleSchema &ccl_rule_schema,
                              const int64_t new_schema_version,
                              common::ObISQLClient &sql_client,
                              const common::ObString *ddl_stmt_str = NULL);

private:
  int gen_sql(ObSqlString &sql, ObSqlString &values, const ObCCLRuleSchema &ccl_rule_schema);
  static constexpr int THE_SYS_TABLE_IDX = 0;
  static constexpr int THE_HISTORY_TABLE_IDX = 1;
  static const char *CCL_RULE_TABLES[2];
private:
  DISALLOW_COPY_AND_ASSIGN(ObCCLRuleSqlService);
};


} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_CCL_RULE_SQL_SERVICE_H_
