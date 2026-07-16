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

/**
 * Unit test for the ai_split_document splitter library (seekdb).
 * Stronger than the upstream cout-based test: asserts exact chunk counts,
 * texts, overlap windows, markdown sectioning, and parameter validation.
 */

#include <gtest/gtest.h>
#include "lib/allocator/page_arena.h"
#include "lib/ai_split_document/ob_ai_split_document.h"
#include "lib/ai_split_document/ob_ai_split_document_util.h"
#include <string>
#include <vector>

using namespace oceanbase::common;

namespace {
struct UtChunk { int64_t id_; int64_t off_; int64_t len_; std::string text_; };

// drive an iterator to exhaustion, collecting all chunks
int collect(ObDocSplitIterator &it, const ObString &content, ObIAllocator &alloc,
            const ObAiSplitDocParams &params, std::vector<UtChunk> &out)
{
  int ret = it.open(content, alloc, params);
  if (OB_SUCCESS != ret) { return ret; }
  ObAiSplitDocChunk c;
  while (OB_SUCCESS == (ret = it.get_next_row(c))) {
    out.push_back(UtChunk{c.chunk_id_, c.chunk_offset_, c.chunk_length_,
                          std::string(c.chunk_text_.ptr(), static_cast<size_t>(c.chunk_text_.length()))});
    c.reset();
  }
  (void)it.close();
  return (OB_ITER_END == ret) ? OB_SUCCESS : ret;
}
} // namespace

class ObAiSplitDocumentTest : public ::testing::Test
{
public:
  ObAiSplitDocumentTest() : alloc_("AiSplitUT") {}
  void SetUp() override { alloc_.reuse(); }
  void TearDown() override { alloc_.reuse(); }
protected:
  ObArenaAllocator alloc_;
};

// Splitting plain text by sentence, one sentence per chunk.
TEST_F(ObAiSplitDocumentTest, text_by_sentence)
{
  ObString content("First sentence. Second sentence. Third sentence.");
  ObAiSplitDocParams p;
  p.type_ = ObAiSplitContentType::TEXT;
  p.by_ = ObAiSplitByUnit::SENTENCE;
  p.max_ = 1;
  p.overlap_ = 0;

  ObTextSplitIterator it;
  std::vector<UtChunk> v;
  ASSERT_EQ(OB_SUCCESS, collect(it, content, alloc_, p, v));
  ASSERT_EQ(3u, v.size());
  EXPECT_EQ("First sentence.", v[0].text_);
  EXPECT_EQ("Second sentence.", v[1].text_);
  EXPECT_EQ("Third sentence.", v[2].text_);
  // invariants: 0-based ids, length == text bytes, offsets strictly increasing, first at 0
  EXPECT_EQ(0, v[0].off_);
  for (size_t i = 0; i < v.size(); ++i) {
    EXPECT_EQ(static_cast<int64_t>(i), v[i].id_);
    EXPECT_EQ(static_cast<int64_t>(v[i].text_.size()), v[i].len_);
    if (i > 0) { EXPECT_GT(v[i].off_, v[i-1].off_); }
  }
}

// Splitting by word with a sliding window: max=3, overlap=1 -> windows share 1 word.
TEST_F(ObAiSplitDocumentTest, text_by_word_with_overlap)
{
  ObString content("alpha beta gamma delta epsilon zeta eta theta");
  ObAiSplitDocParams p;
  p.type_ = ObAiSplitContentType::TEXT;
  p.by_ = ObAiSplitByUnit::WORD;
  p.max_ = 3;
  p.overlap_ = 1;

  ObTextSplitIterator it;
  std::vector<UtChunk> v;
  ASSERT_EQ(OB_SUCCESS, collect(it, content, alloc_, p, v));
  ASSERT_EQ(4u, v.size());
  EXPECT_EQ("alpha beta gamma", v[0].text_);
  EXPECT_EQ("gamma delta epsilon", v[1].text_);
  EXPECT_EQ("epsilon zeta eta", v[2].text_);
  EXPECT_EQ("eta theta", v[3].text_);
}

// Markdown: split into '#' sections; each chunk carries its section title.
TEST_F(ObAiSplitDocumentTest, markdown_sections_prepend_title)
{
  ObString content("# Section A\nThis is content. More here.\n# Section B\nSecond section.");
  ObAiSplitDocParams p;
  p.type_ = ObAiSplitContentType::MARKDOWN;
  p.by_ = ObAiSplitByUnit::SENTENCE;
  p.max_ = 1;
  p.overlap_ = 0;

  ObMarkdownSplitIterator it;
  std::vector<UtChunk> v;
  ASSERT_EQ(OB_SUCCESS, collect(it, content, alloc_, p, v));
  ASSERT_GE(v.size(), 2u);
  // every chunk from section A must carry the "Section A" title; likewise B
  bool saw_a_content = false, saw_b_content = false;
  for (const auto &c : v) {
    if (c.text_.find("This is content.") != std::string::npos) {
      EXPECT_NE(std::string::npos, c.text_.find("Section A"));
      saw_a_content = true;
    }
    if (c.text_.find("Second section.") != std::string::npos) {
      EXPECT_NE(std::string::npos, c.text_.find("Section B"));
      saw_b_content = true;
    }
  }
  EXPECT_TRUE(saw_a_content);
  EXPECT_TRUE(saw_b_content);
}

// Parameter validation: defaults ok; max out of range and overlap > max/2 rejected.
TEST_F(ObAiSplitDocumentTest, param_validation)
{
  ObAiSplitDocParams p;            // defaults: markdown/word/max=256/overlap=0
  EXPECT_EQ(OB_SUCCESS, p.check_validity());

  p.max_ = 1000; p.overlap_ = 0;   // boundary ok
  EXPECT_EQ(OB_SUCCESS, p.check_validity());

  p.max_ = 99999; p.overlap_ = 0;  // > 1000 -> invalid
  EXPECT_NE(OB_SUCCESS, p.check_validity());

  p.max_ = 100; p.overlap_ = 60;   // overlap > max/2 -> invalid
  EXPECT_NE(OB_SUCCESS, p.check_validity());

  p.max_ = 0; p.overlap_ = 0;      // max must be > 0
  EXPECT_NE(OB_SUCCESS, p.check_validity());
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("WARN");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
