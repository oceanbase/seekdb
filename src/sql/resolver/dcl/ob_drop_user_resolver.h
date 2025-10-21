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

#ifndef OCEANBASE_SQL_RESOLVER_DCL_OB_DROP_USER_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_DCL_OB_DROP_USER_RESOLVER_
#include "sql/resolver/dcl/ob_dcl_resolver.h"
#include "sql/resolver/dcl/ob_drop_user_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObDropUserResolver: public ObDCLResolver
{
public:
  explicit ObDropUserResolver(ObResolverParams &params);
  virtual ~ObDropUserResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObDropUserResolver);
};

} // end namespace sql
} // end namespace oceanbase
#endif
