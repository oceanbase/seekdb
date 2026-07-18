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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_FAST_SEGMENT_ARRAY_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_FAST_SEGMENT_ARRAY_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{

// A small append-only segmented array whose allocated blocks survive reuse().
// It is intended for POD-like scratch objects on hot tokenization paths.
template <typename T, int64_t BLOCK_CAPACITY = 256>
class ObFastSegmentArray
{
  static_assert(BLOCK_CAPACITY > 2 && BLOCK_CAPACITY <= (1LL << 30),
                "block capacity is out of range");

public:
  static constexpr int64_t DEFAULT_BLOCK_POINTER_CAPACITY = 64;
  static constexpr int64_t BLOCK_CAPACITY_POWER =
      64 - __builtin_clzll(static_cast<uint64_t>(BLOCK_CAPACITY) - 1);
  static constexpr int64_t REAL_BLOCK_CAPACITY = 1LL << BLOCK_CAPACITY_POWER;
  static constexpr int64_t BLOCK_LOCATOR = REAL_BLOCK_CAPACITY - 1;

  explicit ObFastSegmentArray(
      ObIAllocator &allocator,
      int64_t initial_block_pointer_capacity = DEFAULT_BLOCK_POINTER_CAPACITY)
      : allocator_(allocator),
        blocks_(nullptr),
        block_pointer_capacity_(0),
        block_count_(0),
        size_(0),
        initial_block_pointer_capacity_(initial_block_pointer_capacity > 0
                                            ? initial_block_pointer_capacity
                                            : DEFAULT_BLOCK_POINTER_CAPACITY)
  {
  }

  ~ObFastSegmentArray() { reset(); }

  int push_back(const T &value)
  {
    int ret = OB_SUCCESS;
    const int64_t block_idx = size_ >> BLOCK_CAPACITY_POWER;
    if (OB_FAIL(ensure_block(block_idx))) {
      LOG_WARN("failed to ensure segment array block", K(ret), K(block_idx));
    } else {
      blocks_[block_idx][size_ & BLOCK_LOCATOR] = value;
      ++size_;
    }
    return ret;
  }

  const T &at(int64_t idx) const
  {
    return blocks_[idx >> BLOCK_CAPACITY_POWER][idx & BLOCK_LOCATOR];
  }

  T &at(int64_t idx)
  {
    return const_cast<T &>(static_cast<const ObFastSegmentArray &>(*this).at(idx));
  }

  int64_t count() const { return size_; }
  bool empty() const { return 0 == size_; }

  // Retain blocks so the next document can reuse them without allocations.
  void reuse() { size_ = 0; }

  void reset()
  {
    for (int64_t i = 0; i < block_count_; ++i) {
      if (nullptr != blocks_[i]) {
        allocator_.free(blocks_[i]);
        blocks_[i] = nullptr;
      }
    }
    if (nullptr != blocks_) {
      allocator_.free(blocks_);
    }
    blocks_ = nullptr;
    block_pointer_capacity_ = 0;
    block_count_ = 0;
    size_ = 0;
  }

private:
  int ensure_block(int64_t block_idx)
  {
    int ret = OB_SUCCESS;
    if (block_idx >= block_count_) {
      if (block_idx >= block_pointer_capacity_
          && OB_FAIL(expand_block_pointer_array(block_idx + 1))) {
        LOG_WARN("failed to expand segment block pointer array", K(ret), K(block_idx));
      } else if (OB_UNLIKELY(block_idx != block_count_ || nullptr != blocks_[block_idx])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected segment block index", K(ret), K(block_idx), K(block_count_));
      } else {
        void *buf = allocator_.alloc(REAL_BLOCK_CAPACITY * static_cast<int64_t>(sizeof(T)));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate segment block", K(ret), K(block_idx));
        } else {
          blocks_[block_idx] = static_cast<T *>(buf);
          ++block_count_;
        }
      }
    }
    return ret;
  }

  int expand_block_pointer_array(int64_t needed_capacity)
  {
    int ret = OB_SUCCESS;
    int64_t next_capacity = block_pointer_capacity_ > 0
                                ? block_pointer_capacity_
                                : initial_block_pointer_capacity_;
    while (next_capacity < needed_capacity) {
      next_capacity <<= 1;
    }
    const int64_t bytes = next_capacity * static_cast<int64_t>(sizeof(T *));
    T **next_blocks = static_cast<T **>(allocator_.alloc(bytes));
    if (OB_ISNULL(next_blocks)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate segment block pointers", K(ret), K(next_capacity));
    } else {
      MEMSET(next_blocks, 0, bytes);
      if (nullptr != blocks_) {
        MEMCPY(next_blocks, blocks_, block_count_ * static_cast<int64_t>(sizeof(T *)));
        allocator_.free(blocks_);
      }
      blocks_ = next_blocks;
      block_pointer_capacity_ = next_capacity;
    }
    return ret;
  }

private:
  ObIAllocator &allocator_;
  T **blocks_;
  int64_t block_pointer_capacity_;
  int64_t block_count_;
  int64_t size_;
  const int64_t initial_block_pointer_capacity_;

  DISALLOW_COPY_AND_ASSIGN(ObFastSegmentArray);
};

} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_FAST_SEGMENT_ARRAY_H_
