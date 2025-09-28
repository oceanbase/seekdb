/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
