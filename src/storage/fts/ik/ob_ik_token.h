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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_IK_TOKEN_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_IK_TOKEN_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/list/ob_list.h"
namespace oceanbase
{
namespace storage
{
enum class ObIKTokenType : int8_t
{
  IK_CHINESE_TOKEN = 0,
  IK_ENGLISH_TOKEN = 1,
  IK_NUMBER_TOKEN = 2,
  IK_ARABIC_TOKEN = 3,
  IK_MIX_TOKEN = 4,
  IK_CNNUM_TOKEN = 5,
  IK_COUNT_TOKEN = 6,
  IK_CNQUAN_TOKEN = 7,
  IK_OTHER_CJK_TOKEN = 8,
  IK_SURROGATE_TOKEN = 9,
};

/** class ObIKToken:
 * @brief Token of the fulltext index.
 * It contains the start position and length of the word.
 * It holds the pointer to the original string.
 * Todo(@xinglipeng.xlp): maybe the pointer show be removed or moved to cursor
 */
struct ObIKToken
{
public:
  // current ptr is pointed to the fulltext();
  const char *ptr_;
  int64_t offset_;
  int64_t length_;
  int64_t char_cnt_;
  ObIKTokenType type_;

public:
  ~ObIKToken() {}
  OB_INLINE bool operator==(const ObIKToken &token) const
  {
    return (offset_ == token.offset_ && length_ == token.length_);
  }

  OB_INLINE bool operator>(const ObIKToken &token) const
  {
    return offset_ > token.offset_ || (offset_ == token.offset_ && length_ < token.length_);
  }

  OB_INLINE bool operator<(const ObIKToken &token) const
  {
    return offset_ < token.offset_ || (offset_ == token.offset_ && length_ > token.length_);
  }
  TO_STRING_KV(K_(offset), K_(length), K_(char_cnt), K_(type));
};

// Candidate tokens are short-lived and overwhelmingly fit in one document-sized
// inline buffer.  Keeping them contiguous avoids allocating one ObList node per
// token while preserving the original sorted/deduplicated semantics.
class ObIKCandidateBuffer
{
public:
  static constexpr int64_t INLINE_TOKEN_COUNT = 128;

  explicit ObIKCandidateBuffer(ObIAllocator &allocator)
      : allocator_(allocator), inline_used_(0), head_(nullptr), tail_(nullptr), size_(0)
  {}

  int add_token(const ObIKToken &token);
  bool empty() const { return 0 == size_; }
  bool is_empty() const { return empty(); }
  int64_t size() const { return size_; }
  ObIKToken &get_first() { return head_->token_; }
  const ObIKToken &get_first() const { return head_->token_; }
  ObIKToken &get_last() { return tail_->token_; }
  const ObIKToken &get_last() const { return tail_->token_; }
  int pop_front();
  void reset()
  {
    inline_used_ = 0;
    head_ = nullptr;
    tail_ = nullptr;
    size_ = 0;
  }
  ObIKCandidateBuffer &tokens() { return *this; }
  const ObIKCandidateBuffer &tokens() const { return *this; }

private:
  struct Node
  {
    Node() : prev_(nullptr), next_(nullptr), token_() {}
    Node *prev_;
    Node *next_;
    ObIKToken token_;
  };

  int alloc_node(const ObIKToken &token, Node *&node);
  void link_after(Node *pos, Node *node);

  ObIAllocator &allocator_;
  Node inline_nodes_[INLINE_TOKEN_COUNT];
  int64_t inline_used_;
  Node *head_;
  Node *tail_;
  int64_t size_;
};

// Output tokens only need FIFO access.  A logical head makes pop_front O(1)
// and allows the backing allocation to be retained across parser reuse.
class ObIKResultBuffer
{
public:
  static constexpr int64_t INLINE_TOKEN_COUNT = 128;
  typedef ObSEArray<ObIKToken, INLINE_TOKEN_COUNT, ObWrapperAllocator, false> TokenArray;

  explicit ObIKResultBuffer(ObIAllocator &allocator)
      : tokens_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)), head_(0)
  {}

  int push_back(const ObIKToken &token) { return tokens_.push_back(token); }
  bool empty() const { return head_ >= tokens_.count(); }
  int64_t size() const { return tokens_.count() - head_; }
  ObIKToken &get_first() { return tokens_.at(head_); }
  const ObIKToken &get_first() const { return tokens_.at(head_); }
  int pop_front()
  {
    int ret = OB_SUCCESS;
    if (empty()) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      ++head_;
    }
    return ret;
  }
  void reset()
  {
    tokens_.reuse();
    head_ = 0;
  }

private:
  TokenArray tokens_;
  int64_t head_;
};

class ObFTSortList
{
public:
  ObFTSortList(ObIAllocator &alloc) : tokens_(alloc) {}
  ~ObFTSortList() { tokens_.reset(); }

  int add_token(const ObIKToken &token);

  bool is_empty() const { return tokens_.empty(); }

  void reset() { tokens_.reset(); }

  int64_t min();

  int64_t max();

  ObList<ObIKToken, ObIAllocator> &tokens() { return tokens_; }
  const ObList<ObIKToken, ObIAllocator> &tokens() const { return tokens_; }

public:
  typedef ObList<ObIKToken, ObIAllocator>::iterator CellIter;
  typedef ObList<ObIKToken, ObIAllocator>::const_iterator ConstCellIter;

private:
  ObList<ObIKToken, ObIAllocator> tokens_;
};

class ObIKTokenChain
{
public:
  ObIKTokenChain(ObIAllocator &alloc) : list_(alloc) {}
  ~ObIKTokenChain() { list_.reset(); }

public:
  int add_token_if_conflict(const ObIKToken &token, bool &added);

  int add_token_if_no_conflict(const ObIKToken &token, bool &added);

  int pop_back(ObIKToken &token);

  bool check_conflict(const ObIKToken &token);

  ObFTSortList &list() { return list_; }

  bool better_than(const ObIKTokenChain &other) const;

  int copy(ObIKTokenChain *other);

  int64_t min_offset() const { return min_offset_; }

  int64_t max_offset() const { return max_offset_; }

  int64_t offset_len() const { return max_offset_ - min_offset_; }

  int64_t payload() const { return payload_; }

  int64_t x_weight() const;

  int64_t p_weight() const;

private:
  int min_offset_ = -1;
  int max_offset_ = -1;
  int payload_ = -1;
  ObFTSortList list_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_TOKEN_H_
