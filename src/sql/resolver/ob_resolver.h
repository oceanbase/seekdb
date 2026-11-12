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

#ifndef OCEANBASE_SQL_RESOLVER_OB_RESOLVER
#define OCEANBASE_SQL_RESOLVER_OB_RESOLVER

#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
class ParsedSql;

/// the interface of this module
class ObResolver
{
public:
  explicit ObResolver(ObResolverParams &params);
  virtual ~ObResolver();

  int resolve(const ParsedSql &parsed, ObStmt *&stmt);
  int resolve_anonymous(const ParsedSql &parsed, ObStmt *&stmt);
  int resolve(const peg::Node &statement, ObStmt *&stmt);
  ObResolverParams &get_params() { return params_; }

private:
  template <typename ResolverType, typename Syntax>
  int stmt_resolver_func(ObResolverParams &params, const Syntax &parse_tree, ObStmt *&stmt);

  template <typename SelectResolverType>
  int select_stmt_resolver_func(ObResolverParams &params, const peg::Node &parse_tree, ObStmt *&stmt);
  int check_parameters(const ParsedSql &parsed);
  int finish_resolution(ObStmt *stmt);
private:
  // data members
  ObResolverParams params_;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObResolver);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_RESOLVER_OB_RESOLVER */
