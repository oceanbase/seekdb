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

#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/ob_errno.h"
#include "storage/fts/ik/ob_fast_list.h"
#include "storage/fts/ik/ob_fast_segment_array.h"
#include "storage/fts/ob_beng_ft_parser.h"
#include "storage/fts/ob_i_ft_parser.h"
#include "storage/fts/ob_ik_ft_parser.h"
#include "storage/fts/ob_ngram2_ft_parser.h"
#include "storage/fts/ob_ngram_ft_parser.h"
#include "storage/fts/ob_whitespace_ft_parser.h"

#include <gtest/gtest.h>
#include <string>
#include <type_traits>
#include <vector>

namespace oceanbase
{
namespace storage
{

// 五种内置解析器必须共享统一的复用契约，后续调用方才能安全缓存基类指针。
static_assert(std::is_abstract<ObIFTParser>::value, "ObIFTParser must remain abstract");
static_assert(std::is_base_of<ObIFTParser, ObIKFTParser>::value, "IK parser must support reuse");
static_assert(std::is_base_of<ObIFTParser, ObNgramFTParser>::value, "ngram parser must support reuse");
static_assert(std::is_base_of<ObIFTParser, ObNgram2FTParser>::value, "ngram2 parser must support reuse");
static_assert(std::is_base_of<ObIFTParser, ObBEngFTParser>::value, "beng parser must support reuse");
static_assert(std::is_base_of<ObIFTParser, ObSpaceFTParser>::value, "space parser must support reuse");

TEST(FTSHotPath, FastSegmentArrayCrossBlockAccessAndReuseKeepsAllocatedBlocks)
{
  common::ObArenaAllocator allocator("FtsHotPath");
  ObFastSegmentArray<int64_t, 4> values(allocator);
  for (int64_t i = 0; i < 9; ++i) {
    ASSERT_EQ(OB_SUCCESS, values.push_back(i));
  }
  ASSERT_EQ(9, values.count());
  ASSERT_EQ(3, values.at(3));
  ASSERT_EQ(4, values.at(4));
  ASSERT_EQ(8, values.at(8));

  int64_t *first = &values.at(0);
  values.reuse();
  ASSERT_EQ(0, values.count());
  ASSERT_EQ(OB_SUCCESS, values.push_back(42));
  ASSERT_EQ(first, &values.at(0));
  ASSERT_EQ(42, values.at(0));
}

TEST(FTSHotPath, FastListPreservesBidirectionalOrderAfterReuse)
{
  common::ObArenaAllocator allocator("FtsHotPath");
  ObFastList<int64_t, 4> values(allocator);
  ASSERT_EQ(OB_SUCCESS, values.push_back(2));
  const int64_t *first_allocated = &values.get_first();
  ASSERT_EQ(OB_SUCCESS, values.push_front(1));
  ASSERT_EQ(OB_SUCCESS, values.push_back(3));
  ASSERT_EQ(1, values.get_first());
  ASSERT_EQ(3, values.get_last());

  std::vector<int64_t> forward;
  for (auto iter = values.begin(); iter != values.end(); ++iter) {
    forward.push_back(*iter);
  }
  ASSERT_EQ((std::vector<int64_t>{1, 2, 3}), forward);

  values.reuse();
  ASSERT_TRUE(values.empty());
  ASSERT_EQ(OB_SUCCESS, values.push_back(7));
  ASSERT_EQ(7, values.get_first());
  // reuse 按节点池分配顺序复用；push_front 改变逻辑首节点但不改变首个分配槽。
  ASSERT_EQ(first_allocated, &values.get_first());
}

template <typename Parser>
std::vector<std::string> collect_tokens(Parser &parser)
{
  std::vector<std::string> tokens;
  const char *token = nullptr;
  int64_t token_len = 0;
  int64_t char_cnt = 0;
  int64_t token_freq = 0;
  int ret = OB_SUCCESS;
  while (OB_SUCCESS == (ret = parser.get_next_token(token, token_len, char_cnt, token_freq))) {
    tokens.emplace_back(token, token_len);
  }
  EXPECT_EQ(OB_ITER_END, ret);
  return tokens;
}

TEST(FTSHotPath, BuiltinParserReuseMatchesFirstTokenSequence)
{
  common::ObArenaAllocator metadata_alloc("FtsMetadata");
  common::ObArenaAllocator scratch_alloc("FtsScratch");
  const ObCharsetInfo *cs = common::ObCharset::get_charset(common::CS_TYPE_UTF8MB4_BIN);
  ASSERT_NE(nullptr, cs);

  const char first_text[] = "alpha beta gamma";
  const char reused_text[] = "alpha beta gamma";
  plugin::ObFTParserParam param;
  param.metadata_alloc_ = &metadata_alloc;
  param.scratch_alloc_ = &scratch_alloc;
  param.cs_ = cs;
  param.fulltext_ = first_text;
  param.ft_length_ = static_cast<int64_t>(sizeof(first_text) - 1);

  ObSpaceFTParser parser;
  ASSERT_EQ(OB_SUCCESS, parser.init(&param));
  const std::vector<std::string> first_tokens = collect_tokens(parser);
  ASSERT_FALSE(first_tokens.empty());

  // 复制完上一文档输出后才能清理 scratch；复用后的 token 不得引用已失效的上一文档内存。
  scratch_alloc.reuse();
  ASSERT_EQ(OB_SUCCESS,
            parser.reuse_parser(reused_text, static_cast<int64_t>(sizeof(reused_text) - 1)));
  const std::vector<std::string> reused_tokens = collect_tokens(parser);
  ASSERT_EQ(first_tokens, reused_tokens);
}

} // namespace storage
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
