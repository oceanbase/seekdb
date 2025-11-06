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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_UPGRADE_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_UPGRADE_H_

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace pl
{

class ObDBMSUpgrade
{
public:
  ObDBMSUpgrade() {}
  virtual ~ObDBMSUpgrade() {}
public:
  static int upgrade_single(
    sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  static int upgrade_all(
    sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  static int get_job_action(ObSqlString &job_action, ObSqlString &query_sql);
  static int flush_dll_ncomp(
    sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
};

} // end of pl
} // end of oceanbase

#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_UPGRADE_H_ */
