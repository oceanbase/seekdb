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

#ifndef OCEANBASE_SQL_TRANS_UTIL_
#define OCEANBASE_SQL_TRANS_UTIL_

#include "share/ob_define.h"
#include "sql/ob_sql_define.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
class ObSqlTransUtil
{
public:
  /* Determine whether a statement should start a transaction remotely */
  static bool is_remote_trans(bool ac, bool in_trans, ObPhyPlanType ptype)
  {
    return true == ac && false == in_trans && OB_PHY_PLAN_REMOTE == ptype;
  }

  /* Determine if the transaction can be automatically started */
  static bool plan_can_start_trans(bool ac, bool in_trans)
  {
    UNUSED(ac);
    return false == in_trans;
  }

  /* Determine if the current transaction can be automatically ended */
  static bool plan_can_end_trans(bool ac, bool explicit_start_trans)
  {
    return false == explicit_start_trans && true == ac;
  }

  /* Determine if cmd can automatically end the previous transaction */
  static bool cmd_need_new_trans(bool ac, bool in_trans)
  {
    UNUSED(ac);
    return true == in_trans;
  }
private:
  ObSqlTransUtil() {};
  ~ObSqlTransUtil() {};
  DISALLOW_COPY_AND_ASSIGN(ObSqlTransUtil);
};
}
}
#endif /* OCEANBASE_SQL_TRANS_UTIL_ */
//// end of header file
