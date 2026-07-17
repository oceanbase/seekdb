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
#include "sql/resolver/xa/ob_xa_prepare_resolver.h"
#include "sql/resolver/xa/ob_xa_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObXaPrepareResolver::ObXaPrepareResolver(ObResolverParams &params) 
  : ObStmtResolver(params)
{
}

ObXaPrepareResolver::~ObXaPrepareResolver()
{
}

int ObXaPrepareResolver::resolve(const ParseNode &parse_node)
{
  int ret = OB_SUCCESS;
  ObXaPrepareStmt *xa_prepare_stmt = NULL;
  if (OB_UNLIKELY(T_XA_PREPARE != parse_node.type_ || 1 != parse_node.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected parse node", K(parse_node.type_), K(parse_node.num_child_), K(ret));
  } else if (OB_UNLIKELY(NULL == (xa_prepare_stmt = create_stmt<ObXaPrepareStmt>()))) {
    ret = OB_SQL_RESOLVER_NO_MEMORY;
    LOG_WARN("failed to create xa end stmt", K(ret));
  } else {
    ObString gtrid_string;
    ObString bqual_string;
    int64_t format_id = -1;
    if (OB_FAIL(ObResolverUtils::resolve_xid(parse_node.children_[0], gtrid_string, bqual_string, format_id))) {
      LOG_WARN("resolve xid failed", K(ret));
    } else {
      if(gtrid_string.length() <= 0) {
        ret = OB_TRANS_XA_INVAL;
        LOG_WARN("resolve xid failed, gtrid string can not be NULL", K(ret));
      } else {
        xa_prepare_stmt->set_xa_string(gtrid_string, bqual_string);
        if(format_id >= 0) {
          xa_prepare_stmt->set_format_id(format_id);
        }
      }
      LOG_DEBUG("xa prepare resolver", K(gtrid_string), K(bqual_string), K(format_id));
    }
  }
  return ret;
}

} // end namesapce sql
} // end namesapce oceanbase
