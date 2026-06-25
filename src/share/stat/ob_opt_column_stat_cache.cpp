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

#include "share/stat/ob_opt_column_stat_cache.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

namespace oceanbase
{
namespace common
{

int ObOptColumnStatCache::get_row(const ObOptColumnStat::Key &key, ObOptColumnStatHandle &handle)
{
  int ret = OB_SUCCESS;

  if (!key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid column stat cache key.", K(key), K(ret));
  } else if (OB_FAIL(get(key, handle.stat_, handle.handle_))){
    if (OB_ENTRY_NOT_EXIST != ret) {
      COMMON_LOG(WARN, "Fail to get key from row cache. ", K(key), K(ret));
    }
    EVENT_INC(ObStatEventIds::OPT_COLUMN_STAT_CACHE_MISS);
  } else {
    handle.cache_ = this;
    EVENT_INC(ObStatEventIds::OPT_COLUMN_STAT_CACHE_HIT);
  }
  return ret;
}


int ObOptColumnStatCache::put_and_fetch_row(const ObOptColumnStat::Key &key,
                                            const ObOptColumnStat &value,
                                            ObOptColumnStatHandle &handle)
{
  int ret = OB_SUCCESS;
  if (!key.is_valid()) {
     ret = OB_INVALID_ARGUMENT;
     COMMON_LOG(WARN, "invalid column stat cache key.", K(key), K(ret));
  } else if (OB_FAIL(put_and_fetch(key, value, handle.stat_, handle.handle_, true /*overwrite*/))) {
     COMMON_LOG(WARN, "Fail to put kvpair to cache.", K(ret));
  }
  return ret;
}

} // end of namespace common
} // end of namespace oceanbase
