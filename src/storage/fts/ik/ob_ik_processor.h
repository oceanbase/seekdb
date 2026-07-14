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
#include "storage/fts/ik/ob_fast_segment_array.h"

namespace oceanbase
{
namespace storage
{
class TokenizeContext
{
public:
  // Task4：缓存字符集合法字符长度函数，避免逐字符通过 ObCharset 重复查表。
  using WellFormedLenFunc = size_t (*)(const ObCharsetInfo *,
                                       const char *,
                                       const char *,
                                       size_t,
                                       int *);

  TokenizeContext(ObCollationType coll_type,
                  ObIAllocator &allocator,
                  const char *fulltext,
                  int64_t fulltext_len,
                  bool is_smart);

  ~TokenizeContext();

  int init();
  // Task4 Op2：为下一文档重置游标和临时 token，保留字典及已申请容器块。
  int reuse_context(const char *fulltext, int64_t fulltext_len);
  int reset_resource();

  int get_next_token(const char *&word, int64_t &word_len, int64_t &offset, int64_t &char_cnt);

  int compound(ObIKToken &result);

  int current_char(const char *&ch, uint8_t &char_len);
  int current_char_type(ObFTCharUtil::CharType &type);

  // Task4：一次边界检查同时返回字符地址、长度和类型，减少分词热路径中的重复调用。
  int current_char_and_type(const char *&ch,
                            uint8_t &char_len,
                            ObFTCharUtil::CharType &type);

  int step_next();

  ObCollationType collation() const;
  int64_t get_end_cursor() const;
  const char *fulltext() const;
  int64_t fulltext_len() const;
  int64_t get_cursor() const;

  bool is_last() const;
  bool iter_end() const;
  bool is_smart() const;
  // Task4 Op1：通过索引判断分块结果是否消费完，避免链表逐节点弹出。
  bool is_results_exhaust() const;

  int add_chain(ObIKTokenChain *chain);
  int add_token(const char *fulltext,
                int64_t offset,
                int64_t length,
                int64_t char_cnt,
                ObIKTokenType type);

  // Task4 Op1：主排序链使用节点池化 FastList。
  ObFTFastSortList &token_list() { return token_list_; }

  // Task4 Op1：输出结果连续写入可复用分块数组。
  ObFastSegmentArray<ObIKToken> &get_results() { return results_; }

  int32_t handle_size() const { return handle_size_; }

private:
  int prepare_next_char();

  ObCollationType coll_type_;
  // Task4：字符集元数据和热路径函数指针在上下文构造时缓存。
  const ObCharsetInfo *charset_info_;
  WellFormedLenFunc well_formed_len_func_;
  const char *fulltext_;
  int64_t fulltext_len_;

  int64_t cursor_;
  int64_t next_char_len_;
  ObFTCharUtil::CharType next_char_type_;

  uint32_t handle_size_;
  bool is_smart_;

  ObFTFastSortList token_list_;
  ObFastSegmentArray<ObIKToken> results_;
  int64_t result_idx_;

private:
  DISALLOW_COPY_AND_ASSIGN(TokenizeContext);
};

class ObIIKProcessor
{
public:
  ObIIKProcessor() {}

  virtual ~ObIIKProcessor() {}

  int process(TokenizeContext &ctx);

  // Task4：复用调用方已经取得的字符信息，避免每个处理器重复读取上下文。
  int process(TokenizeContext &ctx,
              const char *ch,
              const uint8_t char_len,
              const ObFTCharUtil::CharType type);

  virtual int do_process(TokenizeContext &ctx,
                         const char *ch,
                         const uint8_t char_len,
                         const ObFTCharUtil::CharType type)
      = 0;

  // Task4 Op2：解析器复用时清理具体处理器的跨字符状态。
  virtual void reuse() = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_PROCESSOR_H_
