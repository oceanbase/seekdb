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

#ifndef _OB_SET_COMMENT_RESOLVER_H
#define _OB_SET_COMMENT_RESOLVER_H 1

#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "sql/resolver/ddl/ob_alter_table_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObSetCommentResolver : public ObDDLResolver
{
public:
  explicit ObSetCommentResolver(ObResolverParams &params);
  virtual ~ObSetCommentResolver();
  virtual int resolve(const ParseNode &parse_tree);
  ObAlterTableStmt *get_alter_table_stmt() { return static_cast<ObAlterTableStmt*>(stmt_); };
private:
  share::schema::ObTableSchema table_schema_;
  common::ObCollationType collation_type_;
  common::ObCharsetType charset_type_;
  DISALLOW_COPY_AND_ASSIGN(ObSetCommentResolver);
};
}//namespace sql
}//namespace oceanbase
#endif
