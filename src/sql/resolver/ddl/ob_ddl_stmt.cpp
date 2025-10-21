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

#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "sql/ob_sql_context.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
int ObDDLStmt::get_first_stmt(ObString &first_stmt)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObStmt::get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt", K(ret));
  } else if (OB_ISNULL(get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("query ctx is null", K(ret));
  } else if (OB_FAIL(ObCharset::charset_convert(allocator_,
                                                first_stmt,
                                                get_query_ctx()->get_sql_stmt_coll_type(),
                                                ObCharset::get_system_collation(),
                                                first_stmt,
                                                ObCharset::REPLACE_UNKNOWN_CHARACTER_ON_SAME_CHARSET))) {
    LOG_WARN("fail to convert charset", K(ret), K(first_stmt),
             "stmt collation type", get_query_ctx()->get_sql_stmt_coll_type());
  }

  return ret;
}
}
}
