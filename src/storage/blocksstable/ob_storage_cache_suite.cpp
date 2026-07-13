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

#include "ob_storage_cache_suite.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace blocksstable
{
ObStorageCacheSuite::ObStorageCacheSuite()
  : index_block_cache_(),
    user_block_cache_(),
    user_row_cache_(),
    bf_cache_(),
    fuse_row_cache_(),
    storage_meta_cache_(),
    truncate_info_cache_(),
    tablet_split_cache_(),
    is_inited_(false)
{
}

ObStorageCacheSuite::~ObStorageCacheSuite()
{
  destroy();
}

ObStorageCacheSuite &ObStorageCacheSuite::get_instance()
{
  static ObStorageCacheSuite instance_;
  return instance_;
}

int ObStorageCacheSuite::init(const int64_t bf_cache_miss_count_threshold)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "The cache suite has been inited, ", K(ret));
  } else if (OB_FAIL(index_block_cache_.init("index_block_cache"))) {
    STORAGE_LOG(ERROR, "init infrc block cache failed", K(ret));
  } else if (OB_FAIL(user_block_cache_.init("user_block_cache"))) {
    STORAGE_LOG(ERROR, "init user block cache failed, ", K(ret));
  } else if (OB_FAIL(user_row_cache_.init("user_row_cache"))) {
    STORAGE_LOG(ERROR, "init user sstable row cache failed, ", K(ret));
  } else if (OB_FAIL(bf_cache_.init("bf_cache"))) {
    STORAGE_LOG(ERROR, "init bloom filter cache failed, ", K(ret));
  } else if (OB_FAIL(bf_cache_.set_bf_cache_miss_count_threshold(bf_cache_miss_count_threshold))) {
    STORAGE_LOG(ERROR, "failed to set bf_cache_miss_count_threshold", K(ret));
  } else if (OB_FAIL(fuse_row_cache_.init("fuse_row_cache"))) {
    STORAGE_LOG(ERROR, "fail to init fuse row cache", K(ret));
  } else if (OB_FAIL(storage_meta_cache_.init("storage_meta_cache"))) {
    STORAGE_LOG(ERROR, "fail to init storage meta cache", K(ret));
  } else if (OB_FAIL(truncate_info_cache_.init("truncate_info_cache"))) {
    STORAGE_LOG(ERROR, "fail to init truncate info cache", K(ret));
  } else if (OB_FAIL(tablet_split_cache_.init("tablet_split_cache"))) {
    STORAGE_LOG(ERROR, "fail to init truncate info cache", K(ret));
  } else {
    is_inited_ = true;
  }

  if (OB_UNLIKELY(OB_SUCCESS != ret && !is_inited_)) {
    destroy();
  }
  return ret;
}

int ObStorageCacheSuite::set_bf_cache_miss_count_threshold(const int64_t bf_cache_miss_count_threshold)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(bf_cache_.set_bf_cache_miss_count_threshold(bf_cache_miss_count_threshold))) {
    STORAGE_LOG(WARN, "failed to set bf_cache_miss_count_threshold", K(ret), K(bf_cache_miss_count_threshold));
  }
  return ret;
}

void ObStorageCacheSuite::destroy()
{
  index_block_cache_.destroy();
  user_block_cache_.destroy();
  user_row_cache_.destroy();
  bf_cache_.destroy();
  fuse_row_cache_.destroy();
  storage_meta_cache_.destory();
  truncate_info_cache_.destroy();
  tablet_split_cache_.destroy();
  is_inited_ = false;
}

}
}
