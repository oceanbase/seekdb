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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_SCHEDULER_MYSQL_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_SCHEDULER_MYSQL_H_

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace pl
{

class ObDBMSSchedulerMysql
{
public:
  ObDBMSSchedulerMysql() {}
  virtual ~ObDBMSSchedulerMysql() {}

public:
#define DECLARE_FUNC(func) \
  static int func(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  DECLARE_FUNC(disable);
  DECLARE_FUNC(enable);
  DECLARE_FUNC(set_attribute);
  DECLARE_FUNC(get_and_increase_job_id);

#undef DECLARE_FUNC

private:
  static int execute_sql(sql::ObExecContext &ctx, ObSqlString &sql, int64_t &affected_rows);
  static int _generate_job_id(int64_t tenant_id, int64_t &max_job_id);
};

} // end of pl
} // end of oceanbase

#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_SCHEDULER_MYSQL_H_ */
