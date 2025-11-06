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
#include "sql/resolver/cmd/ob_variable_set_stmt.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
int ObVariableSetStmt::get_variable_node(int64_t index,
                                         ObVariableSetStmt::VariableSetNode &var_node) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(index < 0 || index >= variable_nodes_.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index", K(index), K(ret));
  } else if (OB_FAIL(variable_nodes_.at(index, var_node))) {
    LOG_WARN("fail to get variable_nodes", K(index), K(ret));
  } else {}
  return ret;
}

}//end of namespace sql
}//end of namespace oceanbase 
