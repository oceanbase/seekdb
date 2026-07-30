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

#include "ob_root_utils.h"
#include "logservice/ob_log_service.h"
#include "share/ob_max_id_cache.h"
#include "share/ob_max_id_fetcher.h"

using namespace oceanbase::rootserver;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::common::sqlclient;

///////////////////////////////

const char *oceanbase::rootserver::resource_type_to_str(const ObResourceType &t)
{
  const char* str = "UNKNOWN";
  if (RES_CPU == t) { str = "CPU"; }
  else if (RES_MEM == t) { str = "MEMORY"; }
  else if (RES_LOG_DISK == t) { str = "LOG_DISK"; }
  else if (RES_DATA_DISK == t) { str = "DATA_DISK"; }
  else { str = "NONE"; }
  return str;
}


// ===== definition moved from src/share/backup/ob_backup_connectivity.cpp / src/share/ob_max_id_cache.cpp / src/share/ob_max_id_fetcher.cpp(truly rootserver-bound: GCTX.local_management_service_ / MANAGEMENT_EVENT_ADD) =====
namespace oceanbase
{
namespace share
{




int ObMaxIdCacheMgr::fetch_max_id(const ObMaxIdType max_id_type, uint64_t &id,
    const uint64_t size, bool init_runtime_if_not_exist)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObMaxIdCache *cache = nullptr;
  bool runtime_not_inited = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("max id cache mgr is not inited", KR(ret), K(inited_));
  } else {
    ObLatchRGuard guard(latch_, ObLatchIds::MAX_ID_CACHE_LOCK);
    if (OB_ISNULL(runtime_cache_)) {
      ret = OB_HASH_NOT_EXIST;
      LOG_WARN("failed to get runtime cache", KR(ret), K(init_runtime_if_not_exist));
      runtime_not_inited = true;
    } else if (FALSE_IT(cache = runtime_cache_)) {
    } else if (OB_ISNULL(cache)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pointer is null", KR(ret), KP(cache));
    } else if (OB_FAIL(cache->fetch_max_id(max_id_type, id, size, sql_proxy_))) {
      LOG_WARN("failed to fetch max id", KR(ret), K(max_id_type), K(size));
    }
  }
  if (OB_HASH_NOT_EXIST == ret && runtime_not_inited && init_runtime_if_not_exist) {
    ret = OB_SUCCESS;
    {
      ObLatchWGuard guard(latch_, ObLatchIds::MAX_ID_CACHE_LOCK);
      if (OB_TMP_FAIL(add_runtime_cache_())) {
        // Another thread may have initialized the runtime cache.
        LOG_WARN("failed to initialize runtime cache", KR(tmp_ret));
      }
    }
    if (OB_FAIL(fetch_max_id(max_id_type, id, size, false/*init_runtime_if_not_exist*/))) {
      LOG_WARN("failed to fetch max id", KR(ret), K(max_id_type), K(size));
    }
  }
  return ret;
}

int ObMaxIdFetcher::fetch_max_id_from_cache_(ObMaxIdType id_type,
      uint64_t &max_id, const uint64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t min_id = OB_INVALID_ID;
  bool use_cache = false;
  if (OB_ISNULL(GCTX.local_management_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(GCTX.local_management_service_));
  } else if (OB_FAIL(check_use_max_id_cache_(id_type, use_cache))) {
    LOG_WARN("failed to check use max id cache", KR(ret), K(id_type));
  } else if (OB_UNLIKELY(!use_cache)) {
  } else if (OB_FAIL(GCTX.local_management_service_->get_max_id_cache_mgr().fetch_max_id(id_type,
          min_id, size))) {
    LOG_WARN("failed to fetch max id", KR(ret), K(id_type), K(size));
  } else if (FALSE_IT(max_id = min_id + size - 1)) {
  } else if (max_id < min_id) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("id out of range", KR(ret), K(min_id), K(size), K(max_id));
  }
  if (FAILEDx(check_id_valid(id_type, max_id))) {
    LOG_WARN("invalid max id", KR(ret), K(id_type), K(max_id));
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
