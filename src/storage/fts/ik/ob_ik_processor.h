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
  // Re-apply the per-fulltext state on top of a reused context: cursor
  // position, charset resolution and the per-character cache cursor are all
  // re-derived from the new fulltext, and the cross-call caches are emptied
  // (char_infos_ entries are dropped when the parser is released, see
  // release_scratch()). This is the reuse counterpart of init().
  int reinit(ObCollationType coll_type,
             const char *fulltext,
             int64_t fulltext_len,
             bool is_smart);
  // Walk-and-clear every per-call container while their memory is still
  // valid, then drop the per-character cache table so no stale entry can be
  // read by the next fulltext. Must be called before the scratch allocator
  // backing the containers is recycled.
  void release_scratch();
  int reset_resource();

  int get_next_token(const char *&word, int64_t &word_len, int64_t &offset, int64_t &char_cnt);

  int compound(ObIKToken &result);

  int current_char(const char *&ch, uint8_t &char_len);
  int current_char_type(ObFTCharUtil::CharType &type);

  int step_next();

  // Reuse table of per-character (offset, len, type) recorded while the
  // cursor advances, so that ObIKArbitrator::output_result() can look the
  // classification up instead of re-decoding the fulltext a second time.
  // byte_offset must be a character start boundary; hint_idx is a monotonic
  // lookup cursor owned by the caller. Positions beyond the prepared region
  // fall back to the original first_valid_char + classify_first_char path.
  int get_char_info(int64_t byte_offset,
                    int64_t &hint_idx,
                    int64_t &char_len,
                    ObFTCharUtil::CharType &type) const;
  int get_char_len(int64_t byte_offset, int64_t &hint_idx, int64_t &char_len) const;

  ObCollationType collation() const;
  int64_t get_end_cursor() const;
  const char *fulltext() const;
  int64_t fulltext_len() const;
  int64_t get_cursor() const;

  bool is_last() const;
  bool iter_end() const;
  bool is_smart() const;

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
  struct CharInfo
  {
    int64_t offset_;
    int64_t len_;
    ObFTCharUtil::CharType type_;
  };

  int prepare_next_char();
  int record_char_info();
  int lookup_char_info(int64_t byte_offset, int64_t &hint_idx, CharInfo &info) const;

  ObCollationType coll_type_;
  const char *fulltext_;
  int64_t fulltext_len_;

  ObIAllocator &allocator_;
  CharInfo *char_infos_;
  int64_t char_info_cnt_;
  int64_t char_info_cap_;

  int64_t cursor_;
  int64_t next_char_len_;
  ObFTCharUtil::CharType next_char_type_;

  // charset resolved once in init() so that the per-character hot path is free
  // of repeated get_charset()/charset_type_by_coll() lookups
  const ObCharsetInfo *cs_;
  ObCharsetType cs_type_;

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

  // Reset the cross-call mutable state of this processor so that a reused
  // processor never carries pending-token state from one fulltext into the
  // next. Default: the processor keeps no cross-call state.
  virtual void reset_state() {}

  virtual int do_process(TokenizeContext &ctx,
                         const char *ch,
                         const uint8_t char_len,
                         const ObFTCharUtil::CharType type)
      = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_PROCESSOR_H_
