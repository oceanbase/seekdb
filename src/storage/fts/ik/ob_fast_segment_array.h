/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_STORAGE_FTS_IK_OB_FAST_SEGMENT_ARRAY_H_
#define OCEANBASE_STORAGE_FTS_IK_OB_FAST_SEGMENT_ARRAY_H_

#define USING_LOG_PREFIX STORAGE_FTS

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

#include <new>

namespace oceanbase
{
namespace storage
{

// 分块数组由外部 allocator 持有物理内存，适用于可批量回收的轻量对象。
// 块容量向上取为 2 的幂，索引热路径只做移位和掩码运算；对象不能持有需要逐个析构的外部资源。
template <typename T, int64_t block_capacity = 256>
class ObFastSegmentArray
{
  static_assert(block_capacity > 2 && block_capacity <= (1LL << 30),
                "block capacity must be in (2, 1 << 30]");

public:
  static constexpr int64_t BLOCK_POINTER_ARRAY_CAPACITY = 64;
  static constexpr int64_t BLOCK_CAPACITY_POWER =
      64 - __builtin_clzll(static_cast<uint64_t>(block_capacity) - 1);
  static constexpr int64_t REAL_BLOCK_CAPACITY = static_cast<int64_t>(1) << BLOCK_CAPACITY_POWER;
  static constexpr int64_t BLOCK_LOCATOR = REAL_BLOCK_CAPACITY - 1;

  // allocator 生命周期必须覆盖本容器；容器只归还自己申请的块，不拥有 allocator 本身。
  explicit ObFastSegmentArray(
      common::ObIAllocator &allocator,
      const int64_t init_block_arr_cap = BLOCK_POINTER_ARRAY_CAPACITY)
      : allocator_(allocator),
        block_arr_(nullptr),
        block_arr_cap_(0),
        block_count_(0),
        size_(0),
        init_block_arr_cap_(init_block_arr_cap > 0 ? init_block_arr_cap
                                                   : BLOCK_POINTER_ARRAY_CAPACITY)
  {}

  ~ObFastSegmentArray() { reset(); }

  // 追加一个对象；已有块足够时不发生分配和拷贝扩容。
  int push_back(const T &value)
  {
    int ret = OB_SUCCESS;
    const int64_t block_idx = size_ >> BLOCK_CAPACITY_POWER;
    if (OB_FAIL(ensure_block_(block_idx))) {
      LOG_WARN("failed to ensure segment block", K(ret), K(block_idx));
    } else {
      const int64_t inner_idx = size_ & BLOCK_LOCATOR;
      // allocator 返回的是原始存储，必须显式开始对象生命周期后才能写入非平凡 T。
      new (&block_arr_[block_idx][inner_idx]) T(value);
      ++size_;
    }
    return ret;
  }

  // 预留并默认构造一个槽位，调用者可在下一次 reset 前通过返回地址写入。
  int alloc(T *&ptr)
  {
    int ret = OB_SUCCESS;
    const int64_t block_idx = size_ >> BLOCK_CAPACITY_POWER;
    if (OB_FAIL(ensure_block_(block_idx))) {
      LOG_WARN("failed to ensure segment block", K(ret), K(block_idx));
    } else {
      const int64_t inner_idx = size_ & BLOCK_LOCATOR;
      ptr = &block_arr_[block_idx][inner_idx];
      new (ptr) T();
      ++size_;
    }
    return ret;
  }

  // 仅回退最后一个逻辑槽位，不释放物理块，供热路径失败回滚使用。
  int free_an_obj()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(0 == size_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      --size_;
      at(size_).~T();
    }
    return ret;
  }

  const T &at(const int64_t idx) const
  {
    const int64_t block_idx = idx >> BLOCK_CAPACITY_POWER;
    const int64_t inner_idx = idx & BLOCK_LOCATOR;
    return block_arr_[block_idx][inner_idx];
  }

  T &at(const int64_t idx)
  {
    return const_cast<T &>(static_cast<const ObFastSegmentArray &>(*this).at(idx));
  }

  int64_t count() const { return size_; }
  bool empty() const { return 0 == size_; }

  // 文档间复用析构逻辑元素但保留块与地址，避免重复分配且满足非平凡 T 的生命周期。
  void reuse()
  {
    destroy_constructed_objects_();
    size_ = 0;
  }

  // reset 才归还全部块；调用后此前取得的元素地址全部失效。
  void reset()
  {
    destroy_constructed_objects_();
    for (int64_t i = 0; i < block_count_; ++i) {
      if (nullptr != block_arr_[i]) {
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
  void destroy_constructed_objects_()
  {
    for (int64_t i = 0; i < size_; ++i) {
      at(i).~T();
    }
  }

  int ensure_block_(const int64_t block_idx)
  {
    int ret = OB_SUCCESS;
    if (block_idx >= block_count_) {
      if (block_idx >= block_arr_cap_ && OB_FAIL(expand_block_array_(block_idx + 1))) {
        LOG_WARN("failed to expand block pointer array", K(ret), K(block_idx));
      }
      if (OB_SUCC(ret)) {
        if (OB_UNLIKELY(nullptr != block_arr_[block_idx])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("segment block state is inconsistent", K(ret), K(block_idx), K(block_count_));
        } else {
          void *buf = allocator_.alloc(REAL_BLOCK_CAPACITY * static_cast<int64_t>(sizeof(T)));
          if (OB_ISNULL(buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate segment block", K(ret), K(block_idx));
          } else {
            block_arr_[block_idx] = static_cast<T *>(buf);
            ++block_count_;
          }
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
      LOG_WARN("failed to allocate block pointer array", K(ret), K(next_cap));
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
  common::ObIAllocator &allocator_;
  T **block_arr_;
  int64_t block_arr_cap_;
  int64_t block_count_;
  int64_t size_;
  const int64_t init_block_arr_cap_;

  DISALLOW_COPY_AND_ASSIGN(ObFastSegmentArray);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_IK_OB_FAST_SEGMENT_ARRAY_H_
