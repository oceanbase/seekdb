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

#ifndef OCEANBASE_DROP_SEQUENCE_RESOLVER_
#define OCEANBASE_DROP_SEQUENCE_RESOLVER_ 1

#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObDropSequenceResolver: public ObStmtResolver
{
public:
  explicit ObDropSequenceResolver(ObResolverParams &params);
  virtual ~ObDropSequenceResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObDropSequenceResolver);
  // function members

private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_DROP_SEQUENCE_RESOLVER_ */
