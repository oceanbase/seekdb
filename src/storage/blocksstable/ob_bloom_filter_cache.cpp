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

#define USING_LOG_PREFIX STORAGE

#include <climits>
#include <cmath>
#include "ob_bloom_filter_cache.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/access/ob_rows_info.h"
#include "storage/access/ob_empty_read_bucket.h"

namespace oceanbase
{
using namespace common;
namespace blocksstable
{

ObBloomFilter::ObBloomFilter() : allocator_(ObModIds::OB_BLOOM_FILTER), nhash_(0), nbit_(0), bits_(NULL)
{
}

ObBloomFilter::~ObBloomFilter()
{
  destroy();
}

int ObBloomFilter::deep_copy(const ObBloomFilter &other)
{
  int ret = OB_SUCCESS;

  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LIB_LOG(WARN, "The ObBloomFilter has data.", K(ret));
  } else if (NULL == (bits_ = reinterpret_cast<uint8_t*>(allocator_.alloc(calc_nbyte(other.nbit_))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LIB_LOG(ERROR, "Fail to allocate memory, ", K(ret));
  } else {
    nbit_ = other.nbit_;
    nhash_ = other.nhash_;
    MEMCPY(bits_, other.bits_, calc_nbyte(nbit_));
  }

  return ret;
}

int ObBloomFilter::deep_copy(const ObBloomFilter &other, char *buffer)
{
  int ret = OB_SUCCESS;

  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LIB_LOG(WARN, "The ObBloomFilter has data.", K(ret));
  } else {
    nbit_ = other.nbit_;
    nhash_ = other.nhash_;
    bits_ = reinterpret_cast<uint8_t*>(buffer);
    MEMCPY(bits_, other.bits_, calc_nbyte(nbit_));
  }

  return ret;
}

int64_t ObBloomFilter::get_deep_copy_size() const
{
  return calc_nbyte(nbit_);
}

int64_t ObBloomFilter::calc_nbyte(const int64_t nbit) const
{
  return (nbit / CHAR_BIT + (nbit % CHAR_BIT ? 1 : 0));
}

double ObBloomFilter::calc_nhash(const double false_positive_prob) const
{
  return -std::log(false_positive_prob) / std::log(2);
}

int ObBloomFilter::init_by_row_count(const int64_t element_count, const double false_positive_prob)
{
  int ret = OB_SUCCESS;
  if (element_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "bloom filter element_count should be > 0", K(element_count), K(ret));
  } else if (!(false_positive_prob < 1.0 && false_positive_prob > 0.0)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "bloom filter false_positive_prob should be < 1.0 and > 0.0", K(false_positive_prob), K(ret));
  } else {
    double num_hashes = calc_nhash(false_positive_prob);
    int64_t num_bits = static_cast<int64_t>((static_cast<double>(element_count)
                                             * num_hashes / static_cast<double>(std::log(2))));
    int64_t num_bytes = calc_nbyte(num_bits);
    bits_ = (uint8_t *)allocator_.alloc(static_cast<int32_t>(num_bytes));
    if (NULL == bits_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LIB_LOG(ERROR, "bits_ null pointer, ", K_(nbit), K(ret));
    } else {
      memset(bits_, 0, num_bytes);
      nhash_ = static_cast<int64_t>(num_hashes);
      nbit_ = num_bits;
    }
  }
  return ret;
}

void ObBloomFilter::destroy()
{
  if (NULL != bits_) {
    allocator_.free(bits_);
    allocator_.reset();
    bits_ = NULL;
    nhash_ = 0;
    nbit_ = 0;
  }
}

void ObBloomFilter::clear()
{
  if (NULL != bits_) {
    memset(bits_, 0, calc_nbyte(nbit_));
  }
}

int ObBloomFilter::insert(const uint32_t key_hash)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LIB_LOG(WARN, "bloom filter has not inited", K_(bits), K_(nbit), K_(nhash), K(ret));
  } else {
    const uint64_t hash = key_hash;
    const uint64_t delta = ((hash >> 17) | (hash << 15)) % nbit_;
    uint64_t  bit_pos = hash % nbit_;
    for (int64_t i = 0; i < nhash_; i++) {
      bits_[bit_pos / CHAR_BIT] = static_cast<unsigned char>(bits_[bit_pos / CHAR_BIT] | (1 << (bit_pos % CHAR_BIT)));
      bit_pos = (bit_pos + delta) < nbit_ ? bit_pos + delta : bit_pos + delta - nbit_;
    }
  }
  return ret;
}

int ObBloomFilter::merge(const ObBloomFilter &src_bf)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(src_bf.bits_ == nullptr || bits_ == nullptr ||
                  src_bf.nhash_ != nhash_ || src_bf.nbit_ != nbit_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to merge bloom filter, invalid argument", K(ret), K(src_bf), KPC(this));
  } else {
    const int64_t nbyte = get_nbytes();
    for (int64_t i = 0; i < nbyte; ++i) {
      bits_[i] |= src_bf.bits_[i];
    }
  }
  return ret;
}

int ObBloomFilter::may_contain(const uint32_t key_hash, bool &is_contain) const
{
  int ret = OB_SUCCESS;
  is_contain = true;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LIB_LOG(WARN, "bloom filter has not inited, ", K_(bits), K_(nbit), K_(nhash), K(ret));
  } else {
    const uint64_t hash = key_hash;
    const uint64_t delta = ((hash >> 17) | (hash << 15)) % nbit_;
    uint64_t bit_pos = hash % nbit_;
    for (int64_t i = 0; i < nhash_; ++i) {
      if (0 == (bits_[bit_pos / CHAR_BIT] & (1 << (bit_pos % CHAR_BIT)))) {
        is_contain = false;
        break;
      }
      bit_pos = (bit_pos + delta) < nbit_ ? bit_pos + delta : bit_pos + delta - nbit_;
    }
  }
  return ret;
}

/**
 * ----------------------------------------------------ObBloomFilterCacheKey--------------------------------------------------
 */
ObBloomFilterCacheKey::ObBloomFilterCacheKey(
  const MacroBlockId &block_id, const int8_t prefix_rowkey_len)
  : macro_block_id_(block_id), prefix_rowkey_len_(prefix_rowkey_len)
{
}

ObBloomFilterCacheKey::~ObBloomFilterCacheKey()
{
}

uint64_t ObBloomFilterCacheKey::hash() const
{
  uint64_t hash_val = macro_block_id_.hash();
  const uint64_t sum = prefix_rowkey_len_;
  hash_val = murmurhash(&sum, sizeof(uint64_t), hash_val);
  return hash_val;
}

bool ObBloomFilterCacheKey::operator==(const common::ObIKVCacheKey &other) const
{
  const ObBloomFilterCacheKey &other_bfkey = reinterpret_cast<const ObBloomFilterCacheKey&> (other);
  return true
      && macro_block_id_ == other_bfkey.macro_block_id_
      && prefix_rowkey_len_ == other_bfkey.prefix_rowkey_len_;
}



int64_t ObBloomFilterCacheKey::size() const
{
  return static_cast<int64_t>(sizeof(*this));
}

int ObBloomFilterCacheKey::deep_copy(char *buf, const int64_t buf_len, common::ObIKVCacheKey *&key) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == buf || buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument, ", KP(buf), K(buf_len), K(ret));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_DATA;
    STORAGE_LOG(WARN, "The bloom filter cache key is invalid, ", K(*this), K(ret));
  } else {
    key = new (buf) ObBloomFilterCacheKey(macro_block_id_, prefix_rowkey_len_);
  }
  return ret;
}

bool ObBloomFilterCacheKey::is_valid() const
{
  return true
      && macro_block_id_.is_valid()
      && 0 < prefix_rowkey_len_;
}

/**
 * --------------------------------------------------ObBloomFilterCacheValue--------------------------------------------------
 */
ObBloomFilterCacheValue::ObBloomFilterCacheValue()
  : rowkey_column_cnt_(0),
    row_count_(0),
    bloom_filter_(),
    is_inited_(false)
{
}

ObBloomFilterCacheValue::~ObBloomFilterCacheValue()
{
}

void ObBloomFilterCacheValue::reset()
{
  rowkey_column_cnt_ = 0;
  bloom_filter_.destroy();
  row_count_ = 0;
  is_inited_ = false;
}

void ObBloomFilterCacheValue::reuse()
{
  row_count_ = 0;
  bloom_filter_.clear();
}

int64_t ObBloomFilterCacheValue::size() const
{
  return static_cast<int64_t>(sizeof(*this) + bloom_filter_.get_deep_copy_size());
}

int ObBloomFilterCacheValue::deep_copy(ObBloomFilterCacheValue &bf_cache_value) const
{
  int ret = common::OB_SUCCESS;

  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_DATA;
    STORAGE_LOG(WARN, "The bloom filter cache value is not valid", K(*this), K(ret));
  } else {
    bf_cache_value.reset();
    if (OB_FAIL(bf_cache_value.bloom_filter_.deep_copy(bloom_filter_))) {
    } else {
      bf_cache_value.rowkey_column_cnt_ = rowkey_column_cnt_;
      bf_cache_value.row_count_ = row_count_;
      bf_cache_value.is_inited_ = true;
    }
  }

  return ret;
}

int ObBloomFilterCacheValue::deep_copy(char *buf, const int64_t buf_len, common::ObIKVCacheValue *&value) const
{
  int ret = common::OB_SUCCESS;

  if (OB_UNLIKELY(NULL == buf || buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument, ", K(buf), K(buf_len), K(ret));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_DATA;
    STORAGE_LOG(WARN, "The bloom filter cache value is not valid, ", K(*this), K(ret));
  } else {
    ObBloomFilterCacheValue *bfcache_value = new (buf) ObBloomFilterCacheValue();
    if (OB_FAIL(bfcache_value->bloom_filter_.deep_copy(bloom_filter_, buf + sizeof(*bfcache_value)))) {
    } else {
      bfcache_value->rowkey_column_cnt_ = rowkey_column_cnt_;
      bfcache_value->row_count_ = row_count_;
      bfcache_value->is_inited_ = true;
      value = bfcache_value;
    }
  }

  return ret;
}

int ObBloomFilterCacheValue::init(const int64_t rowkey_column_cnt, const int64_t row_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(rowkey_column_cnt <= 0 || row_cnt <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument, ", K(rowkey_column_cnt), K(row_cnt), K(ret));
  } else if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "The bloom filter cache value has been inited, ", K(ret));
  } else if (OB_FAIL(bloom_filter_.init_by_row_count(row_cnt))) {
  } else {
    rowkey_column_cnt_ = static_cast<int16_t>(rowkey_column_cnt);
    row_count_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObBloomFilterCacheValue::insert(const uint32_t hash)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "The bloom filter cache value has not been inited, ", K(ret));
  } else if (OB_FAIL(bloom_filter_.insert(hash))) {
  } else {
    row_count_++;
  }
  return ret;
}

int ObBloomFilterCacheValue::may_contain(const uint32_t hash, bool &is_contain) const
{
  int ret = OB_SUCCESS;
  is_contain = true;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "The bloom filter cache value has not been inited, ", K(ret));
  } else if (OB_FAIL(bloom_filter_.may_contain(hash, is_contain))) {
  }
  return ret;
}

bool ObBloomFilterCacheValue::is_valid() const
{
  return is_inited_ && rowkey_column_cnt_ > 0;
}

bool ObBloomFilterCacheValue::could_merge_bloom_filter(const ObBloomFilterCacheValue &bf_cache_value) const
{
  bool bret = false;

  if (OB_UNLIKELY(!is_valid() || !bf_cache_value.is_valid())) {
  } else if (bf_cache_value.rowkey_column_cnt_ != rowkey_column_cnt_) {
  } else if (bf_cache_value.bloom_filter_.get_nhash() != bloom_filter_.get_nhash()
          || bf_cache_value.bloom_filter_.get_nbit() != bloom_filter_.get_nbit()) {
  } else {
    bret = true;
  }

  return bret;
}

int ObBloomFilterCacheValue::merge_bloom_filter(const ObBloomFilterCacheValue &bf_cache_value)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "Unexcepted invalid bloomfilter to merge", K_(rowkey_column_cnt), K_(is_inited), K(ret));
  } else if (OB_UNLIKELY(!bf_cache_value.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid bloomfilter cache to merge", K(ret));
  } else if (OB_UNLIKELY(!could_merge_bloom_filter(bf_cache_value))) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "Unexcepted bloomfitler cache to merge", K(bf_cache_value), K_(rowkey_column_cnt), K_(bloom_filter), K(ret));
  } else if (OB_FAIL(bloom_filter_.merge(bf_cache_value.get_bloom_filter()))) {
  } else {
    row_count_ += bf_cache_value.get_row_count();
  }

  return ret;
}

/**
 * ----------------------------------------------------ObBloomFilterCache----------------------------------------------------
 */
ObBloomFilterCache::ObBloomFilterCache()
  : bf_cache_miss_count_threshold_(DEFAULT_EMPTY_READ_CNT_THRESHOLD)
{
}

ObBloomFilterCache::~ObBloomFilterCache()
{
}

int ObBloomFilterCache::put_bloom_filter(const MacroBlockId& macro_block_id,
    const ObBloomFilterCacheValue &bf_value)
{
  int ret = OB_SUCCESS;
  ObBloomFilterCacheKey bf_key(macro_block_id, static_cast<int8_t>(bf_value.get_prefix_len()) );
  bool overwrite = true;
  if (OB_UNLIKELY(!bf_key.is_valid() || !bf_value.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument, ", K(bf_key), K(bf_value), K(ret));
  } else if (OB_FAIL(put(bf_key, bf_value, overwrite))) {
  }

  if (OB_SUCC(ret)) {
    storage::ObEmptyReadCell *cell = NULL;
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObEmptyReadBucket>()->get_cell(bf_key.hash(), cell))) {
    } else if (OB_ISNULL(cell)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected error, the cell value is NULL, ", K(ret));
    } else {
      cell->reset();//ignore ret
    }
    auto_bf_cache_miss_count_threshold(::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()->get_bf_queue_size());
  }
  return ret;
}

int ObBloomFilterCache::may_contain(
    const MacroBlockId &macro_block_id,
    const ObDatumRowkey &rowkey,
    const ObStorageDatumUtils &datum_utils,
    bool &is_contain)
{
  int ret = OB_SUCCESS;
  is_contain = true;
  ObBloomFilterCacheKey bf_key(macro_block_id, static_cast<int8_t>(rowkey.get_datum_cnt()) );
  const ObBloomFilterCacheValue *bf_value = NULL;
  ObKVCacheHandle handle;
  uint64_t key_hash = 0;

  if (OB_UNLIKELY(!bf_key.is_valid() || !rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument, ", K(bf_key), K(rowkey), K(ret));
  } else if (0 == bf_cache_miss_count_threshold_) {
    //disable bf cache
  } else if (OB_FAIL(get(bf_key, bf_value, handle))) {
    if (OB_UNLIKELY(OB_ENTRY_NOT_EXIST != ret)) {
      STORAGE_LOG(WARN, "Fail to get bloom filter cache, ", K(ret));
    }
  } else {
    if (OB_ISNULL(bf_value)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected error, the bf_value is NULL, ", K(ret));
    } else if (OB_FAIL(rowkey.murmurhash(0, datum_utils, key_hash))) {
    } else if (OB_FAIL(bf_value->may_contain(static_cast<uint32_t>(key_hash), is_contain))) {
    } else {
    }
  }
  return ret;
}

int ObBloomFilterCache::may_contain(
    const MacroBlockId &macro_block_id,
    const storage::ObRowsInfo *rows_info,
    const int64_t rowkey_begin_idx,
    const int64_t rowkey_end_idx,
    const ObStorageDatumUtils &datum_utils,
    bool &is_contain)
{
  int ret = OB_SUCCESS;
  is_contain = false;
  auto *my_rows_info = const_cast<storage::ObRowsInfo *>(rows_info);
  ObBloomFilterCacheKey bf_key(macro_block_id, static_cast<int8_t>(my_rows_info->get_datum_cnt()));
  const ObBloomFilterCacheValue *bf_value = NULL;
  ObKVCacheHandle handle;
  uint64_t key_hash = 0;
  if (OB_UNLIKELY(!bf_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(bf_key), K(ret));
  } else if (0 == bf_cache_miss_count_threshold_) {
    is_contain = true;
  } else if (OB_FAIL(get(bf_key, bf_value, handle))) {
    if (OB_UNLIKELY(OB_ENTRY_NOT_EXIST != ret)) {
      STORAGE_LOG(WARN, "Fail to get bloom filter cache, ", K(ret));
    }
  } else {
    if (OB_ISNULL(bf_value)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected null bf value", K(ret));
    } else {
      for (int64_t i = rowkey_begin_idx; OB_SUCC(ret) && i < rowkey_end_idx; ++i) {
        bool tmp_contain = false;
        const ObDatumRowkey &rowkey = rows_info->get_rowkey(i);
        if (rows_info->is_row_skipped(i)) {
          continue;
        } else if (OB_FAIL(rowkey.murmurhash(0, datum_utils, key_hash))) {
        } else if (OB_FAIL(bf_value->may_contain(static_cast<uint32_t>(key_hash), tmp_contain))) {
        } else {
          if (tmp_contain) {
            is_contain = true;
          } else {
            if (!my_rows_info->is_row_bf_checked(i)) {
              my_rows_info->set_row_non_existent(i);
            }
          }
          my_rows_info->set_row_bf_checked(i);
        }
      }
    }
  }
  return ret;
}

int ObBloomFilterCache::may_contain(
    const MacroBlockId &macro_block_id,
    const storage::ObRowKeysInfo *rowkeys_info,
    const int64_t rowkey_begin_idx,
    const int64_t rowkey_end_idx,
    const ObStorageDatumUtils &datum_utils,
    bool &is_contain)
{
  int ret = OB_SUCCESS;
  is_contain = false;
  storage::ObRowKeysInfo *my_rowkeys_info = const_cast<storage::ObRowKeysInfo *>(rowkeys_info);
  ObBloomFilterCacheKey bf_key(macro_block_id, static_cast<int8_t>(my_rowkeys_info->get_rowkey(rowkey_begin_idx).get_datum_cnt()));
  const ObBloomFilterCacheValue *bf_value = NULL;
  ObKVCacheHandle handle;
  uint64_t key_hash = 0;
  if (OB_UNLIKELY(!bf_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(bf_key), K(ret));
  } else if (0 == bf_cache_miss_count_threshold_) {
    is_contain = true;
  } else if (OB_FAIL(get(bf_key, bf_value, handle))) {
    if (OB_UNLIKELY(OB_ENTRY_NOT_EXIST != ret)) {
      STORAGE_LOG(WARN, "Fail to get bloom filter cache, ", K(ret));
    }
  } else {
    if (OB_ISNULL(bf_value)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected null bf value", K(ret));
    } else {
      for (int64_t i = rowkey_begin_idx; OB_SUCC(ret) && i < rowkey_end_idx; ++i) {
        bool tmp_contain = false;
        const ObDatumRowkey &rowkey = rowkeys_info->get_rowkey(i);
        if (rowkeys_info->is_rowkey_not_exist(i)) {
          continue;
        } else if (OB_FAIL(rowkey.murmurhash(0, datum_utils, key_hash))) {
        } else if (OB_FAIL(bf_value->may_contain(static_cast<uint32_t>(key_hash), tmp_contain))) {
        } else {
          if (tmp_contain) {
            if (i == rowkey_begin_idx) {
              is_contain = true;
            }
          } else {
            my_rowkeys_info->set_rowkey_not_exist(i);
          }
        }
      }
    }
  }
  return ret;
}


int ObBloomFilterCache::inc_empty_read(
    const uint64_t table_id,
    const MacroBlockId &macro_id,
    const int64_t empty_read_prefix,
    const int64_t empty_read_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!macro_id.is_valid()
                  || (table_id == OB_INVALID_ID || table_id < 0)
                  || !(empty_read_prefix > 0
                       && empty_read_prefix <= OB_USER_MAX_ROWKEY_COLUMN_NUMBER /* max rowkey column count */))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(macro_id), K(table_id), K(empty_read_prefix));
  } else if (0 == bf_cache_miss_count_threshold_) {
    // bf cache is disabled, do nothing
  } else {
    const ObBloomFilterCacheKey bfc_key(macro_id, empty_read_prefix);
    uint64_t key_hash = bfc_key.hash();
    uint64_t cur_cnt = 1;
    storage::ObEmptyReadCell *cell = nullptr;
    if (OB_UNLIKELY(!bfc_key.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid argument", K(bfc_key), K(ret));
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObEmptyReadBucket>()->get_cell(key_hash, cell))) {
    } else if (OB_ISNULL(cell)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected error, the cell value is NULL", K(ret));
    } else if (OB_FAIL(cell->inc_and_fetch(key_hash, empty_read_cnt, cur_cnt))) {
    } else if (cell->check_timeout()) {
      // do nothing
    } else if (cur_cnt > bf_cache_miss_count_threshold_) {
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()
                      ->schedule_build_bloomfilter(table_id, macro_id, empty_read_prefix))) {
      } else {
        cell->reset();
      }
    }
  }
  return ret;
}

int ObBloomFilterCache::check_need_build(const ObBloomFilterCacheKey &bf_key, bool &need_build)
{
  int ret = OB_SUCCESS;
  const ObBloomFilterCacheValue *bf_value = NULL;
  ObKVCacheHandle handle;
  need_build = false;
  if (!bf_key.is_valid()) {
    // do nothing
  } else if (OB_FAIL(get(bf_key, bf_value, handle))) {
    if (OB_UNLIKELY(OB_ENTRY_NOT_EXIST != ret)) {
      STORAGE_LOG(WARN, "Fail to get bloom filter cache, ", K(ret));
    } else {
      need_build = true;
      ret = OB_SUCCESS;
    }
  } else if (bf_value->get_prefix_len() != bf_key.get_prefix_rowkey_len()) {
    need_build = true;
  }
  return ret;
}

int ObBloomFilterCache::init(const char *cache_name)
{
  int ret = OB_SUCCESS;
  // size must be 2^n, for fast mod
  if (OB_FAIL((common::ObKVCache<ObBloomFilterCacheKey, ObBloomFilterCacheValue>::init(cache_name)))) {
  }
  return ret;
}

void ObBloomFilterCache::destroy()
{
  common::ObKVCache<ObBloomFilterCacheKey, ObBloomFilterCacheValue>::destroy();
}

} /* namespace blocksstable */
} /* namespace oceanbase */
