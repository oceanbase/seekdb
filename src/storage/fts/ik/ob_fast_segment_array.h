/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef OB_FAST_SEGMENT_ARRAY_H_
#define OB_FAST_SEGMENT_ARRAY_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include <cstdint>
#include <new>

namespace oceanbase
{
namespace storage
{

// A POD-oriented segmented array. Reuse is O(1) and retains allocated blocks.
template <typename T, int64_t BLOCK_CAPACITY = 256>
class ObFastSegmentArray
{
  static_assert(BLOCK_CAPACITY > 2 && BLOCK_CAPACITY <= (1L << 30),
      "invalid segmented array block capacity");
public:
  static constexpr int64_t DEFAULT_BLOCK_PTR_CAPACITY = 64;
  static constexpr int64_t BLOCK_POWER =
      64 - __builtin_clzll(static_cast<uint64_t>(BLOCK_CAPACITY) - 1);
  static constexpr int64_t REAL_BLOCK_CAPACITY = 1L << BLOCK_POWER;
  static constexpr int64_t BLOCK_MASK = REAL_BLOCK_CAPACITY - 1;

  explicit ObFastSegmentArray(
      ObIAllocator &allocator,
      const int64_t initial_block_ptr_capacity = DEFAULT_BLOCK_PTR_CAPACITY)
      : allocator_(allocator), block_arr_(nullptr), block_arr_capacity_(0),
        block_count_(0), size_(0), constructed_size_(0), initial_block_ptr_capacity_(
            initial_block_ptr_capacity > 0
                ? initial_block_ptr_capacity : DEFAULT_BLOCK_PTR_CAPACITY)
  {}
  ~ObFastSegmentArray() { reset(); }

  int push_back(const T &value)
  {
    int ret = OB_SUCCESS;
    const int64_t block_idx = size_ >> BLOCK_POWER;
    if (OB_FAIL(ensure_block(block_idx))) {
      LOG_WARN("fail to ensure segmented array block", K(ret), K(block_idx));
    } else {
      T *slot = &block_arr_[block_idx][size_ & BLOCK_MASK];
      if (size_ < constructed_size_) {
        *slot = value;
      } else {
        new (slot) T(value);
        ++constructed_size_;
      }
      ++size_;
    }
    return ret;
  }

  int alloc(T *&ptr)
  {
    int ret = OB_SUCCESS;
    const int64_t block_idx = size_ >> BLOCK_POWER;
    if (OB_FAIL(ensure_block(block_idx))) {
      LOG_WARN("fail to ensure segmented array block", K(ret), K(block_idx));
    } else {
      ptr = &block_arr_[block_idx][size_ & BLOCK_MASK];
      if (size_ >= constructed_size_) {
        new (ptr) T();
        ++constructed_size_;
      }
      ++size_;
    }
    return ret;
  }

  int free_an_obj()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(0 == size_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      --size_;
    }
    return ret;
  }

  const T &at(const int64_t idx) const
  {
    return block_arr_[idx >> BLOCK_POWER][idx & BLOCK_MASK];
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
    for (int64_t i = 0; i < constructed_size_; ++i) {
      at(i).~T();
    }
    for (int64_t i = 0; i < block_count_; ++i) {
      if (OB_NOT_NULL(block_arr_[i])) {
        allocator_.free(block_arr_[i]);
        block_arr_[i] = nullptr;
      }
    }
    if (OB_NOT_NULL(block_arr_)) {
      allocator_.free(block_arr_);
      block_arr_ = nullptr;
    }
    block_arr_capacity_ = 0;
    block_count_ = 0;
    size_ = 0;
    constructed_size_ = 0;
  }

private:
  int ensure_block(const int64_t block_idx)
  {
    int ret = OB_SUCCESS;
    if (block_idx >= block_count_) {
      if (block_idx >= block_arr_capacity_
          && OB_FAIL(expand_block_array(block_idx + 1))) {
        LOG_WARN("fail to expand segmented array block pointers", K(ret), K(block_idx));
      } else if (OB_NOT_NULL(block_arr_[block_idx])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected existing segmented array block", K(ret), K(block_idx));
      } else if (OB_ISNULL(block_arr_[block_idx] = static_cast<T *>(
                     allocator_.alloc(REAL_BLOCK_CAPACITY * sizeof(T))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate segmented array block", K(ret), K(block_idx));
      } else {
        ++block_count_;
      }
    }
    return ret;
  }

  int expand_block_array(const int64_t required_capacity)
  {
    int ret = OB_SUCCESS;
    int64_t next_capacity = block_arr_capacity_ > 0
        ? block_arr_capacity_ : initial_block_ptr_capacity_;
    while (next_capacity < required_capacity) {
      next_capacity <<= 1;
    }
    const int64_t bytes = next_capacity * sizeof(T *);
    T **next = static_cast<T **>(allocator_.alloc(bytes));
    if (OB_ISNULL(next)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate segmented array block pointers", K(ret), K(next_capacity));
    } else {
      MEMSET(next, 0, bytes);
      if (OB_NOT_NULL(block_arr_)) {
        MEMCPY(next, block_arr_, block_count_ * sizeof(T *));
        allocator_.free(block_arr_);
      }
      block_arr_ = next;
      block_arr_capacity_ = next_capacity;
    }
    return ret;
  }

private:
  ObIAllocator &allocator_;
  T **block_arr_;
  int64_t block_arr_capacity_;
  int64_t block_count_;
  int64_t size_;
  int64_t constructed_size_;
  const int64_t initial_block_ptr_capacity_;
  DISALLOW_COPY_AND_ASSIGN(ObFastSegmentArray);
};

} // namespace storage
} // namespace oceanbase

#endif // OB_FAST_SEGMENT_ARRAY_H_
