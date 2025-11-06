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

#include "ob_end_trans_resolver.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObEndTransResolver::ObEndTransResolver(ObResolverParams &params)
    : ObTCLResolver(params)
{
}

ObEndTransResolver::~ObEndTransResolver()
{
}

int ObEndTransResolver::resolve(const ParseNode &parse_node)
{
  int ret = OB_SUCCESS;
  ObEndTransStmt *end_stmt = NULL;
  if (OB_LIKELY((T_COMMIT == parse_node.type_ || T_ROLLBACK == parse_node.type_)
                && parse_node.num_child_ == 1)) {
    if (OB_UNLIKELY(NULL == (end_stmt = create_stmt<ObEndTransStmt>()))) {
      ret = OB_SQL_RESOLVER_NO_MEMORY;
      LOG_WARN("failed to create select stmt");
    } else {
      stmt_ = end_stmt;
      end_stmt->set_is_rollback(T_ROLLBACK == parse_node.type_);
      auto hint = parse_node.children_[0];
      if (hint) {
        end_stmt->set_hint(ObString(hint->str_len_, hint->str_value_));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected parse node", K(parse_node.type_), K(parse_node.num_child_));
  }
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */


