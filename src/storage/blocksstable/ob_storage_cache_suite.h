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

#ifndef __OCEANBASE_BLOCKSSTABLE_STORAGE_CACHE_SUITE_H__
#define __OCEANBASE_BLOCKSSTABLE_STORAGE_CACHE_SUITE_H__

#include "storage/meta_mem/ob_storage_meta_cache.h"
#include "share/schema/ob_table_schema.h"
#include "ob_micro_block_cache.h"
#include "ob_row_cache.h"
#include "ob_fuse_row_cache.h"
#include "ob_bloom_filter_cache.h"
#include "storage/truncate_info/ob_truncate_info_kv_cache.h"
#include "storage/ddl/ob_tablet_split_cache.h"

#define OB_STORE_CACHE oceanbase::blocksstable::ObStorageCacheSuite::get_instance()

namespace oceanbase
{
namespace blocksstable
{

class ObStorageCacheSuite
{
public:
  static ObStorageCacheSuite &get_instance();
  int init(
      const int64_t index_block_cache_priority,
      const int64_t user_block_cache_priority,
      const int64_t user_row_cache_priority,
      const int64_t fuse_row_cache_priority,
      const int64_t bf_cache_priority,
      const int64_t bf_cache_miss_count_threshold,
      const int64_t storage_meta_cache_priority);
  int reset_priority(
      const int64_t index_block_cache_priority,
      const int64_t user_block_cache_priority,
      const int64_t user_row_cache_priority,
      const int64_t fuse_row_cache_priority,
      const int64_t bf_cache_priority,
      const int64_t storage_meta_cache_priority);
  int set_bf_cache_miss_count_threshold(const int64_t bf_cache_miss_count_threshold);
  ObDataMicroBlockCache &get_block_cache() { return user_block_cache_; }
  ObIndexMicroBlockCache &get_index_block_cache() { return index_block_cache_; }
  ObDataMicroBlockCache &get_micro_block_cache(const bool is_data_block)
  { return is_data_block ? user_block_cache_ : index_block_cache_; }
  ObRowCache &get_row_cache() { return user_row_cache_; }
  ObBloomFilterCache &get_bf_cache() { return bf_cache_; }
  ObFuseRowCache &get_fuse_row_cache() { return fuse_row_cache_; }
  ObMultiVersionFuseRowCache &get_multi_version_fuse_row_cache() { return multi_version_fuse_row_cache_; }
  ObStorageMetaCache &get_storage_meta_cache() { return storage_meta_cache_; }
  storage::ObTruncateInfoKVCache &get_truncate_info_cache() { return truncate_info_cache_; }
  storage::ObTabletSplitCache &get_tablet_split_cache() { return tablet_split_cache_; }
  void destroy();
  inline bool is_inited() const { return is_inited_; }
  TO_STRING_KV(K(is_inited_));
private:
  ObStorageCacheSuite();
  virtual ~ObStorageCacheSuite();
  static const int64_t TRUNCATE_INFO_KV_CACHE_PRIORITY = 10;
  static const int64_t TABLET_SPLIT_CACHE_PRIORITY = 10;
  ObIndexMicroBlockCache index_block_cache_;
  ObDataMicroBlockCache user_block_cache_;
  ObRowCache user_row_cache_;
  ObBloomFilterCache bf_cache_;
  ObFuseRowCache fuse_row_cache_;
  ObStorageMetaCache storage_meta_cache_;
  ObMultiVersionFuseRowCache multi_version_fuse_row_cache_;
  storage::ObTruncateInfoKVCache truncate_info_cache_;
  ObTabletSplitCache tablet_split_cache_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObStorageCacheSuite);
};

}
}
#endif //__OCEANBASE_BLOCKSSTABLE_STORAGE_CACHE_SUITE_H__
