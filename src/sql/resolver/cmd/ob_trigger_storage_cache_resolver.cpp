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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/cmd/ob_trigger_storage_cache_stmt.h"
#include "sql/resolver/cmd/ob_trigger_storage_cache_resolver.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

ObTriggerStorageCacheResolver::ObTriggerStorageCacheResolver(ObResolverParams &params)
  : ObCMDResolver(params)
{
}

ObTriggerStorageCacheResolver::~ObTriggerStorageCacheResolver()
{
}
int ObTriggerStorageCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObTriggerStorageCacheStmt *stmt = nullptr;
  uint64_t compat_version = 0;
  ret = OB_NOT_SUPPORTED;
  LOG_ERROR("shared nothing do not support trigger storage cache", KR(ret));
  return ret;
}

} // namespace sql
} // namespace oceanbase
