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

#ifndef _OB_CREATE_FUNC_RESOLVER_H
#define _OB_CREATE_FUNC_RESOLVER_H 1

#include "sql/resolver/ddl/ob_create_func_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "lib/container/ob_se_array.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace sql
{

class ObCreateFuncResolver : public ObDDLResolver
{
public:
  explicit ObCreateFuncResolver(ObResolverParams &params);
  virtual ~ObCreateFuncResolver();

  virtual int resolve(const ParseNode &parse_tree);
};

}
}

#endif /* _OB_CREATE_FUNC_RESOLVER_H */


