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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_EXTERNAL_TABLE_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_EXTERNAL_TABLE_H_
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace pl
{

class ObDBMSExternalTable
{
public:
  static int auto_refresh_external_table(
    sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
};

} // end of pl
} // end of oceanbase
#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_EXTERNAL_TABLE_H_ */
