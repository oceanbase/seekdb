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

#define USING_LOG_PREFIX SQL_PC

#include "sql/plan_cache/ob_param_value_formatter.h"

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_check_macros.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{

int store_params_value_to_str(common::ObIAllocator &allocator,
                              ObSQLSessionInfo &session,
                              common::ParamStore *params,
                              char *&params_value,
                              int64_t &params_value_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t length = OB_MAX_SQL_LENGTH;
  CK(OB_NOT_NULL(params));
  CK(OB_ISNULL(params_value));
  CK(OB_NOT_NULL(params_value = static_cast<char *>(allocator.alloc(length))));
  for (int64_t i = 0; OB_SUCC(ret) && i < params->count(); ++i) {
    const common::ObObjParam &param = params->at(i);
    if (param.is_ext()) {
      pos = 0;
      params_value = nullptr;
      params_value_len = 0;
      break;
    } else {
      OZ(param.print_sql_literal(params_value, length, pos, allocator, TZ_INFO(&session)));
      if (i != params->count() - 1) {
        OZ(databuff_printf(params_value, length, pos, allocator, ","));
      }
    }
  }
  if (OB_FAIL(ret)) {
    params_value = nullptr;
    params_value_len = 0;
    // Formatting is diagnostic-only and must not affect statement execution.
    ret = OB_SUCCESS;
  } else {
    params_value_len = pos;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
