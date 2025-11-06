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
#include "sql/resolver/ddl/ob_set_comment_resolver.h"

namespace oceanbase
{
using namespace common;

namespace sql
{
ObSetCommentResolver::ObSetCommentResolver(ObResolverParams &params)
    : ObDDLResolver(params),
      table_schema_(NULL),
      collation_type_(CS_TYPE_INVALID),
      charset_type_(CHARSET_INVALID)
{
}

ObSetCommentResolver::~ObSetCommentResolver()
{
}

int ObSetCommentResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  CHECK_COMPATIBILITY_MODE(session_info_);
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "session_info should not be null", K(ret));
  } else if (OB_ISNULL(parse_tree.children_)
      || ((T_SET_TABLE_COMMENT != parse_tree.type_)
         && (T_SET_COLUMN_COMMENT != parse_tree.type_))) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "invalid parse tree", K(ret));
  } else {
    // do-nothing for non-oracle mode
  }
  return ret;
}


} //namespace sql
} //namespace oceanbase
