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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_savepoint_stmt.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

int ObSavePointStmt::set_sp_name(const char *str_value, int64_t str_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(str_value) || str_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid savepoint name", K(ret), KP(str_value), K(str_len));
  } else {
    sp_name_.assign_ptr(str_value, static_cast<int32_t>(str_len));
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

