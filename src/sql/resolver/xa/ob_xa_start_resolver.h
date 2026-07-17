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

#ifndef _OB_XA_START_RESOLVER_
#define _OB_XA_START_RESOLVER_

#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObXaStartResolver : public ObStmtResolver
{
public:
  explicit ObXaStartResolver(ObResolverParams &params);
  virtual ~ObXaStartResolver();

  virtual int resolve(const ParseNode &parse_node);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaStartResolver);
};

} // end sql namespace
} // end oceanbase namspace

#endif /* OCEANBASE_RESOLVER_XA_START_RESOLVER_ */
