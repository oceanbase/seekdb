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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_px_bloom_filter.h"
#include "data_plane/encoding/ob_cpu_features.h"

using namespace oceanbase;
using namespace common;
using namespace sql;
using namespace obcall;

#define MIN_FILTER_SIZE 256
#define MAX_BIT_COUNT 17179869184// 2^34 due to the memory single alloc limit
#define BF_BLOCK_SIZE 256L
#define CACHE_LINE_SIZE 64      // 64 bytes
#define LOG_CACHE_LINE_SIZE 6   // = log2(CACHE_LINE_SIZE)

#define FIXED_HASH_COUNT 4
#define WORD_SIZE 64            // WORD_SIZE * FIXED_HASH_COUNT = BF_BLOCK_SIZE
#define BLOCK_FILTER_HASH_MASK 0x3F3F3F3F // for each 8 bits, we only use the last 6 bits

ObPxBloomFilter::ObPxBloomFilter() : data_length_(0), max_bit_count_(0), bits_count_(0), fpp_(0.0),
    hash_func_count_(0), is_inited_(false), bits_array_length_(0),
    bits_array_(NULL), true_count_(0), allocator_()
{

}

int ObPxBloomFilter::init(int64_t data_length, ObIAllocator &allocator,
                          double fpp /*= 0.01 */, int64_t max_filter_size /* =2147483648 */)
{
  int ret = OB_SUCCESS;
  set_allocator_attr();
  data_length = max(data_length, 1);
  if (fpp <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to init px bloom filter", K(ret), K(data_length), K(fpp));
  } else {
    data_length_ = data_length;
    fpp_ = fpp;
    align_max_bit_count(max_filter_size);
    (void)calc_num_of_bits();
    (void)calc_num_of_hash_func();
    bits_array_length_ = ceil((double)bits_count_ / 64);
    void *bits_array_buf = NULL;
    bool simd_support = data_plane::is_avx512_supported();
    might_contain_ = simd_support ? &ObPxBloomFilter::might_contain_simd
                     : &ObPxBloomFilter::might_contain_nonsimd;
    if (OB_ISNULL(bits_array_buf = allocator.alloc(
                                       (CACHE_LINE_SIZE + bits_array_length_) * sizeof(int64_t)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc px bloom filter bits_array_", K(ret), K(bits_count_));
    } else {
      // cache line aligned address.
      int64_t align_addr = ((reinterpret_cast<int64_t>(bits_array_buf)
                            + CACHE_LINE_SIZE - 1) >> LOG_CACHE_LINE_SIZE) << LOG_CACHE_LINE_SIZE;
      bits_array_ = reinterpret_cast<int64_t *>(align_addr);
      MEMSET(bits_array_, 0, bits_array_length_ * sizeof(int64_t));
      is_inited_ = true;
    }
  }
  return ret;
}

int ObPxBloomFilter::assign(const ObPxBloomFilter &filter)
{
  int ret = OB_SUCCESS;
  set_allocator_attr();
  data_length_ = filter.data_length_;
  max_bit_count_ = filter.max_bit_count_;
  block_mask_ = filter.block_mask_;
  bits_count_ = filter.bits_count_;
  fpp_ = filter.fpp_;
  hash_func_count_ = filter.hash_func_count_;
  is_inited_ = filter.is_inited_;
  bits_array_length_ = filter.bits_array_length_;
  true_count_ = filter.true_count_;
  might_contain_ = filter.might_contain_;
  void *bits_array_buf = NULL;
  if (OB_ISNULL(bits_array_buf = allocator_.alloc((bits_array_length_ + CACHE_LINE_SIZE)* sizeof(int64_t)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc filter", K(bits_array_length_), K(ret));
  } else {
    int64_t align_addr = ((reinterpret_cast<int64_t>(bits_array_buf)
                          + CACHE_LINE_SIZE - 1) >> LOG_CACHE_LINE_SIZE) << LOG_CACHE_LINE_SIZE;
    bits_array_ = reinterpret_cast<int64_t *>(align_addr);
    MEMCPY(bits_array_, filter.bits_array_, sizeof(int64_t) * bits_array_length_);
  }
  return ret;
}

void ObPxBloomFilter::set_allocator_attr()
{
  ObMemAttr attr("PxBfAlloc", ObCtxIds::DEFAULT_CTX_ID);
  allocator_.set_attr(attr);
}

void ObPxBloomFilter::reset_filter()
{
  MEMSET(bits_array_, 0, bits_array_length_ * sizeof(int64_t));
}

void ObPxBloomFilter::reset_for_rescan()
{
  // all the member inited should be reset
  data_length_ = 0;
  max_bit_count_ = 0;
  bits_count_ = 0;
  fpp_ = 0;
  hash_func_count_ = 0;
  is_inited_ = false;
  bits_array_length_ = 0;
  bits_array_ = nullptr;
  allocator_.reset();
}

// previous version bits_num = - data_length * ln(p) / (ln2)^2
// close-to 2^n
// blocked bloom filter: fpp = (1 - (1 - 1 / w) ^ x) ^ 4.  x = n / block_count
void ObPxBloomFilter::calc_num_of_bits()
{
  int64_t old_n = ceil(-data_length_ * log(fpp_) / (log(2) * log(2)));
  int64_t n = ceil(data_length_ * BF_BLOCK_SIZE * log(1 - 1.0 / static_cast<double>(WORD_SIZE))
                    / log(1 - pow(fpp_, 1.0 / static_cast<double>(FIXED_HASH_COUNT))));
  int64_t ori_n = n;
  n = n - 1;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;

  // min size is block size = 256.
  bits_count_ = ((n < MIN_FILTER_SIZE) ? MIN_FILTER_SIZE : (n >= max_bit_count_) ? max_bit_count_ : n + 1);
  block_mask_ = (bits_count_ >> (LOG_HASH_COUNT + 6)) - 1;
}

void ObPxBloomFilter::align_max_bit_count(int64_t max_filter_size)
{
  int64_t max_bit_count = max_filter_size * CHAR_BIT;
  if (MAX_BIT_COUNT == max_bit_count) {
    max_bit_count_ = max_bit_count;
  } else {
    max_bit_count_ = next_pow2(max_bit_count);
  }
}

// previous versino: hash_func_nums = bits_num / data_length * log(2)
// hash_func_count_ = BF_BLOCK_SIZE / REG_SIZE = 256 / 64 = 4
void ObPxBloomFilter::calc_num_of_hash_func()
{
  hash_func_count_ = FIXED_HASH_COUNT;
}

int ObPxBloomFilter::put(uint64_t hash)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("the px bloom filter is not inited", K(ret));
  } else {
    uint64_t block_begin = (hash & block_mask_) << LOG_HASH_COUNT;
    uint32_t hash_high = ((uint32_t)(hash >> 32) & BLOCK_FILTER_HASH_MASK);
    uint8_t *block_hash_vals = (uint8_t *)&hash_high;
    (void)set(block_begin, 1L << block_hash_vals[0]);
    (void)set(block_begin + 1, 1L << block_hash_vals[1]);
    (void)set(block_begin + 2, 1L << block_hash_vals[2]);
    (void)set(block_begin + 3, 1L << block_hash_vals[3]);
  }
  return ret;
}

int ObPxBloomFilter::put_batch(uint64_t *batch_hash_values, const EvalBound &bound,
                               const ObBitVector &skip, bool &is_empty)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the px bloom filter is not inited", K(ret));
  } else if (bound.get_all_rows_active()) {
    uint32_t hash_high = 0;
    uint8_t *block_hash_vals = (uint8_t *)&hash_high;
    for (int64_t i = bound.start(); i < bound.end(); ++i) {
      uint64_t block_begin = (batch_hash_values[i] & block_mask_) << LOG_HASH_COUNT;
      hash_high = ((uint32_t)(batch_hash_values[i] >> 32) & BLOCK_FILTER_HASH_MASK);
      (void)set(block_begin, 1L << block_hash_vals[0]);
      (void)set(block_begin + 1, 1L << block_hash_vals[1]);
      (void)set(block_begin + 2, 1L << block_hash_vals[2]);
      (void)set(block_begin + 3, 1L << block_hash_vals[3]);
    }
    if (is_empty && bound.end() - bound.start() > 0) {
      is_empty = false;
    }
  } else {
    uint32_t hash_high = 0;
    uint8_t *block_hash_vals = (uint8_t *)&hash_high;
    for (int64_t i = bound.start(); i < bound.end(); ++i) {
      if (skip.at(i)) {
      } else {
        uint64_t block_begin = (batch_hash_values[i] & block_mask_) << LOG_HASH_COUNT;
        hash_high = ((uint32_t)(batch_hash_values[i] >> 32) & BLOCK_FILTER_HASH_MASK);
        (void)set(block_begin, 1L << block_hash_vals[0]);
        (void)set(block_begin + 1, 1L << block_hash_vals[1]);
        (void)set(block_begin + 2, 1L << block_hash_vals[2]);
        (void)set(block_begin + 3, 1L << block_hash_vals[3]);
        if (is_empty) {
          is_empty = false;
        }
      }
    }
  }
  return ret;
}

int ObPxBloomFilter::might_contain_nonsimd(uint64_t hash, bool &is_match)
{
  int ret = OB_SUCCESS;
  is_match = true;
  uint64_t block_begin = (hash & block_mask_) << LOG_HASH_COUNT;
  uint32_t hash_high = ((uint32_t)(hash >> 32) & BLOCK_FILTER_HASH_MASK);
  uint8_t *block_hash_vals = (uint8_t *)&hash_high;
  if (!get(block_begin, 1L << block_hash_vals[0])) {
    is_match = false;
  } else if (!get(block_begin + 1, 1L << block_hash_vals[1])) {
    is_match = false;
  } else if (!get(block_begin + 2, 1L << block_hash_vals[2])) {
    is_match = false;
  } else if (!get(block_begin + 3, 1L << block_hash_vals[3])) {
    is_match = false;
  }
  return ret;
}

bool ObPxBloomFilter::set(uint64_t word_index, uint64_t bit_index)
{
  if (!get(word_index, bit_index)) {
    int64_t old_v = 0, new_v = 0;
    do {
      old_v = bits_array_[word_index];
      new_v = old_v | bit_index;
    } while(ATOMIC_CAS(&bits_array_[word_index], old_v, new_v) != old_v);
    return true;
  }
  return false;
}

int ObPxBloomFilter::merge_filter(ObPxBloomFilter *filter)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(filter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("filer is null", K(ret));
  } else if (OB_UNLIKELY(bits_array_length_ != filter->bits_array_length_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("bloom filter length mismatch", K(ret), K(bits_array_length_),
             "other_length", filter->bits_array_length_);
  } else {
    int64_t old_v = 0, new_v = 0;
    for (int i = 0; i < filter->bits_array_length_; ++i) {
      do {
        old_v = bits_array_[i];
        new_v = old_v | filter->bits_array_[i];
      } while (old_v != new_v // do not write if old is equal to new
               && ATOMIC_CAS(&bits_array_[i], old_v, new_v) != old_v);
    }
  }
  return ret;
}

void ObPxBloomFilter::reset()
{
  // need reset memory
  allocator_.reset();
}

OB_DEF_SERIALIZE(ObPxBloomFilter)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              data_length_,
              bits_count_,
              fpp_,
              hash_func_count_,
              is_inited_,
              bits_array_length_,
              true_count_);
  for (int i = 0; OB_SUCC(ret) && i < bits_array_length_; ++i) {
    if (OB_FAIL(serialization::encode(buf, buf_len, pos, bits_array_[i]))) {
    }
  }
  OB_UNIS_ENCODE(max_bit_count_);
  return ret;
}

OB_DEF_DESERIALIZE(ObPxBloomFilter)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              data_length_,
              bits_count_,
              fpp_,
              hash_func_count_,
              is_inited_,
              bits_array_length_,
              true_count_);
  int64_t real_len = bits_array_length_;
  void *bits_array_buf = NULL;
  if (OB_ISNULL(bits_array_buf = allocator_.alloc((real_len + CACHE_LINE_SIZE)* sizeof(int64_t)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc filter", K(real_len), K(ret));
  } else {
    // cache line aligned address.
    int64_t align_addr = ((reinterpret_cast<int64_t>(bits_array_buf)
                          + CACHE_LINE_SIZE - 1) >> LOG_CACHE_LINE_SIZE) << LOG_CACHE_LINE_SIZE;
    int64_t *bits_array = reinterpret_cast<int64_t *>(align_addr);
    for (int i = 0; OB_SUCC(ret) && i < real_len; ++i) {
      if (OB_FAIL(serialization::decode(buf, data_len, pos, bits_array[i]))) {
      }
    }
    if (OB_SUCC(ret)) {
      bits_array_ = bits_array;
      might_contain_ = data_plane::is_avx512_supported() ? &ObPxBloomFilter::might_contain_simd
                       : &ObPxBloomFilter::might_contain_nonsimd;
    }
  }
  OB_UNIS_DECODE(max_bit_count_);
  block_mask_ = (bits_count_ >> (LOG_HASH_COUNT + 6)) - 1;
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPxBloomFilter)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
        data_length_,
        bits_count_,
        fpp_,
        hash_func_count_,
        is_inited_,
        bits_array_length_,
        true_count_);
  for (int i = 0; i < bits_array_length_; ++i) {
    len += serialization::encoded_length(bits_array_[i]);
  }
  OB_UNIS_ADD_LEN(max_bit_count_);
  return len;
}


//-------------------------------------division line----------------------------
int ObPxBFStaticInfo::init(int64_t filter_id, bool is_shared,
    bool skip_subpart, int64_t p2p_dh_id,
    bool is_shuffle, ObLogJoinFilter *log_join_filter_create_op)
{
  int ret = OB_SUCCESS;
  if (is_inited_){
    ret = OB_INIT_TWICE;
    LOG_WARN("twice init bf static info", K(ret));
  } else {
    
    filter_id_ = filter_id;
    is_shared_ = is_shared;
    skip_subpart_ = skip_subpart;
    p2p_dh_id_ = p2p_dh_id;
    is_shuffle_ = is_shuffle;
    log_join_filter_create_op_ = log_join_filter_create_op;
    is_inited_ = true;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObPxBFStaticInfo, is_inited_, filter_id_,
    is_shared_, skip_subpart_, p2p_dh_id_, is_shuffle_);
