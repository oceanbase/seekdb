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

#ifndef OCEANBASE_STORAGE_FTS_IK_OB_FAST_LIST_H_
#define OCEANBASE_STORAGE_FTS_IK_OB_FAST_LIST_H_

#include "storage/fts/ik/ob_fast_segment_array.h"

namespace oceanbase
{
namespace storage
{

// 使用分块数组作为节点池的双向链表，保留 IK 排序插入语义并消除逐节点分配。
// T 必须是可批量回收的轻量对象；reuse 不逐个析构元素，上一文档结果必须先停止被引用。
template <typename T, int64_t block_capacity = 256>
class ObFastList
{
  struct Node
  {
    Node() : next(nullptr), prev(nullptr), value() {}
    explicit Node(const T &input) : next(nullptr), prev(nullptr), value(input) {}
    Node *next;
    Node *prev;
    T value;
  };

  struct NodeHolder
  {
    operator Node *() { return reinterpret_cast<Node *>(this); }
    operator const Node *() const { return reinterpret_cast<const Node *>(this); }
    Node *next;
    Node *prev;
  };

public:
  class iterator
  {
  public:
    iterator() : node_(nullptr) {}
    explicit iterator(Node *node) : node_(node) {}
    T &operator*() const { return node_->value; }
    T *operator->() const { return &node_->value; }
    iterator &operator++() { node_ = nullptr == node_ ? nullptr : node_->next; return *this; }
    iterator operator++(int) { iterator tmp(*this); ++(*this); return tmp; }
    iterator &operator--() { node_ = nullptr == node_ ? nullptr : node_->prev; return *this; }
    iterator operator--(int) { iterator tmp(*this); --(*this); return tmp; }
    bool operator==(const iterator &other) const { return node_ == other.node_; }
    bool operator!=(const iterator &other) const { return node_ != other.node_; }
  private:
    friend class ObFastList;
    Node *node_;
  };

  class const_iterator
  {
  public:
    const_iterator() : node_(nullptr) {}
    explicit const_iterator(const Node *node) : node_(const_cast<Node *>(node)) {}
    const_iterator(const iterator &iter) : node_(iter.node_) {}
    const T &operator*() const { return node_->value; }
    const T *operator->() const { return &node_->value; }
    const_iterator &operator++() { node_ = nullptr == node_ ? nullptr : node_->next; return *this; }
    const_iterator operator++(int) { const_iterator tmp(*this); ++(*this); return tmp; }
    const_iterator &operator--() { node_ = nullptr == node_ ? nullptr : node_->prev; return *this; }
    const_iterator operator--(int) { const_iterator tmp(*this); --(*this); return tmp; }
    bool operator==(const const_iterator &other) const { return node_ == other.node_; }
    bool operator!=(const const_iterator &other) const { return node_ != other.node_; }
  private:
    friend class ObFastList;
    Node *node_;
  };

public:
  // allocator 生命周期必须覆盖链表；节点内存由 pool_ 批量申请和释放。
  explicit ObFastList(common::ObIAllocator &allocator) : pool_(allocator), size_(0)
  {
    root_.next = root_;
    root_.prev = root_;
  }

  ~ObFastList() { reset(); }

  // 释放全部节点块，调用后此前的迭代器和元素地址均失效。
  void reset()
  {
    clear_();
    pool_.reset();
  }

  // 文档间只重置链关系和节点池逻辑长度，下一次插入会复用原节点地址。
  void reuse()
  {
    root_.next = root_;
    root_.prev = root_;
    size_ = 0;
    pool_.reuse();
  }

  bool empty() const { return 0 == size_; }
  int64_t size() const { return size_; }
  T &get_first() { return root_.next->value; }
  const T &get_first() const { return root_.next->value; }
  T &get_last() { return root_.prev->value; }
  const T &get_last() const { return root_.prev->value; }
  iterator begin() { return iterator(root_.next); }
  iterator end() { return iterator(root_); }
  const_iterator begin() const { return const_iterator(root_.next); }
  const_iterator end() const { return const_iterator(root_); }
  iterator last() { return iterator(root_.prev); }
  const_iterator last() const { return const_iterator(root_.prev); }
  int push_front(const T &value) { return insert_before_(root_.next, value); }
  int push_back(const T &value) { return insert_before_(root_, value); }
  int insert(iterator pos, const T &value) { return insert_before_(pos.node_, value); }
  int insert(const_iterator pos, const T &value) { return insert_before_(pos.node_, value); }

  int pop_front()
  {
    int ret = OB_SUCCESS;
    if (empty()) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      remove_node_(root_.next);
    }
    return ret;
  }

  int pop_back()
  {
    int ret = OB_SUCCESS;
    if (empty()) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      remove_node_(root_.prev);
    }
    return ret;
  }

private:
  void clear_()
  {
    Node *cur = root_.next;
    while (cur != root_) {
      Node *next = cur->next;
      destroy_node_(cur);
      cur = next;
    }
    root_.next = root_;
    root_.prev = root_;
    size_ = 0;
  }

  int insert_before_(Node *pos, const T &value)
  {
    int ret = OB_SUCCESS;
    Node *node = nullptr;
    if (OB_FAIL(alloc_node_(node, value))) {
    } else {
      Node *anchor = nullptr == pos ? root_ : pos;
      node->next = anchor;
      node->prev = anchor->prev;
      anchor->prev->next = node;
      anchor->prev = node;
      ++size_;
    }
    return ret;
  }

  int alloc_node_(Node *&node, const T &value)
  {
    int ret = OB_SUCCESS;
    Node dummy(value);
    if (OB_FAIL(pool_.push_back(dummy))) {
    } else {
      node = &pool_.at(pool_.count() - 1);
    }
    return ret;
  }

  void remove_node_(Node *node)
  {
    if (nullptr != node && node != root_) {
      node->prev->next = node->next;
      node->next->prev = node->prev;
      destroy_node_(node);
      --size_;
    }
  }

  void destroy_node_(Node *node) { node->next = node->prev = nullptr; }

private:
  ObFastSegmentArray<Node, block_capacity> pool_;
  NodeHolder root_;
  int64_t size_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_IK_OB_FAST_LIST_H_
