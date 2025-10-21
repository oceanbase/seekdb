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
