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
  } else if (OB_FAIL(rw_dict_lock_.init(K_MAX_DICT_BUCKET))) {
  } else {
    is_inited_ = true;
  }
  return ret;
};

int ObFTDictHub::destroy()
{
  int ret = OB_SUCCESS;
  is_inited_ = false;
  for (int64_t i = 0; i < MAX_SHARED_DICT_CNT; ++i) {
    if (nullptr != shared_dicts_[i].dict_) {
      shared_dicts_[i].dict_->~ObFTRangeDict();
      shared_dicts_[i].dict_ = nullptr;
    }
    if (nullptr != shared_dicts_[i].container_) {
      shared_dicts_[i].container_->~ObFTCacheRangeContainer();
      shared_dicts_[i].container_ = nullptr;
    }
  }
  shared_dict_alloc_.reset();
  return ret;
}

int ObFTDictHub::get_shared_dict(const ObFTDictDesc &desc, ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;
  const int64_t idx = static_cast<int64_t>(desc.type_) - static_cast<int64_t>(ObFTDictType::DICT_IK_MAIN);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (idx < 0 || idx >= MAX_SHARED_DICT_CNT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fts dict type for shared dict", K(ret), K(desc.type_));
  } else {
    lib::ObMutexGuard guard(shared_dict_lock_);
    ObFTSharedDict &shared_dict = shared_dicts_[idx];
    if (nullptr == shared_dict.dict_) {
      if (OB_FAIL(build_shared_dict(desc, shared_dict))) {
        LOG_WARN("failed to build shared fts dict", K(ret), K(desc));
      }
    }
    if (OB_SUCC(ret)) {
      dict = shared_dict.dict_;
    }
  }
  return ret;
}

int ObFTDictHub::build_shared_dict(const ObFTDictDesc &desc, ObFTSharedDict &shared_dict)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObFTCacheRangeContainer *container = nullptr;
  ObFTRangeDict *range_dict = nullptr;
  if (OB_UNLIKELY(nullptr != shared_dict.dict_)) {
    // already built by others
  } else if (OB_ISNULL(buf = shared_dict_alloc_.alloc(sizeof(ObFTCacheRangeContainer)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate fts cache range container", K(ret));
  } else if (OB_ISNULL(container = new (buf) ObFTCacheRangeContainer(shared_dict_alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to construct fts cache range container", K(ret));
  } else if (OB_FAIL(load_cache(desc, *container))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      if (OB_FAIL(build_cache(desc, *container))) {
        LOG_WARN("failed to build fts dict cache", K(ret), K(desc));
      }
    } else {
      LOG_WARN("failed to load fts dict cache", K(ret), K(desc));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(range_dict = OB_NEWx(ObFTRangeDict, &shared_dict_alloc_,
                                            shared_dict_alloc_, container, desc))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate fts range dict", K(ret), K(desc));
  } else if (OB_FAIL(range_dict->init())) {
    LOG_WARN("failed to init fts range dict", K(ret), K(desc));
  } else {
    shared_dict.container_ = container;
    shared_dict.dict_ = range_dict;
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
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());

    // try if valid with no recursive lock
    if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
        ret = OB_ENTRY_NOT_EXIST;
      } else {
      }
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
      } else {
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        if (OB_FAIL(ObFTRangeDict::build_cache_from_ik_dict(desc, container))) {
        } else if (FALSE_IT(info.range_count_ = container.get_handles().size())) {
        } else if (OB_FAIL(put_dict_info(key, info))) {
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
  } else {
    {
      ObBucketHashRLockGuard guard(rw_dict_lock_, key.hash());
      if (OB_FAIL(get_dict_info(key, info))) {
        if (OB_HASH_NOT_EXIST == ret) {
          // dict not exist, make new one, by caller
          ret = OB_ENTRY_NOT_EXIST;
        } else {
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
      } else {
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
    }
  }

  return ret;
}

int ObFTDictHub::put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info)
{
  int ret = OB_SUCCESS;
  const int cover_exist_flag = 1;
  if (OB_FAIL(dict_map_.set_refactored(key, info, cover_exist_flag))) {
  }

  return ret;
}
} //  namespace storage
} //  namespace oceanbase
