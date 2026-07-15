/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef OB_FAST_LIST_H_
#define OB_FAST_LIST_H_

#include "storage/fts/ik/ob_fast_segment_array.h"

namespace oceanbase
{
namespace storage
{

// Node-pool list for POD-like values. Removed nodes are reclaimed on reuse.
template <typename T, int64_t BLOCK_CAPACITY = 256>
class ObFastList
{
  struct Node
  {
    Node() : next_(nullptr), prev_(nullptr), value_() {}
    explicit Node(const T &value) : next_(nullptr), prev_(nullptr), value_(value) {}
    Node *next_;
    Node *prev_;
    T value_;
  };
  struct Root
  {
    operator Node *() { return reinterpret_cast<Node *>(this); }
    operator const Node *() const { return reinterpret_cast<const Node *>(this); }
    Node *next_;
    Node *prev_;
  };

public:
  class const_iterator;

  class iterator
  {
  public:
    iterator() : node_(nullptr) {}
    explicit iterator(Node *node) : node_(node) {}
    T &operator*() const { return node_->value_; }
    T *operator->() const { return &node_->value_; }
    iterator &operator++() { node_ = node_->next_; return *this; }
    iterator operator++(int) { iterator old(*this); ++(*this); return old; }
    iterator &operator--() { node_ = node_->prev_; return *this; }
    iterator operator--(int) { iterator old(*this); --(*this); return old; }
    bool operator==(const iterator &other) const { return node_ == other.node_; }
    bool operator!=(const iterator &other) const { return node_ != other.node_; }
  private:
    friend class ObFastList;
    friend class const_iterator;
    Node *node_;
  };

  class const_iterator
  {
  public:
    const_iterator() : node_(nullptr) {}
    explicit const_iterator(const Node *node) : node_(const_cast<Node *>(node)) {}
    const_iterator(const iterator &iter) : node_(iter.node_) {}
    const T &operator*() const { return node_->value_; }
    const T *operator->() const { return &node_->value_; }
    const_iterator &operator++() { node_ = node_->next_; return *this; }
    const_iterator operator++(int) { const_iterator old(*this); ++(*this); return old; }
    const_iterator &operator--() { node_ = node_->prev_; return *this; }
    const_iterator operator--(int) { const_iterator old(*this); --(*this); return old; }
    bool operator==(const const_iterator &other) const { return node_ == other.node_; }
    bool operator!=(const const_iterator &other) const { return node_ != other.node_; }
  private:
    friend class ObFastList;
    Node *node_;
  };

  explicit ObFastList(ObIAllocator &allocator) : pool_(allocator), size_(0)
  {
    root_.next_ = root_;
    root_.prev_ = root_;
  }
  ~ObFastList() { reset(); }

  void reset() { reuse(); pool_.reset(); }
  void reuse()
  {
    root_.next_ = root_;
    root_.prev_ = root_;
    size_ = 0;
    pool_.reuse();
  }
  bool empty() const { return 0 == size_; }
  int64_t size() const { return size_; }
  T &get_first() { return root_.next_->value_; }
  const T &get_first() const { return root_.next_->value_; }
  T &get_last() { return root_.prev_->value_; }
  const T &get_last() const { return root_.prev_->value_; }
  iterator begin() { return iterator(root_.next_); }
  iterator end() { return iterator(root_); }
  const_iterator begin() const { return const_iterator(root_.next_); }
  const_iterator end() const { return const_iterator(root_); }
  iterator last() { return iterator(root_.prev_); }
  const_iterator last() const { return const_iterator(root_.prev_); }
  int push_front(const T &value) { return insert_before(root_.next_, value); }
  int push_back(const T &value) { return insert_before(root_, value); }
  int insert(iterator pos, const T &value) { return insert_before(pos.node_, value); }
  int insert(const_iterator pos, const T &value) { return insert_before(pos.node_, value); }
  int pop_front()
  {
    int ret = OB_SUCCESS;
    if (empty()) { ret = OB_ENTRY_NOT_EXIST; } else { remove(root_.next_); }
    return ret;
  }
  int pop_back()
  {
    int ret = OB_SUCCESS;
    if (empty()) { ret = OB_ENTRY_NOT_EXIST; } else { remove(root_.prev_); }
    return ret;
  }

private:
  int insert_before(Node *anchor, const T &value)
  {
    int ret = OB_SUCCESS;
    Node node_value(value);
    if (OB_FAIL(pool_.push_back(node_value))) {
      LOG_WARN("fail to allocate fast-list node", K(ret));
    } else {
      Node *node = &pool_.at(pool_.count() - 1);
      node->next_ = anchor;
      node->prev_ = anchor->prev_;
      anchor->prev_->next_ = node;
      anchor->prev_ = node;
      ++size_;
    }
    return ret;
  }
  void remove(Node *node)
  {
    node->prev_->next_ = node->next_;
    node->next_->prev_ = node->prev_;
    node->next_ = nullptr;
    node->prev_ = nullptr;
    --size_;
  }

private:
  ObFastSegmentArray<Node, BLOCK_CAPACITY> pool_;
  Root root_;
  int64_t size_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_FAST_LIST_H_
