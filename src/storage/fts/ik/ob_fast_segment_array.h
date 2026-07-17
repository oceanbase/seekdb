/**
 * Copyright (c) 2024 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _OCEANBASE_STORAGE_FTS_OB_FAST_SEGMENT_ARRAY_H_
#define _OCEANBASE_STORAGE_FTS_OB_FAST_SEGMENT_ARRAY_H_

#define USING_LOG_PREFIX STORAGE_FTS

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

#include <new>

namespace oceanbase
{
namespace storage
{

// FTS next-stage optimization (Op1): retain geometrically addressed blocks
// between IK batches so appending a token does not allocate a list node.
template <typename T, int64_t block_capacity = 256>
class ObFastSegmentArray
{
  static_assert(block_capacity > 2 && block_capacity <= (1 << 30),
      "block capacity must be in (2, 1 << 30]");

public:
  static constexpr int64_t BLOCK_POINTER_ARRAY_CAPACITY = 64;
  static constexpr int64_t BLOCK_CAPACITY_POWER
      = 64 - __builtin_clzll(static_cast<uint64_t>(block_capacity) - 1);
  static constexpr int64_t REAL_BLOCK_CAPACITY = static_cast<int64_t>(1) << BLOCK_CAPACITY_POWER;
  static constexpr int64_t BLOCK_LOCATOR = REAL_BLOCK_CAPACITY - 1;

  explicit ObFastSegmentArray(
      ObIAllocator &allocator,
      int64_t init_block_arr_cap = BLOCK_POINTER_ARRAY_CAPACITY)
      : allocator_(allocator),
        block_arr_(nullptr),
        block_arr_cap_(0),
        block_count_(0),
        size_(0),
        init_block_arr_cap_(init_block_arr_cap > 0
                                ? init_block_arr_cap
                                : BLOCK_POINTER_ARRAY_CAPACITY)
  {}

  ~ObFastSegmentArray() { reset(); }

  int push_back(const T &value)
  {
    int ret = OB_SUCCESS;
    const int64_t block_idx = size_ >> BLOCK_CAPACITY_POWER;
    if (OB_FAIL(ensure_block_(block_idx))) {
    } else {
      block_arr_[block_idx][size_ & BLOCK_LOCATOR] = value;
      ++size_;
    }
    return ret;
  }

  const T &at(const int64_t idx) const
  {
    return block_arr_[idx >> BLOCK_CAPACITY_POWER][idx & BLOCK_LOCATOR];
  }

  T &at(const int64_t idx)
  {
    return const_cast<T &>(static_cast<const ObFastSegmentArray &>(*this).at(idx));
  }

  int64_t count() const { return size_; }
  bool empty() const { return 0 == size_; }
  void reuse() { size_ = 0; }

  void reset()
  {
    for (int64_t i = 0; i < block_count_; ++i) {
      if (nullptr != block_arr_[i]) {
        // Objects are constructed when a block is allocated. Destroy all of
        // them here so this container remains safe for non-trivial T as well.
        for (int64_t j = 0; j < REAL_BLOCK_CAPACITY; ++j) {
          block_arr_[i][j].~T();
        }
        allocator_.free(block_arr_[i]);
        block_arr_[i] = nullptr;
      }
    }
    if (nullptr != block_arr_) {
      allocator_.free(block_arr_);
    }
    block_arr_ = nullptr;
    block_arr_cap_ = 0;
    block_count_ = 0;
    size_ = 0;
  }

private:
  int ensure_block_(const int64_t block_idx)
  {
    int ret = OB_SUCCESS;
    if (block_idx >= block_count_) {
      if (block_idx != block_count_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("non-sequential block request", K(ret), K(block_idx), K_(block_count));
      } else if (block_idx >= block_arr_cap_ && OB_FAIL(expand_block_array_(block_idx + 1))) {
        LOG_WARN("failed to expand block array", K(ret), K(block_idx));
      } else {
        void *buf = allocator_.alloc(REAL_BLOCK_CAPACITY * static_cast<int64_t>(sizeof(T)));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate segment block", K(ret), K(block_idx));
        } else {
          T *block = static_cast<T *>(buf);
          int64_t constructed = 0;
          for (; constructed < REAL_BLOCK_CAPACITY; ++constructed) {
            new (&block[constructed]) T();
          }
          block_arr_[block_idx] = block;
          ++block_count_;
        }
      }
    }
    return ret;
  }

  int expand_block_array_(const int64_t need_cap)
  {
    int ret = OB_SUCCESS;
    int64_t next_cap = block_arr_cap_ > 0 ? block_arr_cap_ : init_block_arr_cap_;
    while (next_cap < need_cap) {
      next_cap <<= 1;
    }
    const int64_t bytes = next_cap * static_cast<int64_t>(sizeof(T *));
    T **next_arr = static_cast<T **>(allocator_.alloc(bytes));
    if (OB_ISNULL(next_arr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to expand segment block pointers", K(ret), K(next_cap));
    } else {
      MEMSET(next_arr, 0, bytes);
      if (nullptr != block_arr_) {
        MEMCPY(next_arr, block_arr_, block_count_ * static_cast<int64_t>(sizeof(T *)));
        allocator_.free(block_arr_);
      }
      block_arr_ = next_arr;
      block_arr_cap_ = next_cap;
    }
    return ret;
  }

private:
  ObIAllocator &allocator_;
  T **block_arr_;
  int64_t block_arr_cap_;
  int64_t block_count_;
  int64_t size_;
  const int64_t init_block_arr_cap_;

  DISALLOW_COPY_AND_ASSIGN(ObFastSegmentArray);
};

} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_OB_FAST_SEGMENT_ARRAY_H_
