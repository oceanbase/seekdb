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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_AI_SERVICE_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_AI_SERVICE_H_


#include "sql/ob_sql_define.h"
#include "lib/json_type/ob_json_base.h"
#include "sql/privilege_check/ob_ai_model_priv_util.h"

namespace oceanbase
{

namespace common
{

class ObObj;

} // namespace common

namespace pl
{

class ObPLExecCtx;

class ObDBMSAiService
{
public:
  static int create_ai_model(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result);
  static int drop_ai_model(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result);

  static int create_ai_model_endpoint(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result);
  static int alter_ai_model_endpoint(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result);
  static int drop_ai_model_endpoint(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result);
private:
  static int precheck_version_and_param_count_(int expect_param_count, sql::ParamStore &params);
  static int get_json_base_(ObArenaAllocator &allocator, sql::ParamStore &params, common::ObIJsonBase *&j_base);
  static int check_ai_model_privilege_(ObPLExecCtx &ctx, ObPrivSet required_priv);
};

} // namespace pl
} // namespace oceanbase

#endif // OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_AI_SERVICE_H_
