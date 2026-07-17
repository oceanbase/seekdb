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
#include "share/rc/ob_tenant_base.h"
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
  return ret;
}
int ObFTDictHub::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfoKey key(desc);
  ObFTDictInfo info;
  ObFTDictDesc versioned_desc(desc);
  container.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());

    // try if valid with no recursive lock
    if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        info.version_ = 1;
        info.type_ = desc.type_;
        info.charset_ = desc.charset_;
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        LOG_WARN("Failed to get dict info", K(ret));
      }
    } else if (info.range_count_ <= 0) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (FALSE_IT(versioned_desc.version_ = info.version_)) {
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(versioned_desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        versioned_desc.version_ = info.version_;
        if (OB_FAIL(desc.is_builtin_
                    ? ObFTRangeDict::build_cache_from_ik_dict(versioned_desc, container)
                    : ObFTRangeDict::build_cache(versioned_desc, container))) {
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
  ObFTDictInfoKey key(desc);
  ObFTDictDesc versioned_desc(desc);
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
    } else if (info.range_count_ <= 0) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (FALSE_IT(versioned_desc.version_ = info.version_)) {
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(versioned_desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
    }
  }

  return ret;
}

int ObFTDictHub::refresh_cache(const ObString &dict_table_name)
{
  int ret = OB_SUCCESS;
  if (!is_inited_ || dict_table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dictionary cache refresh request", K(ret), K(dict_table_name), K(is_inited_));
  }
  for (uint32_t type_value = static_cast<uint32_t>(ObFTDictType::DICT_IK_MAIN);
       OB_SUCC(ret) && type_value <= static_cast<uint32_t>(ObFTDictType::DICT_IK_STOP);
       ++type_value) {
    const ObFTDictDesc desc(dict_table_name,
                            static_cast<ObFTDictType>(type_value),
                            CHARSET_UTF8MB4,
                            CS_TYPE_UTF8MB4_BIN,
                            false,
                            0,
                            MTL_ID());
    const ObFTDictInfoKey key(desc);
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());
    ObFTDictInfo info;
    int tmp_ret = get_dict_info(key, info);
    if (OB_HASH_NOT_EXIST == tmp_ret) {
      // A dictionary which has never been used has no stale cache to invalidate.
    } else if (OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
      LOG_WARN("failed to find dictionary cache during refresh", K(ret), K(dict_table_name));
    } else {
      ++info.version_;
      info.range_count_ = 0;
      if (OB_FAIL(put_dict_info(key, info))) {
        LOG_WARN("failed to invalidate dictionary cache", K(ret), K(dict_table_name));
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
