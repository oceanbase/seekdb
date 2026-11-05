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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_PARTITION_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_PARTITION_H_
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace pl
{
class ObDBMSPartition
{
public:
  static int manage_dynamic_partition(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
private:
  static int get_and_check_params_(const sql::ParamStore &params, common::ObString &precreate_time_str, common::ObArray<common::ObString> &time_unit_strs);
};

} // end of pl
} // end of oceanbase
#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_PARTITION_H_ */
