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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_LIMIT_CALCULATOR_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_LIMIT_CALCULATOR_H_

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace share
{
struct ObUserResourceCalculateArg;
}
namespace pl
{

class ObDBMSLimitCalculator
{
public:
  static int phy_res_calculate_by_logic_res(
      sql::ObExecContext &ctx,
      sql::ParamStore &params,
      common::ObObj &result);
  static int phy_res_calculate_by_unit(
      sql::ObExecContext &ctx,
      sql::ParamStore &params,
      common::ObObj &result);
  static int phy_res_calculate_by_standby_tenant(
      sql::ObExecContext &ctx,
      sql::ParamStore &params,
      common::ObObj &result);
private:
  static int parse_dict_like_args_(
      const char* ptr,
      share::ObUserResourceCalculateArg &arg);
  static int get_json_result_(
      const ObMinPhyResourceResult &res,
      char *buf,
      const int64_t buf_len,
      int64_t &pos);
  static int get_json_result_(
      const int64_t tenant_id,
      const ObAddr &addr,
      const ObMinPhyResourceResult &res,
      char *buf,
      const int64_t buf_len,
      int64_t &pos);
};

} // namespace pl
} // namespace oceanbase

#endif
