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
#include "storage/fts/dict/ob_ft_cache.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_range_dict.h"
namespace oceanbase
{
namespace storage
{
int ObFTDictHub::init()
{
  static constexpr int K_MAX_DICT_BUCKET = 128; // for now, only built-in dicts.
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
  if (OB_FAIL(dict_map_.destroy())) {
    LOG_WARN("destroy dict map failed", K(ret));
  }
  rw_dict_lock_.destroy();
  return ret;
}
int ObFTDictHub::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfoKey key(desc.get_cache_name(), desc.type_);
  ObFTDictInfo info;
  container.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());

    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("lock dict cache failed", K(ret), K(key.hash()));
    } else if (OB_FAIL(get_dict_info(key, info))) {
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
  ObFTDictInfoKey key(desc.get_cache_name(), desc.type_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    ObBucketHashRLockGuard guard(rw_dict_lock_, key.hash());
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("lock dict cache failed", K(ret), K(key.hash()));
    } else if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        LOG_WARN("Failed to get dict info", K(ret));
      }
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

int ObFTDictHub::build_custom_cache(const ObFTDictDesc &desc,
                                    ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  container.reset();
  if (OB_FAIL(ObFTRangeDict::build_cache_from_custom_table(desc, container))) {
    LOG_WARN("Failed to build custom dict cache", K(ret), K(desc));
  }
  return ret;
}

int ObFTDictHub::erase_cache_ranges(const ObFTDictDesc &desc,
                                    const int32_t range_count)
{
  int ret = OB_SUCCESS;
  for (int32_t range_id = 0; range_id < range_count; ++range_id) {
    const ObDictCacheKey cache_key(desc.get_cache_name(), desc.type_, range_id);
    const int tmp_ret = ObDictCache::get_instance().erase(cache_key);
    if (OB_SUCCESS != tmp_ret && OB_ENTRY_NOT_EXIST != tmp_ret) {
      if (OB_SUCCESS == ret) {
        ret = tmp_ret;
      }
      LOG_WARN("Failed to erase dict cache range", K(tmp_ret), K(desc), K(range_id));
    }
  }
  return ret;
}

int ObFTDictHub::load_or_build_custom_cache(const ObFTDictDesc &desc,
                                            ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  bool need_build = false;
  bool info_exists = false;
  ObFTDictInfo info;
  ObFTDictInfoKey key(desc.get_cache_name(), desc.type_);
  container.reset();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else if (OB_UNLIKELY(desc.table_name_.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("custom dict table name is empty", K(ret), K(desc));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("lock custom dict cache failed", K(ret), K(key.hash()));
    } else if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        need_build = true;
      } else {
        LOG_WARN("Failed to get custom dict info", K(ret), K(desc));
      }
    } else {
      info_exists = true;
      if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          need_build = true;
        } else {
          LOG_WARN("Failed to load custom dict cache", K(ret), K(desc));
        }
      }
    }

    if (OB_SUCC(ret) && need_build) {
      if (info_exists) {
        const int tmp_ret = dict_map_.erase_refactored(key);
        if (OB_SUCCESS != tmp_ret && OB_HASH_NOT_EXIST != tmp_ret) {
          ret = tmp_ret;
          LOG_WARN("Failed to erase stale custom dict info", K(ret), K(desc));
        } else if (OB_FAIL(erase_cache_ranges(desc, info.range_count_))) {
          LOG_WARN("Failed to erase stale custom dict ranges", K(ret), K(desc));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(build_custom_cache(desc, container))) {
        LOG_WARN("Failed to lazily build custom dict cache", K(ret), K(desc));
      }
      if (OB_SUCC(ret)) {
        const int64_t range_count = container.get_handles().size();
        if (OB_UNLIKELY(range_count > INT32_MAX)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("custom dict range count overflow", K(ret), K(range_count), K(desc));
        } else {
          ObFTDictInfo new_info;
          new_info.range_count_ = static_cast<int32_t>(range_count);
          if (OB_FAIL(put_dict_info(key, new_info))) {
            LOG_WARN("Failed to publish custom dict info", K(ret), K(desc), K(range_count));
          }
        }
      }
      if (OB_FAIL(ret)) {
        const int64_t handle_count = container.get_handles().size();
        const int32_t partial_count =
            static_cast<int32_t>(handle_count > INT32_MAX ? INT32_MAX : handle_count);
        const int cleanup_ret = erase_cache_ranges(desc, partial_count);
        if (OB_SUCCESS != cleanup_ret) {
          LOG_WARN("Failed to clean partial custom dict cache", K(cleanup_ret), K(desc));
        }
        container.reset();
      }
    }
  }
  return ret;
}

int ObFTDictHub::refresh_custom_cache(const ObFTDictDesc &desc,
                                      ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  bool info_exists = false;
  ObFTDictInfo info;
  ObFTDictInfoKey key(desc.get_cache_name(), desc.type_);
  container.reset();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else if (OB_UNLIKELY(desc.table_name_.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("custom dict table name is empty", K(ret), K(desc));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("lock custom dict cache failed", K(ret), K(key.hash()));
    } else if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("Failed to get custom dict info", K(ret), K(desc));
      }
    } else {
      info_exists = true;
    }

    if (OB_SUCC(ret) && info_exists) {
      const int tmp_ret = dict_map_.erase_refactored(key);
      if (OB_SUCCESS != tmp_ret && OB_HASH_NOT_EXIST != tmp_ret) {
        ret = tmp_ret;
        LOG_WARN("Failed to erase custom dict info", K(ret), K(desc));
      } else if (OB_FAIL(erase_cache_ranges(desc, info.range_count_))) {
        LOG_WARN("Failed to erase stale custom dict ranges", K(ret), K(desc));
      }
    }

    if (OB_SUCC(ret) && OB_FAIL(build_custom_cache(desc, container))) {
      LOG_WARN("Failed to refresh custom dict cache", K(ret), K(desc));
    }
    if (OB_SUCC(ret)) {
      const int64_t range_count = container.get_handles().size();
      if (OB_UNLIKELY(range_count > INT32_MAX)) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("custom dict range count overflow", K(ret), K(range_count), K(desc));
      } else {
        ObFTDictInfo new_info;
        new_info.range_count_ = static_cast<int32_t>(range_count);
        if (OB_FAIL(put_dict_info(key, new_info))) {
          LOG_WARN("Failed to publish refreshed custom dict info", K(ret), K(desc), K(range_count));
        }
      }
    }
    if (OB_FAIL(ret)) {
      const int64_t handle_count = container.get_handles().size();
      const int32_t partial_count =
          static_cast<int32_t>(handle_count > INT32_MAX ? INT32_MAX : handle_count);
      const int cleanup_ret = erase_cache_ranges(desc, partial_count);
      if (OB_SUCCESS != cleanup_ret) {
        LOG_WARN("Failed to clean partial refreshed custom dict cache", K(cleanup_ret), K(desc));
      }
      container.reset();
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
