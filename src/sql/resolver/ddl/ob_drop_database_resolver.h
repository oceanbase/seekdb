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

#ifndef OCEANBASE_SQL_OB_DROP_DATABASE_RESOLVER_H
#define OCEANBASE_SQL_OB_DROP_DATABASE_RESOLVER_H
#include "sql/resolver/ddl/ob_ddl_resolver.h"
namespace oceanbase
{
namespace sql
{
class ObDropDatabaseResolver: public ObDDLResolver
{
public:
  const static int64_t IF_EXIST = 0;
  const static int64_t DBNAME = 1;
  const static int64_t DB_NODE_COUNT = 2;

  explicit ObDropDatabaseResolver(ObResolverParams &params);
  virtual ~ObDropDatabaseResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObDropDatabaseResolver);

private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_OB_DROP_DATABASE_RESOLVER_H */
