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

#pragma once

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace storage
{
// Chunked reuse array (official ObFastSegmentArray idea): elements live in
// blocks of power-of-two capacity located by a bit shift, so indexing never
// walks a chain. reset() keeps the blocks allocated so a pooled parser
// reuses them across calls; release_blocks() forgets them and must be called
// before the backing scratch arena recycles its pages. Element order is the
// insertion order, identical to the ObList it replaces.
// BLOCK_SHIFT=2 (4-element blocks): the dominant user is the smart
// arbitrator's short token chains (1-3 tokens each); a 4-element block keeps
// the per-chain first-touch footprint at 160B instead of 1280B (measured:
// 32-element blocks made the arbitrator arena churn ~64KB/call vs ~6KB with
// per-node ObList). Long sequences (token_list_, result queue) simply span
// more blocks; the block table grows amortized.
template <typename T, int64_t BLOCK_SHIFT = 2>
class ObFTSegVec
{
public:
  class iterator
  {
  public:
    explicit iterator(ObFTSegVec *vec, int64_t idx) : vec_(vec), idx_(idx) {}
    iterator(const iterator &) = default;
    iterator &operator=(const iterator &) = default;
    bool operator!=(const iterator &other) const { return idx_ != other.idx_ || vec_ != other.vec_; }
    bool operator==(const iterator &other) const { return idx_ == other.idx_ && vec_ == other.vec_; }
    iterator &operator++() { ++idx_; return *this; }
    iterator operator++(int) { iterator tmp = *this; ++idx_; return tmp; }
    iterator &operator--() { --idx_; return *this; }
    iterator operator--(int) { iterator tmp = *this; --idx_; return tmp; }
    T &operator*() const { return vec_->at(idx_); }
    int64_t index() const { return idx_; }
  private:
    ObFTSegVec *vec_;
    int64_t idx_;
  };

  explicit ObFTSegVec(ObIAllocator &alloc)
      : alloc_(alloc), blocks_(nullptr), block_cnt_(0), block_cap_(0), size_(0)
  {
    static_assert(BLOCK_SHIFT >= 1 && BLOCK_SHIFT <= 12, "block size must be a sane power of two");
  }
  ~ObFTSegVec() { release_blocks(); }

  // drop every element; keep the allocated blocks for the next round
  void reset() { size_ = 0; }
  // forget the blocks: call before the scratch arena is reset/recycled
  void release_blocks()
  {
    for (int64_t i = 0; i < block_cnt_; ++i) {
      alloc_.free(blocks_[i]);
      blocks_[i] = nullptr;
    }
    if (OB_NOT_NULL(blocks_)) {
      alloc_.free(blocks_);
      blocks_ = nullptr;
    }
    block_cnt_ = 0;
    block_cap_ = 0;
    size_ = 0;
  }

  int push_back(const T &value)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ensure_capacity(size_ + 1))) {
    } else {
      T *slot = &block_at(size_ >> BLOCK_SHIFT)[size_ & BLOCK_MASK];
      // copy-construct (not assign): a fresh block slot has never been
      // constructed, and T's operator= may deliberately skip fields (e.g.
      // ObDATrieHit keeps its dict_ untouched) - only the constructor sets
      // every field.
      new (static_cast<void *>(slot)) T(value);
      ++size_;
    }
    return ret;
  }

  int push_front(const T &value)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ensure_capacity(size_ + 1))) {
    } else if (size_ > 0) {
      // construct the new tail slot first, then shift the constructed
      // elements up (full-field move, never operator=), then write the head
      new (static_cast<void *>(&at(size_))) T(at(size_ - 1));
      for (int64_t i = size_ - 1; i > 0; --i) {
        move_slot(i, i - 1);
      }
      move_write(0, value);
      ++size_;
    } else {
      new (static_cast<void *>(&at(0))) T(value);
      ++size_;
    }
    return ret;
  }

  int pop_front()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(0 >= size_)) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      for (int64_t i = 1; i < size_; ++i) {
        move_slot(i - 1, i);
      }
      --size_;
    }
    return ret;
  }

  int pop_back()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(0 >= size_)) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      --size_;
    }
    return ret;
  }

  // erase the element at idx keeping the order of the rest
  int erase_at(int64_t idx)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(idx < 0 || idx >= size_)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = idx + 1; i < size_; ++i) {
        move_slot(i - 1, i);
      }
      --size_;
    }
    return ret;
  }

  // insert value before the element currently at idx (shift up)
  int insert_at(int64_t idx, const T &value)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(idx < 0 || idx > size_)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(ensure_capacity(size_ + 1))) {
    } else if (0 == size_) {
      new (static_cast<void *>(&at(0))) T(value);
      ++size_;
    } else {
      // construct the new tail slot as a copy of the last element, shift the
      // constructed elements up (full-field move), then write idx
      new (static_cast<void *>(&at(size_))) T(at(size_ - 1));
      for (int64_t i = size_ - 1; i > idx; --i) {
        move_slot(i, i - 1);
      }
      move_write(idx, value);
      ++size_;
    }
    return ret;
  }

  T &at(int64_t idx) { return block_at(idx >> BLOCK_SHIFT)[idx & BLOCK_MASK]; }
  const T &at(int64_t idx) const
  {
    return const_cast<ObFTSegVec *>(this)->at(idx);
  }
  T &get_first() { return at(0); }
  const T &get_first() const { return at(0); }
  T &get_last() { return at(size_ - 1); }
  const T &get_last() const { return at(size_ - 1); }

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size_); }
  iterator last() { return iterator(this, size_ - 1); }
  // read-only traversal from const contexts (p_weight/x_weight style loops);
  // iterator::operator* goes through at() which respects constness of use
  iterator begin() const { return iterator(const_cast<ObFTSegVec *>(this), 0); }
  iterator end() const { return iterator(const_cast<ObFTSegVec *>(this), size_); }

  bool empty() const { return 0 == size_; }
  int64_t size() const { return size_; }

private:
  static constexpr int64_t BLOCK_ELEMS = (static_cast<int64_t>(1) << BLOCK_SHIFT);
  static constexpr int64_t BLOCK_MASK = BLOCK_ELEMS - 1;

  // Full-field relocation of a constructed element: destroy the destination,
  // then copy-construct from the source. T's operator= must NOT be used here:
  // some element types (ObDATrieHit) deliberately skip fields in operator=
  // (their dict_ is invariant within one element lifetime, not across
  // different elements), which would silently corrupt a shifted element.
  void move_slot(int64_t dst, int64_t src)
  {
    T &d = at(dst);
    d.~T();
    new (static_cast<void *>(&d)) T(at(src));
  }
  // write `value` into an already-constructed slot with full-field semantics
  void move_write(int64_t dst, const T &value)
  {
    T &d = at(dst);
    d.~T();
    new (static_cast<void *>(&d)) T(value);
  }

  int ensure_capacity(int64_t need)
  {
    int ret = OB_SUCCESS;
    if (need <= BLOCK_ELEMS * block_cnt_) {
      // fast path: current blocks already hold it
    } else if (OB_UNLIKELY(need > BLOCK_ELEMS * block_cap_)) {
      // grow the block table
      int64_t new_cap = (0 == block_cap_) ? 8 : block_cap_ * 2;
      while (need > BLOCK_ELEMS * new_cap) {
        new_cap *= 2;
      }
      T **new_blocks = static_cast<T **>(alloc_.alloc(sizeof(T *) * new_cap));
      if (OB_ISNULL(new_blocks)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_FTS_LOG(WARN, "alloc block table failed");
      } else {
        if (block_cnt_ > 0) {
          MEMCPY(new_blocks, blocks_, sizeof(T *) * block_cnt_);
        }
        if (OB_NOT_NULL(blocks_)) {
          alloc_.free(blocks_);
        }
        blocks_ = new_blocks;
        block_cap_ = new_cap;
      }
    }
    if (OB_SUCC(ret)) {
      // grow blocks until need fits
      while (OB_SUCC(ret) && need > BLOCK_ELEMS * block_cnt_) {
        T *blk = static_cast<T *>(alloc_.alloc(sizeof(T) * BLOCK_ELEMS));
        if (OB_ISNULL(blk)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          STORAGE_FTS_LOG(WARN, "alloc seg block failed");
        } else {
          blocks_[block_cnt_++] = blk;
        }
      }
    }
    return ret;
  }

  T *block_at(int64_t block_idx)
  {
    if (OB_UNLIKELY(block_idx >= block_cnt_)) {
      // ensure_capacity already covered this index on the inserting path;
      // direct at() beyond that is a caller bug - stay safe anyway.
      abort();
    }
    return blocks_[block_idx];
  }

  ObIAllocator &alloc_;
  T **blocks_;
  int64_t block_cnt_;
  int64_t block_cap_;
  int64_t size_;

  DISALLOW_COPY_AND_ASSIGN(ObFTSegVec);
};

// FIFO queue over a reuse array: pop_front only advances a head cursor, so
// the compound()/get_next_token() pattern costs no memmove. Order preserved.
template <typename T>
class ObFTTokenQueue
{
public:
  explicit ObFTTokenQueue(ObIAllocator &alloc) : vec_(alloc), head_(0) {}

  void reset()
  {
    vec_.reset();
    head_ = 0;
  }
  void release_blocks()
  {
    vec_.release_blocks();
    head_ = 0;
  }

  int push_back(const T &value) { return vec_.push_back(value); }
  int pop_front()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(head_ >= vec_.size())) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      ++head_;
      if (head_ >= vec_.size()) {
        // fully drained: restart from zero so the storage is reused
        vec_.reset();
        head_ = 0;
      }
    }
    return ret;
  }
  // pre-condition: !empty() (same contract as ObList::get_first(); the only
  // caller, TokenizeContext::get_next_token/compound, guards with empty())
  T &get_first() { return vec_.at(head_); }
  bool empty() const { return head_ >= vec_.size(); }
  int64_t size() const { return vec_.size() - head_; }

private:
  ObFTSegVec<T> vec_;
  int64_t head_;

  DISALLOW_COPY_AND_ASSIGN(ObFTTokenQueue);
};

} // namespace storage
} // namespace oceanbase
