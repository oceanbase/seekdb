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

#define USING_LOG_PREFIX RS

#include "rootserver/ob_max_id_cache_adapter.h"

#include "rootserver/ob_local_management_service.h"

namespace oceanbase
{
namespace rootserver
{

int ObMaxIdCacheAdapter::fetch_max_id(share::ObMaxIdType id_type,
    uint64_t &min_id, uint64_t size)
{
  int ret = OB_SUCCESS;
  if (!management_service_.is_ddl_allowed()) {
    ret = OB_RS_SHUTDOWN;
    LOG_WARN("root service is not ready to allocate cached ids", KR(ret));
  } else if (OB_FAIL(management_service_.get_max_id_cache_mgr().fetch_max_id(
      id_type, min_id, size))) {
    LOG_WARN("failed to fetch cached max id", KR(ret), K(id_type), K(size));
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
