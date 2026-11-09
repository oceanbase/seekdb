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
  for (int64_t i = 0; i < MAX_DICT_TYPE_CNT; ++i) {
    ObFTSharedDict &shared = shared_dicts_[i];
    if (OB_NOT_NULL(shared.dict_)) {
      // the dict reads the pinned kv cache blocks, release it before the handles
      shared.dict_->~ObIFTDict();
      shared.dict_ = nullptr;
    }
    if (OB_NOT_NULL(shared.container_)) {
      shared.container_->~ObFTCacheRangeContainer();
      shared.container_ = nullptr;
    }
  }
  is_inited_ = false;
  return ret;
}
int ObFTDictHub::get_dict(const ObFTDictDesc &desc, const ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;
  const int64_t type_idx = static_cast<int64_t>(desc.type_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (type_idx <= static_cast<int64_t>(ObFTDictType::DICT_TYPE_INVALID) || type_idx >= MAX_DICT_TYPE_CNT) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObFTDictInfoKey key(static_cast<uint64_t>(desc.type_));
    ObFTDictInfo info;
    ObFTSharedDict &shared = shared_dicts_[type_idx];
    {
      // the dict is shared and read-only, the common case only needs a read lock
      ObBucketHashRLockGuard guard(rw_dict_lock_, key.hash());
      if (OB_NOT_NULL(shared.dict_)) {
        dict = shared.dict_;
        return OB_SUCCESS;
      }
    }
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());
    if (OB_ISNULL(shared.dict_)) {
      ObFTCacheRangeContainer *container = OB_NEWx(ObFTCacheRangeContainer, &dict_alloc_, dict_alloc_);
      if (OB_ISNULL(container)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        // try the kv cache first, build the dictionary on the first miss
        if (OB_FAIL(get_dict_info(key, info))) {
          if (OB_HASH_NOT_EXIST == ret) {
            ret = OB_ENTRY_NOT_EXIST;
          }
        } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, *container))) {
          if (OB_ENTRY_NOT_EXIST == ret) {
            ret = OB_ENTRY_NOT_EXIST;
          }
        }
        if (OB_ENTRY_NOT_EXIST == ret) {
          container->reset();
          if (OB_FAIL(ObFTRangeDict::build_cache_from_ik_dict(desc, *container))) {
          } else if (FALSE_IT(info.range_count_ = container->get_handles().size())) {
          } else if (OB_FAIL(put_dict_info(key, info))) {
          }
        }
        if (OB_SUCC(ret)) {
          ObIFTDict *new_dict = OB_NEWx(ObFTRangeDict, &dict_alloc_, dict_alloc_, container, desc);
          if (OB_ISNULL(new_dict)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(new_dict->init())) {
            OB_DELETEx(ObIFTDict, &dict_alloc_, new_dict);
          } else {
            shared.container_ = container;
            shared.dict_ = new_dict;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      dict = shared.dict_;
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
