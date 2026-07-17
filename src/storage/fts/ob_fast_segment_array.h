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

#ifndef OB_FAST_SEGMENT_ARRAY_H_
#define OB_FAST_SEGMENT_ARRAY_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "common/ob_member.h"

namespace oceanbase
{
namespace storage
{

template <typename T, int64_t SEGMENT_SHIFT = 8>
class ObFastSegmentArray final
{
public:
  static const int64_t SEGMENT_SIZE = 1LL << SEGMENT_SHIFT;
  static const int64_t SEGMENT_MASK = SEGMENT_SIZE - 1;

  struct Segment
  {
    T *data_;
    Segment() : data_(nullptr) {}
    ~Segment()
    {
      if (data_ != nullptr) {
        ob_free(data_);
        data_ = nullptr;
      }
    }
    DISALLOW_COPY_AND_ASSIGN(Segment);
  };

  ObFastSegmentArray() : count_(0), segment_capacity_(0), segments_(nullptr) {}

  ~ObFastSegmentArray() { destroy(); }

  int init(lib::ObMemAttr attr = lib::ObMemAttr())
  {
    int ret = OB_SUCCESS;
    attr_ = attr;
    count_ = 0;
    segment_capacity_ = 0;
    segments_ = nullptr;
    return ret;
  }

  void destroy()
  {
    if (segments_ != nullptr) {
      for (int64_t i = 0; i < segment_capacity_; ++i) {
        segments_[i].~Segment();
      }
      ob_free(segments_);
      segments_ = nullptr;
    }
    count_ = 0;
    segment_capacity_ = 0;
  }

  void reuse()
  {
    count_ = 0;
  }

  void reset()
  {
    destroy();
  }

  int push_back(const T &elem)
  {
    int ret = OB_SUCCESS;
    int64_t seg_idx = count_ >> SEGMENT_SHIFT;
    int64_t idx_in_seg = count_ & SEGMENT_MASK;
    if (seg_idx >= segment_capacity_) {
      if (OB_FAIL(expand_segments(seg_idx + 1))) {
      } else {
        segments_[seg_idx].data_[idx_in_seg] = elem;
        ++count_;
      }
    } else {
      segments_[seg_idx].data_[idx_in_seg] = elem;
      ++count_;
    }
    return ret;
  }

  int push_back(T &&elem)
  {
    int ret = OB_SUCCESS;
    int64_t seg_idx = count_ >> SEGMENT_SHIFT;
    int64_t idx_in_seg = count_ & SEGMENT_MASK;
    if (seg_idx >= segment_capacity_) {
      if (OB_FAIL(expand_segments(seg_idx + 1))) {
      } else {
        segments_[seg_idx].data_[idx_in_seg] = elem;
        ++count_;
      }
    } else {
      segments_[seg_idx].data_[idx_in_seg] = elem;
      ++count_;
    }
    return ret;
  }

  OB_INLINE const T &at(const int64_t idx) const
  {
    return segments_[idx >> SEGMENT_SHIFT].data_[idx & SEGMENT_MASK];
  }

  OB_INLINE T &at(const int64_t idx)
  {
    return segments_[idx >> SEGMENT_SHIFT].data_[idx & SEGMENT_MASK];
  }

  OB_INLINE const T &operator[](const int64_t idx) const { return at(idx); }
  OB_INLINE T &operator[](const int64_t idx) { return at(idx); }
  OB_INLINE int64_t size() const { return count_; }
  OB_INLINE int64_t count() const { return count_; }
  OB_INLINE bool empty() const { return 0 == count_; }

private:
  int expand_segments(const int64_t required)
  {
    int ret = OB_SUCCESS;
    if (required <= segment_capacity_) {
    } else {
      int64_t new_capacity = segment_capacity_ > 0 ? segment_capacity_ * 2 : 8;
      while (new_capacity < required) {
        new_capacity *= 2;
      }
      int64_t alloc_size = sizeof(Segment) * new_capacity;
      Segment *new_segs = static_cast<Segment *>(ob_malloc(alloc_size, attr_));
      if (OB_ISNULL(new_segs)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMSET(new_segs, 0, alloc_size);
        for (int64_t i = 0; OB_SUCC(ret) && i < segment_capacity_; ++i) {
          new_segs[i] = segments_[i];
        }
        if (segments_ != nullptr) {
          ob_free(segments_);
        }
        segments_ = new_segs;
        for (int64_t i = segment_capacity_; OB_SUCC(ret) && i < new_capacity; ++i) {
          int64_t data_alloc = sizeof(T) * SEGMENT_SIZE;
          segments_[i].data_ = static_cast<T *>(ob_malloc(data_alloc, attr_));
          if (OB_ISNULL(segments_[i].data_)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            MEMSET(segments_[i].data_, 0, data_alloc);
          }
        }
        if (OB_SUCC(ret)) {
          segment_capacity_ = new_capacity;
        }
      }
    }
    return ret;
  }

  int64_t count_;
  int64_t segment_capacity_;
  Segment *segments_;
  lib::ObMemAttr attr_;
};

} // end namespace storage
} // end namespace oceanbase

#endif // OB_FAST_SEGMENT_ARRAY_H_
