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
  lib::ObMemAttr mem_attr;
  mem_attr.label_ = "FtParse";
  if (OB_FAIL(dict_map_.create(K_MAX_DICT_BUCKET, "dict_map"))) {
  } else if (OB_FAIL(rw_dict_lock_.init(K_MAX_DICT_BUCKET))) {
  } else if (OB_FAIL(dict_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                         common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                                         mem_attr))) {
  } else if (OB_FAIL(dict_cache_map_.create(K_MAX_DICT_BUCKET, "dict_cache_map"))) {
  } else if (OB_FAIL(rw_dict_cache_lock_.init(K_MAX_DICT_BUCKET))) {
  } else {
    is_inited_ = true;
  }
  return ret;
};

int ObFTDictHub::destroy()
{
  int ret = OB_SUCCESS;
  // The cached dictionaries are shared by all the parses, so they are
  // released only when the hub itself is destroyed.
  for (auto iter = dict_cache_map_.begin(); iter != dict_cache_map_.end(); ++iter) {
    ObFTCachedDictEntry *entry = iter->second;
    if (OB_NOT_NULL(entry)) {
      if (OB_NOT_NULL(entry->dict_)) {
        entry->dict_->~ObIFTDict();
        dict_allocator_.free(entry->dict_);
        entry->dict_ = nullptr;
      }
      entry->~ObFTCachedDictEntry();
      dict_allocator_.free(entry);
    }
  }
  if (dict_cache_map_.created()) {
    dict_cache_map_.destroy();
  }
  if (dict_allocator_.is_inited()) {
    dict_allocator_.reset();
  }
  is_inited_ = false;
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


int ObFTDictHub::get_or_build_dict(const ObFTDictDesc &desc, ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;
  ObFTDictInfoKey key(static_cast<uint64_t>(desc.type_));
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    {
      ObBucketHashRLockGuard guard(rw_dict_cache_lock_, key.hash());
      ObFTCachedDictEntry **cached = dict_cache_map_.get(key);
      if (OB_NOT_NULL(cached) && OB_NOT_NULL(*cached)) {
        dict = (*cached)->dict_;
      }
    }
    if (OB_ISNULL(dict)) {
      ObBucketHashWLockGuard guard(rw_dict_cache_lock_, key.hash());
      ObFTCachedDictEntry **cached = dict_cache_map_.get(key);
      if (OB_NOT_NULL(cached) && OB_NOT_NULL(*cached)) {
        dict = (*cached)->dict_;
      } else {
        ObFTCachedDictEntry *entry = OB_NEWx(ObFTCachedDictEntry, &dict_allocator_, dict_allocator_);
        ObIFTDict *new_dict = nullptr;
        if (OB_ISNULL(entry)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(build_cache(desc, entry->container_))) {
          entry->~ObFTCachedDictEntry();
          dict_allocator_.free(entry);
        } else if (OB_ISNULL(new_dict =
                             OB_NEWx(ObFTRangeDict,
                                     &dict_allocator_,
                                     dict_allocator_,
                                     &entry->container_,
                                     desc))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          entry->~ObFTCachedDictEntry();
          dict_allocator_.free(entry);
        } else if (OB_FAIL(new_dict->init())) {
          new_dict->~ObIFTDict();
          dict_allocator_.free(new_dict);
          entry->~ObFTCachedDictEntry();
          dict_allocator_.free(entry);
        } else if (OB_FAIL(dict_cache_map_.set_refactored(key, entry, 0))) {
          new_dict->~ObIFTDict();
          dict_allocator_.free(new_dict);
          entry->~ObFTCachedDictEntry();
          dict_allocator_.free(entry);
        } else {
          entry->dict_ = new_dict;
          dict = new_dict;
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
