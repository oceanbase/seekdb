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
  } else if (OB_FAIL(refresh_version_map_.create(K_MAX_DICT_BUCKET, "dict_refresh"))) {
    LOG_WARN("init dictionary refresh version map failed", K(ret));
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
  refresh_version_map_.destroy();
  is_inited_ = false;
  return ret;
}

int ObFTDictHub::get_refresh_version(const uint64_t dict_table_id, int64_t &version)
{
  int ret = OB_SUCCESS;
  version = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(OB_INVALID_ID == dict_table_id)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBucketHashRLockGuard guard(rw_dict_lock_, common::murmurhash(&dict_table_id, sizeof(dict_table_id), 0));
    ret = refresh_version_map_.get_refactored(dict_table_id, version);
    if (OB_HASH_NOT_EXIST == ret) {
      // 首次使用的词典尚未 refresh，按初始代次读取。
      ret = OB_SUCCESS;
      version = 0;
    }
  }
  return ret;
}

int ObFTDictHub::advance_refresh_version(const uint64_t dict_table_id, int64_t &version)
{
  int ret = OB_SUCCESS;
  int64_t old_version = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(OB_INVALID_ID == dict_table_id)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, common::murmurhash(&dict_table_id, sizeof(dict_table_id), 0));
    int tmp_ret = refresh_version_map_.get_refactored(dict_table_id, old_version);
    if (OB_HASH_NOT_EXIST == tmp_ret) {
      old_version = 0;
    } else if (OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
      LOG_WARN("failed to get dictionary refresh version", K(ret), K(dict_table_id));
    }
    if (OB_SUCC(ret)) {
      version = old_version + 1;
      if (OB_FAIL(refresh_version_map_.set_refactored(dict_table_id, version, 1 /* overwrite */))) {
        LOG_WARN("failed to advance dictionary refresh version", K(ret), K(dict_table_id), K(version));
      }
    }
  }
  return ret;
}
int ObFTDictHub::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfoKey key(desc.get_cache_identity());
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
        // 内置词典从程序内嵌文本构建；用户词典必须扫描其配置表，不能退化为内置词典。
        if (OB_FAIL(desc.is_builtin()
                        ? ObFTRangeDict::build_cache_from_ik_dict(desc, container)
                        : ObFTRangeDict::build_cache_from_table(desc, container))) {
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
  ObFTDictInfoKey key(desc.get_cache_identity());
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
