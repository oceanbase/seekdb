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

#ifndef OCEANBASE_SQL_OB_CREATE_TABLEGROUP_RESOLVER_
#define OCEANBASE_SQL_OB_CREATE_TABLEGROUP_RESOLVER_
#include "sql/resolver/ddl/ob_create_tablegroup_stmt.h"
#include "sql/resolver/ddl/ob_tablegroup_resolver.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace sql
{
class ObCreateTablegroupResolver: public ObTableGroupResolver
{
  static const int64_t IF_NOT_EXIST = 0;
  static const int64_t TG_NAME = 1;
  static const int64_t TABLEGROUP_OPTION = 2;
  static const int64_t PARTITION_OPTION = 3;
public:
  explicit ObCreateTablegroupResolver(ObResolverParams &params);
  virtual ~ObCreateTablegroupResolver();
  virtual int resolve(const ParseNode &parse_tree);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObCreateTablegroupResolver);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_OB_CREATE_TABLEGROUP_RESOLVER_ */
