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

#include "ob_anonymous_block_stmt.h"

namespace oceanbase
{
namespace sql
{
int ObAnonymousBlockStmt::add_param(const ObObjParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited array", K(ret));
  } else if (OB_FAIL(params_->push_back(param))) {
    LOG_WARN("fail to push back param", K(ret));
  }
  return ret;
}
}
}
