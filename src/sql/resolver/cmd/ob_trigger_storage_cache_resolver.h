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

#ifndef SQL_RESOLVER_CMD_OB_TRIGGER_STORAGE_CACHE_RESOLVER_H
#define SQL_RESOLVER_CMD_OB_TRIGGER_STORAGE_CACHE_RESOLVER_H

#include "sql/resolver/cmd/ob_cmd_resolver.h"

namespace oceanbase
{
namespace sql
{
  
class ObTriggerStorageCacheResolver : public ObCMDResolver
{
public:
  explicit ObTriggerStorageCacheResolver(ObResolverParams &params);
  virtual ~ObTriggerStorageCacheResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTriggerStorageCacheResolver);
};
} // end namespace sql
} // end namespace oceanbase

#endif /*_OB_TENANT_SNAPSHOT_RESOLVER_H*/
