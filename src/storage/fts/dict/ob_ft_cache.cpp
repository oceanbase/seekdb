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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/dict/ob_ft_cache.h"

#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{
int ObDictCache::get_dict(const ObDictCacheKey &key,
                          const ObDictCacheValue *&value,
                          common::ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (OB_FAIL(get(key, value, handle))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("get dict from cache failed", K(ret));
    }
  }
  return ret;
}


int ObDictCache::put_and_fetch_dict(const ObDictCacheKey &key,
                                    const ObDictCacheValue &value,
                                    const ObDictCacheValue *&pvalue,
                                    common::ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (OB_FAIL(put_and_fetch(key, value, pvalue, handle))) {
    LOG_WARN("put dict to cache failed", K(ret));
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
