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

#include "storage/fts/dict/ob_ft_dict_hub.h"

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_cache_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_range_dict.h"
namespace oceanbase
{
namespace storage
{
int ObFTDictHub::init()
{
  static constexpr int K_MAX_DICT_BUCKET = 128;
  int ret = OB_SUCCESS;
  if (OB_FAIL(dict_map_.create(K_MAX_DICT_BUCKET, "dict_map"))) {
    LOG_WARN("init dict map failed", K(ret));
  } else if (OB_FAIL(rw_dict_lock_.init(K_MAX_DICT_BUCKET))) {
    LOG_WARN("init dict lock failed", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
};

int ObFTDictHub::destroy()
{
  int ret = OB_SUCCESS;
  is_inited_ = false;
  for (int i = 0; i < 4; ++i) {
    if (cached_containers_[i] != nullptr) {
      cached_containers_[i]->~ObFTCacheRangeContainer();
      hub_alloc_.free(cached_containers_[i]);
      cached_containers_[i] = nullptr;
    }
    if (cached_dicts_[i] != nullptr) {
      static_cast<ObFTRangeDict *>(cached_dicts_[i])->~ObFTRangeDict();
      hub_alloc_.free(cached_dicts_[i]);
      cached_dicts_[i] = nullptr;
    }
  }
  hub_alloc_.reset();
  return ret;
}

int ObFTDictHub::get_cached_builtin_dict(const ObFTDictDesc &desc, ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;
  uint64_t idx = static_cast<uint64_t>(desc.type_);
  if (OB_UNLIKELY(idx < 1 || idx > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dict type for cached builtin dict", K(ret), K(idx));
  } else {
    ObSpinLockGuard guard(cached_dict_lock_);
    if (cached_dicts_[idx] != nullptr) {
      dict = cached_dicts_[idx];
    } else {
      ObFTCacheRangeContainer *container = nullptr;
      if (OB_ISNULL(container = OB_NEWx(ObFTCacheRangeContainer, &hub_alloc_, hub_alloc_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc cached container", K(ret));
      } else if (OB_FAIL(load_cache(desc, *container))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          if (OB_FAIL(build_cache(desc, *container))) {
            LOG_WARN("Failed to build cache for caching", K(ret));
          }
        } else {
          LOG_WARN("Failed to load cache for caching", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        ObFTRangeDict *range_dict = nullptr;
        if (OB_ISNULL(range_dict = OB_NEWx(ObFTRangeDict, &hub_alloc_, hub_alloc_, container, desc))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc cached range dict", K(ret));
        } else if (OB_FAIL(range_dict->init())) {
          LOG_WARN("fail to init cached range dict", K(ret));
        } else {
          cached_dicts_[idx] = range_dict;
          cached_containers_[idx] = container;
          dict = range_dict;
          container = nullptr;
          LOG_INFO("succeed to cache builtin dict", K(idx));
        }
        if (OB_FAIL(ret) && range_dict != nullptr) {
          OB_DELETEx(ObFTRangeDict, &hub_alloc_, range_dict);
        }
      }
      if (OB_FAIL(ret) && container != nullptr) {
        OB_DELETEx(ObFTCacheRangeContainer, &hub_alloc_, container);
      }
    }
  }
  return ret;
}

int ObFTDictHub::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfoKey key(static_cast<uint64_t>(desc.type_));
  ObFTDictInfo info;
  container.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());

    // try if valid with no recursive lock
    if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        LOG_WARN("Failed to get dict info", K(ret));
      }
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        if (OB_FAIL(ObFTRangeDict::build_cache_from_ik_dict(desc, container))) {
          LOG_WARN("Failed to build cache", K(ret));
        } else if (FALSE_IT(info.range_count_ = container.get_handles().size())) {
        } else if (OB_FAIL(put_dict_info(key, info))) {
          LOG_WARN("Failed to put dict info", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObFTDictHub::load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfo info;
  container.reset();
  ObFTDictInfoKey key(static_cast<uint64_t>(desc.type_));
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    {
      ObBucketHashRLockGuard guard(rw_dict_lock_, key.hash());
      if (OB_FAIL(get_dict_info(key, info))) {
        if (OB_HASH_NOT_EXIST == ret) {
          // dict not exist, make new one, by caller
          ret = OB_ENTRY_NOT_EXIST;
        } else {
          LOG_WARN("Failed to get dict info", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
    }
  }

  return ret;
}


int ObFTDictHub::get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(dict_map_.get_refactored(key, info))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("get dict info failed", K(ret));
    }
  }

  return ret;
}

int ObFTDictHub::put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info)
{
  int ret = OB_SUCCESS;
  const int cover_exist_flag = 1;
  if (OB_FAIL(dict_map_.set_refactored(key, info, cover_exist_flag))) {
    LOG_WARN("put dict info failed", K(ret));
  }

  return ret;
}
} //  namespace storage
} //  namespace oceanbase
