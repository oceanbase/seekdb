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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ik/ob_ik_token.h"

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{
int ObIKCandidateBuffer::add_token(const ObIKToken &token)
{
  int ret = OB_SUCCESS;
  Node *insert_after = tail_;
  Node *node = nullptr;

  while (OB_NOT_NULL(insert_after) && token < insert_after->token_) {
    insert_after = insert_after->prev_;
  }
  if (OB_NOT_NULL(insert_after) && insert_after->token_ == token) {
    // Duplicate token: keep the first copy, matching ObFTSortList.
  } else if (OB_FAIL(alloc_node(token, node))) {
    LOG_WARN("failed to allocate IK candidate token", K(ret));
  } else {
    link_after(insert_after, node);
  }
  return ret;
}

int ObIKCandidateBuffer::alloc_node(const ObIKToken &token, Node *&node)
{
  int ret = OB_SUCCESS;
  if (inline_used_ < INLINE_TOKEN_COUNT) {
    node = &inline_nodes_[inline_used_++];
  } else {
    void *buf = allocator_.alloc(sizeof(Node));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to grow IK candidate token pool", K(ret));
    } else {
      node = new (buf) Node();
    }
  }
  if (OB_SUCC(ret)) {
    node->prev_ = nullptr;
    node->next_ = nullptr;
    node->token_ = token;
  }
  return ret;
}

void ObIKCandidateBuffer::link_after(Node *pos, Node *node)
{
  if (OB_ISNULL(pos)) {
    node->next_ = head_;
    if (OB_NOT_NULL(head_)) {
      head_->prev_ = node;
    } else {
      tail_ = node;
    }
    head_ = node;
  } else {
    node->prev_ = pos;
    node->next_ = pos->next_;
    if (OB_NOT_NULL(pos->next_)) {
      pos->next_->prev_ = node;
    } else {
      tail_ = node;
    }
    pos->next_ = node;
  }
  ++size_;
}

int ObIKCandidateBuffer::pop_front()
{
  int ret = OB_SUCCESS;
  if (empty()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    head_ = head_->next_;
    if (OB_NOT_NULL(head_)) {
      head_->prev_ = nullptr;
    } else {
      tail_ = nullptr;
    }
    --size_;
  }
  return ret;
}

int ObFTSortList::add_token(const ObIKToken &token)
{
  int ret = OB_SUCCESS;

  if (tokens_.empty()) {
    if (OB_FAIL(tokens_.push_back(token))) {
      LOG_WARN("Failed to push back token", K(ret));
    }
  } else if (tokens_.get_last() == token) {
    // pass
  } else if (token > tokens_.get_last()) {
    if (OB_FAIL(tokens_.push_back(token))) {
      LOG_WARN("fail to push back token", K(ret));
    }
  } else if (token < tokens_.get_first()) {
    if (OB_FAIL(tokens_.push_front(token))) {
      LOG_WARN("fail to push back token", K(ret));
    }
  } else {
    for (ObFTSortList::CellIter iter = tokens_.last(); OB_SUCC(ret) && iter != tokens_.end();
         --iter) {
      if (token < *iter) {
        continue;
      }
      if (*iter == token) {
        // no need to add again
      } else if (OB_FAIL(tokens_.insert(++iter, token))) { // NOTE: insert after iter
        LOG_WARN("fail to insert token", K(ret));
      } else {
        // insert ok
      }
      break;
    }
  }

  return ret;
}

int ObIKTokenChain::add_token_if_conflict(const ObIKToken &token, bool &added)
{
  int ret = OB_SUCCESS;
  added = true;
  if (list_.is_empty()) {
    list_.add_token(token);
    min_offset_ = token.offset_;
    max_offset_ = token.offset_ + token.length_;
    payload_ = token.length_;
  } else if (check_conflict(token)) {
    list_.add_token(token);
    if (token.offset_ + token.length_ > max_offset_) {
      max_offset_ = token.offset_ + token.length_;
    }
    payload_ = max_offset_ - min_offset_;
  } else {
    added = false;
  }
  return ret;
}

int ObIKTokenChain::add_token_if_no_conflict(const ObIKToken &token, bool &added)
{
  int ret = OB_SUCCESS;
  added = true;
  if (list_.is_empty()) {
    list_.add_token(token);
    min_offset_ = token.offset_;
    max_offset_ = token.offset_ + token.length_;
    payload_ = token.length_;
  } else if (check_conflict(token)) {
    added = false;
  } else {
    list_.add_token(token);
    min_offset_ = list_.min();
    max_offset_ = list_.max();
    payload_ += token.length_;
    added = true;
  }
  return ret;
}

bool ObIKTokenChain::check_conflict(const ObIKToken &token)
{
  if (list_.is_empty()) {
    return false;
  }
  return (token.offset_ >= min_offset_ && token.offset_ < max_offset_)
         || (min_offset_ >= token.offset_ && min_offset_ < token.offset_ + token.length_);
}

bool ObIKTokenChain::better_than(const ObIKTokenChain &other) const
{
  if (this == &other) {
    return false;
  }
  // more valid text length
  if (payload_ != other.payload_) {
    return payload_ > other.payload_;
  }

  // less words
  int64_t token_count = list_.tokens().size();
  int64_t other_token_count = other.list_.tokens().size();
  if (token_count != other_token_count) {
    return token_count < other_token_count;
  }

  // more distance
  if (offset_len() != other.offset_len()) {
    return offset_len() > other.offset_len();
  }

  // inverse cut is better
  if (max_offset_ != other.max_offset_) {
    return max_offset_ > other.max_offset_;
  }

  // more average distance is better
  int64_t this_x_weight = x_weight();
  int64_t other_x_weight = other.x_weight();
  if (this_x_weight != other_x_weight) {
    return this_x_weight > other_x_weight;
  }

  // position
  int64_t this_p_weight = p_weight();
  int64_t other_p_weight = other.p_weight();
  return this_p_weight > other_p_weight;
}

int ObIKTokenChain::copy(ObIKTokenChain *other)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(other)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("other is null", K(ret));
  } else {
    min_offset_ = other->min_offset_;
    max_offset_ = other->max_offset_;
    payload_ = other->payload_;
    ObFTSortList::CellIter iter = other->list().tokens().begin();

    for (; OB_SUCC(ret) && iter != other->list().tokens().end(); ++iter) {
      bool added = false;
      if (OB_FAIL(add_token_if_no_conflict(*iter, added))) {
        LOG_WARN("fail to add token", K(ret));
      }
    }
  }
  return ret;
}

int64_t ObIKTokenChain::p_weight() const
{
  int64_t p_weight = 0;
  int p = 0;
  for (const ObIKToken &token : list_.tokens()) {
    p++;
    p_weight += p * token.char_cnt_;
  }
  return p_weight;
}
int64_t ObIKTokenChain::x_weight() const
{
  int64_t product = 1;
  for (const ObIKToken &token : list_.tokens()) {
    product *= token.char_cnt_;
  }
  return product;
}

int ObIKTokenChain::pop_back(ObIKToken &token)
{
  int ret = OB_SUCCESS;
  if (list_.tokens().empty()) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("Token list is empty", K(ret));
  } else if (FALSE_IT(token = list_.tokens().get_last())) {
  } else if (OB_FAIL(list_.tokens().pop_back())) {
    LOG_WARN("Pop back failed", K(ret));
  } else if (list_.tokens().empty()) {
    min_offset_ = -1;
    max_offset_ = -1;
    payload_ = 0;
  } else {
    payload_ -= token.length_;
    ObIKToken new_last = list_.tokens().get_last();
    // only for not conflict case
    max_offset_ = new_last.offset_ + new_last.length_;
  }
  return ret;
}

int64_t ObFTSortList::max()
{
  if (tokens_.empty()) {
    return 0;
  }
  return tokens_.get_last().offset_ + tokens_.get_last().length_;
}
int64_t ObFTSortList::min()
{
  if (tokens_.empty()) {
    return 0;
  }
  return tokens_.get_first().offset_;
}
} //  namespace storage
} //  namespace oceanbase
