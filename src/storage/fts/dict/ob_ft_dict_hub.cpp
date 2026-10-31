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

#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
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
/**
 * A dictionary that is built once and then shared by every parser.
 *
 * It owns the KV cache handles that keep the double array tries alive, so it
 * has to outlive every parser that uses it.
 */
class ObFTDictHub::ObFTSharedDict
{
public:
  ObFTSharedDict() : alloc_(), container_(nullptr), dict_(nullptr) {}
  ~ObFTSharedDict() { reset(); }

  int init(const ObFTDictDesc &desc, ObFTDictHub *hub);

  void reset();

  ObIFTDict *get_dict() const { return dict_; }

private:
  ObArenaAllocator alloc_;
  ObFTCacheRangeContainer *container_;
  ObIFTDict *dict_;
};

int ObFTDictHub::init()
{
  static constexpr int K_MAX_DICT_BUCKET = 128; // for now, only built-in dicts.
  int ret = OB_SUCCESS;
  if (OB_FAIL(dict_map_.create(K_MAX_DICT_BUCKET, "dict_map"))) {
  } else if (OB_FAIL(rw_dict_lock_.init(K_MAX_DICT_BUCKET))) {
  } else if (OB_FAIL(shared_dict_lock_.init(K_MAX_DICT_BUCKET))) {
  } else {
    is_inited_ = true;
  }
  return ret;
};

int ObFTDictHub::destroy()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ObBucketWLockGuard guard(shared_dict_lock_, 0);
    if (OB_UNLIKELY(guard.get_ret() != OB_SUCCESS)) {
      ret = guard.get_ret();
    } else {
      for (int64_t i = 0; i < MAX_DICT_TYPE_NUM; ++i) {
        if (OB_NOT_NULL(shared_dicts_[i])) {
          OB_DELETE(ObFTSharedDict, "FtSharedDict", shared_dicts_[i]);
        }
      }
    }
    is_inited_ = false;
  }
  return ret;
}

int ObFTDictHub::ObFTSharedDict::init(const ObFTDictDesc &desc, ObFTDictHub *hub)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(hub)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(container_ = OB_NEWx(ObFTCacheRangeContainer, &alloc_, alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(hub->load_cache(desc, *container_))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      // The dictionary has not been built yet, build it now. The blocks are
      // pinned by the container, so they stay valid as long as this object
      // is alive.
      ret = OB_SUCCESS;
      if (OB_FAIL(hub->build_cache(desc, *container_))) {
      }
    } else {
    }
  } else {
  }

  if (OB_SUCC(ret)) {
    dict_ = OB_NEWx(ObFTRangeDict, &alloc_, alloc_, container_, desc);
    if (OB_ISNULL(dict_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(dict_->init())) {
    } else {
    }
  }

  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

void ObFTDictHub::ObFTSharedDict::reset()
{
  // The dictionary reads the KV cache blocks pinned by the container, so it
  // has to be destroyed before the container releases them.
  if (OB_NOT_NULL(dict_)) {
    dict_->~ObIFTDict();
    dict_ = nullptr;
  }
  if (OB_NOT_NULL(container_)) {
    container_->~ObFTCacheRangeContainer();
    container_ = nullptr;
  }
  alloc_.reset();
}

int ObFTDictHub::get_shared_dict(const ObFTDictDesc &desc, ObIFTDict *&dict)
{
  int ret = OB_SUCCESS;
  dict = nullptr;
  const int64_t type_idx = static_cast<int64_t>(desc.type_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (type_idx <= 0 || type_idx >= MAX_DICT_TYPE_NUM) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObFTDictInfoKey key(static_cast<uint64_t>(desc.type_));
    {
      ObBucketHashRLockGuard guard(shared_dict_lock_, key.hash());
      if (OB_UNLIKELY(guard.get_ret() != OB_SUCCESS)) {
        ret = guard.get_ret();
      } else if (OB_NOT_NULL(shared_dicts_[type_idx])) {
        dict = shared_dicts_[type_idx]->get_dict();
      }
    }

    if (OB_SUCC(ret) && OB_ISNULL(dict)) {
      ObBucketHashWLockGuard guard(shared_dict_lock_, key.hash());
      if (OB_UNLIKELY(guard.get_ret() != OB_SUCCESS)) {
        ret = guard.get_ret();
      } else {
        ObFTSharedDict *&shared = shared_dicts_[type_idx];
        if (OB_ISNULL(shared)) {
          ObFTSharedDict *new_shared = OB_NEW(ObFTSharedDict, "FtSharedDict");
          if (OB_ISNULL(new_shared)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(new_shared->init(desc, this))) {
            OB_DELETE(ObFTSharedDict, "FtSharedDict", new_shared);
          } else {
            shared = new_shared;
          }
        }
        if (OB_SUCC(ret)) {
          dict = shared->get_dict();
        }
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
