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

#ifndef OB_FAST_LIST_H_
#define OB_FAST_LIST_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/ob_fast_segment_array.h"

namespace oceanbase
{
namespace storage
{

template <typename T, int64_t SEGMENT_SHIFT = 8>
class ObFastList final
{
public:
  static const int64_t INVALID_IDX = -1;

  struct Node
  {
    T data_;
    int64_t prev_;
    int64_t next_;
    Node() : data_(), prev_(INVALID_IDX), next_(INVALID_IDX) {}
  };

  class Iterator
  {
  public:
    Iterator() : list_(nullptr), cur_(INVALID_IDX) {}
    Iterator(ObFastList *list, int64_t cur) : list_(list), cur_(cur) {}
    T &operator*() { return list_->node_pool_.at(cur_).data_; }
    T *operator->() { return &list_->node_pool_.at(cur_).data_; }
    Iterator &operator++()
    {
      if (cur_ != INVALID_IDX) {
        cur_ = list_->node_pool_.at(cur_).next_;
      }
      return *this;
    }
    bool operator!=(const Iterator &other) const { return cur_ != other.cur_; }
    bool operator==(const Iterator &other) const { return cur_ == other.cur_; }

  private:
    ObFastList *list_;
    int64_t cur_;
  };

  class ConstIterator
  {
  public:
    ConstIterator() : list_(nullptr), cur_(INVALID_IDX) {}
    ConstIterator(const ObFastList *list, int64_t cur) : list_(list), cur_(cur) {}
    const T &operator*() const { return list_->node_pool_.at(cur_).data_; }
    const T *operator->() const { return &list_->node_pool_.at(cur_).data_; }
    ConstIterator &operator++()
    {
      if (cur_ != INVALID_IDX) {
        cur_ = list_->node_pool_.at(cur_).next_;
      }
      return *this;
    }
    bool operator!=(const ConstIterator &other) const { return cur_ != other.cur_; }

  private:
    const ObFastList *list_;
    int64_t cur_;
  };

  ObFastList() : head_(INVALID_IDX), tail_(INVALID_IDX) {}

  ~ObFastList() { destroy(); }

  int init(lib::ObMemAttr attr = lib::ObMemAttr())
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(node_pool_.init(attr))) {
    } else {
      head_ = INVALID_IDX;
      tail_ = INVALID_IDX;
    }
    return ret;
  }

  void destroy()
  {
    node_pool_.destroy();
    head_ = INVALID_IDX;
    tail_ = INVALID_IDX;
  }

  void reuse()
  {
    node_pool_.reuse();
    head_ = INVALID_IDX;
    tail_ = INVALID_IDX;
  }

  void reset()
  {
    destroy();
  }

  OB_INLINE int64_t size() const { return node_pool_.size(); }
  OB_INLINE bool empty() const { return node_pool_.empty(); }

  int push_back(const T &val)
  {
    int ret = OB_SUCCESS;
    int64_t next_idx = node_pool_.size();
    Node n;
    n.data_ = val;
    n.prev_ = tail_;
    n.next_ = INVALID_IDX;
    if (OB_FAIL(node_pool_.push_back(n))) {
    } else if (tail_ != INVALID_IDX) {
      node_pool_.at(tail_).next_ = next_idx;
      tail_ = next_idx;
    } else {
      head_ = next_idx;
      tail_ = next_idx;
    }
    return ret;
  }

  int insert_sorted(const T &val, bool (*cmp)(const T &a, const T &b))
  {
    int ret = OB_SUCCESS;
    int64_t cur = head_;
    int64_t prev_idx = INVALID_IDX;
    while (cur != INVALID_IDX) {
      if (cmp(val, node_pool_.at(cur).data_)) {
        break;
      }
      prev_idx = cur;
      cur = node_pool_.at(cur).next_;
    }
    int64_t new_idx = node_pool_.size();
    Node n;
    n.data_ = val;
    n.prev_ = prev_idx;
    n.next_ = cur;
    if (OB_FAIL(node_pool_.push_back(n))) {
    } else if (prev_idx != INVALID_IDX) {
      node_pool_.at(prev_idx).next_ = new_idx;
    } else {
      head_ = new_idx;
    }
    if (cur != INVALID_IDX) {
      node_pool_.at(cur).prev_ = new_idx;
    } else {
      tail_ = new_idx;
    }
    return ret;
  }

  OB_INLINE T &front() { return node_pool_.at(head_).data_; }
  OB_INLINE T &back() { return node_pool_.at(tail_).data_; }
  OB_INLINE const T &front() const { return node_pool_.at(head_).data_; }

  void pop_front()
  {
    if (head_ != INVALID_IDX) {
      int64_t next = node_pool_.at(head_).next_;
      if (next != INVALID_IDX) {
        node_pool_.at(next).prev_ = INVALID_IDX;
      } else {
        tail_ = INVALID_IDX;
      }
      head_ = next;
    }
  }

  Iterator begin() { return Iterator(this, head_); }
  Iterator end() { return Iterator(this, INVALID_IDX); }
  ConstIterator begin() const { return ConstIterator(this, head_); }
  ConstIterator end() const { return ConstIterator(this, INVALID_IDX); }

  const ObFastSegmentArray<Node, SEGMENT_SHIFT> &get_pool() const { return node_pool_; }

private:
  ObFastSegmentArray<Node, SEGMENT_SHIFT> node_pool_;
  int64_t head_;
  int64_t tail_;
};

} // end namespace storage
} // end namespace oceanbase

#endif // OB_FAST_LIST_H_
