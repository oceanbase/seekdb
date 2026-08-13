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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PARAM_VALUE_FORMATTER_H_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PARAM_VALUE_FORMATTER_H_

#include "common/object/ob_object.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
class ObSQLSessionInfo;

int store_params_value_to_str(common::ObIAllocator &allocator,
                              ObSQLSessionInfo &session,
                              common::ParamStore *params,
                              char *&params_value,
                              int64_t &params_value_len);

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_PARAM_VALUE_FORMATTER_H_
