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

#ifndef OCEANBASE_SQL_OB_CREATE_LOCATION_RESOLVER_H_
#define OCEANBASE_SQL_OB_CREATE_LOCATION_RESOLVER_H_

#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObCreateLocationResolver: public ObDDLResolver
{
  static const int64_t LOCATION_REPLACE = 0;
  static const int64_t LOCATION_NAME = 1;
  static const int64_t LOCATION_URL = 2;
  static const int64_t LOCATION_CREDENTIAL = 3;
  static const int64_t LOCATION_NODE_COUNT = 4;

public:
  explicit ObCreateLocationResolver(ObResolverParams &params);
  virtual ~ObCreateLocationResolver();
  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateLocationResolver);
};
} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_SQL_OB_CREATE_LOCATION_RESOLVER_H_
