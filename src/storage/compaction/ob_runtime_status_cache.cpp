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
#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "storage/compaction/ob_runtime_status_cache.h"
#include "share/rc/ob_module_provider.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace share;
namespace compaction
{
bool ObRuntimeStatusCache::should_skip_merge() const
{
  bool bret = true;
  if (IS_INIT) {
    const share::ObServerRole::Role role = share::server_role();
    bret = during_restore_ && is_standby_role(role);
  }
  return bret;
}

int ObRuntimeStatusCache::during_restore(bool &during_restore) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    if (REACH_THREAD_TIME_INTERVAL(30_s)) {
      LOG_INFO("runtime status has not been initialized", KR(ret), KPC(this));
    }
  } else {
    during_restore = during_restore_;
  }
  return ret;
}

int ObRuntimeStatusCache::init_or_refresh()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    is_inited_ = true;
  }
  IGNORE_RETURN inner_refresh_restore_status();
  return ret;
}

int ObRuntimeStatusCache::refresh_runtime_config(const bool enable_adaptive_compaction)
{
  ATOMIC_SET(&enable_adaptive_compaction_, enable_adaptive_compaction);
  return init_or_refresh();
}

int ObRuntimeStatusCache::inner_refresh_restore_status()
{
  int ret = OB_SUCCESS;
  if (REACH_THREAD_TIME_INTERVAL(REFRESH_SERVER_RUNTIME_STATUS_INTERVAL)) {
    ObSchemaGetterGuard schema_guard;
    const ObSimpleServerRuntimeSchema *runtime_schema = nullptr;

    if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
      LOG_WARN("fail to get schema guard", K(ret));
    } else if (OB_FAIL(schema_guard.get_server_runtime_info(runtime_schema))) {
      LOG_WARN("fail to get runtime schema", K(ret));
    } else if (OB_ISNULL(runtime_schema)) {
      ret = OB_SCHEMA_ERROR;
      LOG_WARN("runtime schema is null", K(ret));
    } else if (runtime_schema->is_restore()) {
      ATOMIC_SET(&during_restore_, true);
    } else {
      ATOMIC_SET(&during_restore_, false);
    }
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
