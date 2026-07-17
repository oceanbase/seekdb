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

#ifndef OCEANBASE_SQL_RESOLVER_DROP_INDEX_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_DROP_INDEX_RESOLVER_

#include "lib/hash/ob_placement_hashset.h"
#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObDropIndexResolver : public ObDDLResolver
{
public:
  explicit ObDropIndexResolver(ObResolverParams &params);
  virtual ~ObDropIndexResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropIndexResolver);
};
}  // namespace sql
}  // namespace oceanbase
#endif
