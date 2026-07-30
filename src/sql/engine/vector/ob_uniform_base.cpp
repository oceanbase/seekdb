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

#define USING_LOG_PREFIX SHARE
#include "sql/engine/vector/ob_uniform_base.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
namespace common
{
DEF_TO_STRING(ObUniformBase)
{
  int64_t pos = 0;
  J_OBJ_START();
  BUF_PRINTF("eval_info: ");
  pos += eval_info_->to_string(buf + pos, buf_len - pos);
  J_OBJ_END();
  return pos;
}
}
}
