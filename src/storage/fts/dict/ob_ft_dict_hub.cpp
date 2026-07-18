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
  dict_map_.destroy();
  rw_dict_lock_.destroy();
  is_inited_ = false;
  return ret;
}
int ObFTDictHub::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  return build_cache_internal(desc, container, false);
}

int ObFTDictHub::refresh(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  return build_cache_internal(desc, container, true);
}

int ObFTDictHub::build_cache_internal(const ObFTDictDesc &desc,
                                      ObFTCacheRangeContainer &container,
                                      const bool force_refresh)
{
  int ret = OB_SUCCESS;
  ObFTDictInfoKey key(desc.tenant_id_, desc.table_id_);
  ObFTDictInfo info;
  container.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());

    int info_ret = get_dict_info(key, info);
    if (OB_HASH_NOT_EXIST == info_ret) {
      info_ret = OB_SUCCESS;
    }
    if (OB_SUCCESS != info_ret) {
      ret = info_ret;
      LOG_WARN("Failed to get dict info", K(ret), K(desc));
    } else if (!force_refresh && info.generation_ > 0) {
      ObFTDictDesc load_desc(desc);
      load_desc.generation_ = info.generation_;
      if (OB_FAIL(ObFTRangeDict::try_load_cache(
                      load_desc, info.range_count_, container))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("Failed to load current dictionary cache", K(ret), K(load_desc));
        }
      } else {
        return OB_SUCCESS;
      }
    }

    if (OB_SUCC(ret)) {
      ObFTDictDesc build_desc(desc);
      build_desc.generation_ = info.generation_ + 1;
      container.reset();
      if (build_desc.is_builtin_) {
        if (OB_FAIL(ObFTRangeDict::build_cache_from_ik_dict(build_desc, container))) {
          LOG_WARN("Failed to build compiled dictionary cache", K(ret), K(build_desc));
        }
      } else if (OB_FAIL(ObFTRangeDict::build_cache(build_desc, container))) {
        LOG_WARN("Failed to build custom dictionary cache", K(ret), K(build_desc));
      }
      if (OB_SUCC(ret)) {
        info.generation_ = build_desc.generation_;
        info.range_count_ = static_cast<int32_t>(container.get_handles().size());
        if (OB_FAIL(put_dict_info(key, info))) {
          LOG_WARN("Failed to publish dictionary cache generation", K(ret), K(build_desc));
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
  ObFTDictInfoKey key(desc.tenant_id_, desc.table_id_);
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
    } else {
      ObFTDictDesc load_desc(desc);
      load_desc.generation_ = info.generation_;
      if (OB_FAIL(ObFTRangeDict::try_load_cache(
                      load_desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
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
