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

#ifndef _OB_OPT_SYSTEM_STAT_CACHE_H_
#define _OB_OPT_SYSTEM_STAT_CACHE_H_

#include "share/cache/ob_kv_storecache.h"
#include "share/stat/ob_opt_system_stat.h"

namespace oceanbase {
namespace common {

struct ObOptSystemStatHandle;

class ObOptSystemStatCache : public common::ObKVCache<ObOptSystemStat::Key, ObOptSystemStat>
{
public:
  int get_value(const ObOptSystemStat::Key &key, ObOptSystemStatHandle &handle);
  int put_and_fetch_value(const ObOptSystemStat::Key &key,
                          const ObOptSystemStat &value,
                          ObOptSystemStatHandle &handle);
};

struct ObOptSystemStatHandle
{
  const ObOptSystemStat *stat_;
  ObOptSystemStatCache *cache_;
  ObKVCacheHandle handle_;

  ObOptSystemStatHandle()
    : stat_(nullptr), cache_(nullptr), handle_() {}
  ~ObOptSystemStatHandle()
  {
    stat_ = nullptr;
    cache_ = nullptr;
  }
  void reset()
  {
    stat_ = nullptr;
    cache_ = nullptr;
    handle_.reset();
  }
  TO_STRING_KV(K(stat_));
};

}
}



#endif /* _OB_OPT_SYSTEM_STAT_CACHE_H_ */
