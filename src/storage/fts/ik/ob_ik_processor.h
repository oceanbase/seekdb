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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_IK_PROCESSOR_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_IK_PROCESSOR_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/ik/ob_ik_char_util.h"
#include "storage/fts/ik/ob_ik_token.h"

namespace oceanbase
{
namespace storage
{
class TokenizeContext
{
public:
  TokenizeContext(ObCollationType coll_type,
                  ObIAllocator &allocator,
                  const char *fulltext,
                  int64_t fulltext_len,
                  bool is_smart);

  ~TokenizeContext();

  int init();
  int reuse_context(const char *fulltext, const int64_t fulltext_len);
  int reset_resource();

  int get_next_token(const char *&word, int64_t &word_len, int64_t &offset, int64_t &char_cnt);

  int compound(ObIKToken &result);

  OB_INLINE int current_char(const char *&ch, uint8_t &char_len)
  {
    int ret = OB_SUCCESS;
    if (cursor_ >= fulltext_len_) {
      ret = OB_ITER_END;
    } else {
      ch = fulltext_ + cursor_;
      char_len = static_cast<uint8_t>(next_char_len_);
    }
    return ret;
  }

  OB_INLINE int current_char_type(ObFTCharUtil::CharType &type)
  {
    int ret = OB_SUCCESS;
    if (cursor_ >= fulltext_len_) {
      ret = OB_ITER_END;
    } else {
      type = next_char_type_;
    }
    return ret;
  }

  OB_INLINE int current_char_and_type(const char *&ch,
                                      uint8_t &char_len,
                                      ObFTCharUtil::CharType &type)
  {
    int ret = OB_SUCCESS;
    if (cursor_ >= fulltext_len_) {
      ret = OB_ITER_END;
    } else {
      ch = fulltext_ + cursor_;
      char_len = static_cast<uint8_t>(next_char_len_);
      type = next_char_type_;
    }
    return ret;
  }

  int step_next();

  OB_INLINE ObCollationType collation() const { return coll_type_; }
  OB_INLINE int64_t get_end_cursor() const { return cursor_ + next_char_len_; }
  OB_INLINE const char *fulltext() const { return fulltext_; }
  OB_INLINE int64_t fulltext_len() const { return fulltext_len_; }
  OB_INLINE int64_t get_cursor() const { return cursor_; }

  OB_INLINE bool is_last() const { return cursor_ + next_char_len_ >= fulltext_len_; }
  OB_INLINE bool iter_end() const { return cursor_ >= fulltext_len_; }
  OB_INLINE bool is_smart() const { return is_smart_; }

  int add_chain(ObIKTokenChain *chain);
  int add_token(const char *fulltext,
                int64_t offset,
                int64_t length,
                int64_t char_cnt,
                ObIKTokenType type);

  ObFTSortList &token_list() { return token_list_; }

  ObList<ObIKToken, ObIAllocator> &result_list() { return result_list_; }

  int32_t handle_size() const { return handle_size_; }

private:
  int prepare_next_char();

  ObCollationType coll_type_;
  common::ObCharsetType cs_type_;
  const char *fulltext_;
  int64_t fulltext_len_;

  int64_t cursor_;
  int64_t next_char_len_;
  ObFTCharUtil::CharType next_char_type_;

  uint32_t handle_size_;
  bool is_smart_;

  ObFTSortList token_list_;
  ObList<ObIKToken, ObIAllocator> result_list_;
private:
  DISALLOW_COPY_AND_ASSIGN(TokenizeContext);
};

class ObIIKProcessor
{
public:
  ObIIKProcessor() {}

  virtual ~ObIIKProcessor() {}

  int process(TokenizeContext &ctx);

  virtual int do_process(TokenizeContext &ctx,
                         const char *ch,
                         const uint8_t char_len,
                         const ObFTCharUtil::CharType type)
      = 0;

  virtual void reuse() = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_PROCESSOR_H_
