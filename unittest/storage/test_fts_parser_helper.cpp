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


#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#define USING_LOG_PREFIX STORAGE

#define protected public
#define private   public

#include "lib/charset/ob_charset.h"
#include "object/ob_object.h"
#include "storage/fts/ob_fts_parser.h"
#include "storage/fts/ob_fts_parser_helper.h"
#include "storage/fts/ob_fts_stop_word.h"
#include "storage/fts/ob_whitespace_ft_parser.h"
#include "storage/fts/utils/ob_ft_ngram_impl.h"

namespace oceanbase
{

namespace storage
{

typedef common::hash::ObHashMap<ObFTWord, int64_t> ObFTWordMap;

int segment_and_calc_word_count(
    common::ObIAllocator &allocator,
    storage::ObFTParseHelper *helper,
    const common::ObObjMeta &meta,
    const ObString &fulltext,
    ObFTWordMap &words_count)
{
  int ret = OB_SUCCESS;
  int64_t doc_length = 0;
  if (OB_ISNULL(helper) ||
      OB_UNLIKELY(
          ObCollationType::CS_TYPE_INVALID == meta.get_collation_type() ||
          ObCollationType::CS_TYPE_PINYIN_BEGIN_MARK <= meta.get_collation_type()) ||
      OB_UNLIKELY(!words_count.created())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(helper), K(meta), K(words_count.created()));
  } else if (OB_FAIL(helper->segment(meta, fulltext.ptr(), fulltext.length(), doc_length, words_count))) {
    LOG_WARN("fail to segment", K(ret), KPC(helper), K(meta), K(fulltext));
  }
  return ret;
}

class ObTestAddWord final
{
public:
  static const char *TEST_FULLTEXT;
  static const int64_t TEST_WORD_COUNT = 5;
  static const int64_t TEST_WORD_COUNT_WITHOUT_STOPWORD = 4;
  static const int64_t FT_MIN_WORD_LEN = 3;
  static const int64_t FT_MAX_WORD_LEN = 84;
public:
  ObTestAddWord(const ObObjMeta &meta, common::ObIAllocator &allocator);
  ~ObTestAddWord() = default;

  static ObObjMeta get_meta(ObCollationType type)
  {
    ObObjMeta meta;
    meta.set_varchar();
    meta.set_collation_type(type);
    return meta;
  }
  int check_words(ObITokenIterator *iter);
  int64_t get_add_word_count() const { return ith_word_; }
  static int64_t get_word_cnt_without_stopword() { return TEST_WORD_COUNT_WITHOUT_STOPWORD; }
  VIRTUAL_TO_STRING_KV(K_(ith_word));
private:
  int check_ith_word(
      const char *word,
      const int64_t word_len,
      const int64_t char_cnt);
private:
  bool is_min_max_word(const int64_t c_len) const;
  int casedown_word(const ObFTWord &src, ObFTWord &dst);
  ObObjMeta meta_;
  common::ObIAllocator &allocator_;
  const char *words_[TEST_WORD_COUNT];
  const char *words_without_stopword_[TEST_WORD_COUNT_WITHOUT_STOPWORD];
  int64_t ith_word_;
};

const char *ObTestAddWord::TEST_FULLTEXT = "OceanBase fulltext search is No.1 in the world.";

ObTestAddWord::ObTestAddWord(const ObObjMeta &meta, common::ObIAllocator &allocator)
  : meta_(meta),
    allocator_(allocator),
    words_{"oceanbase", "fulltext", "search", "the", "world"},
    words_without_stopword_{"oceanbase", "fulltext", "search", "world"},
    ith_word_(0)
{
}

bool ObTestAddWord::is_min_max_word(const int64_t c_len) const
{
  return c_len < FT_MIN_WORD_LEN || c_len > FT_MAX_WORD_LEN;
}

int ObTestAddWord::casedown_word(const ObFTWord &src, ObFTWord &dst)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(src.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid src ft word", K(ret), K(src));
  } else {
    ObString dst_str;
    if (OB_FAIL(ObCharset::tolower(meta_.get_collation_type(), src.get_word().get_string(), dst_str, allocator_))) {
      LOG_WARN("fail to tolower", K(ret), K(src), K(meta_));
    } else {
      ObFTWord tmp(dst_str.length(), dst_str.ptr(), meta_);
      dst = tmp;
    }
  }
  return ret;
}

int ObTestAddWord::check_words(ObITokenIterator *iter)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(iter));
  } else {
    const char *word = nullptr;
    int64_t word_len = 0;
    int64_t char_len = 0;
    int64_t word_freq = 0;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(iter->get_next_token(word, word_len, char_len, word_freq))) {
        LOG_WARN("fail to get next token", K(ret), KPC(iter));
      } else if (OB_FAIL(check_ith_word(word, word_len, char_len))) {
        LOG_WARN("fail to check ith word", K(ret), KP(word), K(word_len), K(char_len));
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObTestAddWord::check_ith_word(
      const char *word,
      const int64_t word_len,
      const int64_t char_cnt)
{
  int ret = OB_SUCCESS;
  ObFTWord src_word(word_len, word, meta_);
  ObFTWord dst_word;
  if (OB_ISNULL(word) || OB_UNLIKELY(0 >= word_len || 0 >= char_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(word), K(word_len), K(char_cnt));
  } else if (is_min_max_word(char_cnt)) {
    // skip min/max word
  } else if (OB_FAIL(casedown_word(src_word, dst_word))) {
    LOG_WARN("fail to casedown word", K(ret), K(src_word));
  } else if (OB_UNLIKELY(0 != strncmp(words_[ith_word_],
                                      dst_word.get_word().get_string().ptr(),
                                      dst_word.get_word().get_string().length()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the ith word isn't default word", K(ret), K(ith_word_), KCSTRING(words_[ith_word_]), K(dst_word));
  } else {
    ++ith_word_;
  }
  return ret;
}

class TestDefaultFTParser : public ::testing::Test
{
public:
  TestDefaultFTParser();
  virtual ~TestDefaultFTParser() = default;

  virtual void SetUp() override;
  virtual void TearDown() override;

private:
  ObFTParser parser_;
  const ObIFTParserDesc *desc_;
  ObFTParserParam ft_parser_param_;
  common::ObArenaAllocator allocator_;
  ObTestAddWord add_word_;
};

TestDefaultFTParser::TestDefaultFTParser()
  : parser_(),
    desc_(nullptr),
    ft_parser_param_(),
    allocator_(),
    add_word_(ObTestAddWord::get_meta(ObCollationType::CS_TYPE_UTF8MB4_BIN), allocator_)
{
}

void TestDefaultFTParser::SetUp()
{
  ASSERT_EQ(OB_SUCCESS, parser_.init("space"));
  ASSERT_EQ(OB_SUCCESS, parser_.get_desc(desc_));
  ASSERT_NE(nullptr, desc_);

  ft_parser_param_.allocator_ = &allocator_;
  ft_parser_param_.cs_ = common::ObCharset::get_charset(ObCollationType::CS_TYPE_UTF8MB4_BIN);
  ft_parser_param_.parser_version_ = 0x00001;
  ASSERT_TRUE(nullptr != ft_parser_param_.cs_);
}

void TestDefaultFTParser::TearDown()
{
  ft_parser_param_.reset();
  desc_ = nullptr;
}

TEST_F(TestDefaultFTParser, test_space_ft_parser_segment)
{
  ObSpaceFTParser parser;
  const char *fulltext = ObTestAddWord::TEST_FULLTEXT;
  const int64_t ft_len = strlen(fulltext);

  ASSERT_EQ(OB_INVALID_ARGUMENT, parser.init(nullptr));

  ft_parser_param_.fulltext_ = nullptr;
  ft_parser_param_.ft_length_ = 0;
  ASSERT_EQ(OB_INVALID_ARGUMENT, parser.init(&ft_parser_param_));

  ft_parser_param_.fulltext_ = fulltext;
  ASSERT_EQ(OB_INVALID_ARGUMENT, parser.init(&ft_parser_param_));

  ft_parser_param_.ft_length_ = -1;
  ASSERT_EQ(OB_INVALID_ARGUMENT, parser.init(&ft_parser_param_));

  ft_parser_param_.fulltext_ = fulltext;
  ft_parser_param_.ft_length_ = ft_len;

  LOG_INFO("before space segment", KCSTRING(fulltext), K(ft_len), K(ft_parser_param_));
  ASSERT_EQ(OB_SUCCESS, parser.init(&ft_parser_param_));
  ASSERT_EQ(OB_SUCCESS, add_word_.check_words(&parser));
  LOG_INFO("after space segment", KCSTRING(fulltext), K(ft_len), K(ft_parser_param_));
}

TEST_F(TestDefaultFTParser, test_space_ft_parser_segment_bug_56324268)
{
  ObSpaceFTParser parser;
  const char *fulltext = "\201 想 将 数据 添加 到 数据库\f\026 ";
  const int64_t ft_len = strlen(fulltext);

  ft_parser_param_.fulltext_ = fulltext;
  ft_parser_param_.ft_length_ = ft_len;
  ft_parser_param_.cs_ = common::ObCharset::get_charset(ObCollationType::CS_TYPE_UTF8MB4_BIN);

  LOG_INFO("before space segment", KCSTRING(fulltext), K(ft_len), K(ft_parser_param_));
  ASSERT_EQ(OB_SUCCESS, parser.init(&ft_parser_param_));
  const char *word = nullptr;
  int64_t word_len = 0;
  int64_t char_len = 0;
  int64_t word_freq = 0;
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(parser.get_next_token(word, word_len, char_len, word_freq))) {
      LOG_WARN("fail to get next token", K(ret), K(parser));
    } else {
      LOG_INFO("succeed to get next token", K(ret), K(ObString(word_len, word)), K(char_len));
    }
  }
  LOG_INFO("after space segment", KCSTRING(fulltext), K(ft_len), K(ft_parser_param_));
}

TEST_F(TestDefaultFTParser, test_default_ft_parser_desc)
{
  ObITokenIterator *iter = nullptr;
  ASSERT_EQ(OB_INVALID_ARGUMENT, desc_->segment(&ft_parser_param_, iter));

  ft_parser_param_.fulltext_ = ObTestAddWord::TEST_FULLTEXT;
  ft_parser_param_.ft_length_ = strlen(ft_parser_param_.fulltext_);

  ASSERT_EQ(OB_SUCCESS, desc_->segment(&ft_parser_param_, iter));
  ASSERT_EQ(OB_SUCCESS, add_word_.check_words(iter));
  desc_->free_token_iter(&ft_parser_param_, iter);
  iter = nullptr;
  ASSERT_EQ(OB_INVALID_ARGUMENT, desc_->segment(nullptr, iter));
}

class ObTestFTParseHelper : public ::testing::Test
{
public:
  static const char *name_;
  static const char *properties_;
  typedef common::hash::ObHashMap<ObFTWord, int64_t> ObFTWordMap;
public:
  ObTestFTParseHelper();
  virtual ~ObTestFTParseHelper() = default;

  virtual void SetUp() override;
  virtual void TearDown() override;

private:
  const common::ObString parser_name_;
  const common::ObString parser_properties_;
  common::ObObjMeta meta_;
  common::ObArenaAllocator allocator_;
  ObFTParseHelper parse_helper_;
};

const char *ObTestFTParseHelper::name_ = "space.1";
const char *ObTestFTParseHelper::properties_ = "{\"min_token_size\":3,\"max_token_size\":84,\"stopword_table\":\"default\",\"dict_table\":\"none\",\"quanitfier_table\":\"none\",\"ngram_token_size\":2}";
ObTestFTParseHelper::ObTestFTParseHelper()
  : parser_name_(STRLEN(name_), name_),
    parser_properties_(STRLEN(properties_), properties_),
    meta_(),
    allocator_()
{
  meta_.set_varchar();
  meta_.set_collation_type(ObCollationType::CS_TYPE_UTF8MB4_BIN);
}

void ObTestFTParseHelper::SetUp()
{
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));
}

void ObTestFTParseHelper::TearDown()
{
  parse_helper_.reset();
}

TEST_F(ObTestFTParseHelper, test_parse_fulltext)
{
  ObFTWordMap ft_word_map;
  ASSERT_EQ(OB_SUCCESS, ft_word_map.create(10, "TestParse"));
  int64_t doc_length = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          ft_word_map));

  ObTestAddWord test_add_word(meta_, allocator_);
  ASSERT_EQ(ObTestAddWord::get_word_cnt_without_stopword(), ft_word_map.size());
  for (int64_t i = 0; i < ft_word_map.size(); ++i) {
    int64_t word_cnt = 0;
    ObFTWord word(strlen(test_add_word.words_without_stopword_[i]), test_add_word.words_without_stopword_[i], meta_);
    ASSERT_EQ(OB_SUCCESS, ft_word_map.get_refactored(word, word_cnt));
    ASSERT_TRUE(word_cnt >= 1);
  }

  ft_word_map.clear();
  ASSERT_EQ(
      OB_SUCCESS,
      segment_and_calc_word_count(allocator_, &parse_helper_, meta_, ObTestAddWord::TEST_FULLTEXT, ft_word_map));
  ASSERT_EQ(ObTestAddWord::get_word_cnt_without_stopword(), ft_word_map.size());

  ft_word_map.clear();
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(meta_, nullptr, std::strlen(ObTestAddWord::TEST_FULLTEXT), doc_length, ft_word_map));
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(meta_, ObTestAddWord::TEST_FULLTEXT, 0, doc_length, ft_word_map));
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(meta_, ObTestAddWord::TEST_FULLTEXT, -1, doc_length, ft_word_map));

  parse_helper_.reset();
  ft_word_map.clear();
  ASSERT_EQ(
      OB_NOT_INIT,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          ft_word_map));

  ASSERT_EQ(OB_INVALID_ARGUMENT, parse_helper_.init(nullptr, parser_name_, parser_properties_));
  ASSERT_EQ(OB_INVALID_ARGUMENT, parse_helper_.init(&allocator_, ObString(), parser_properties_));

  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));

  ObObjMeta default_meta;
  default_meta.set_varchar();
  default_meta.set_collation_type(common::CS_TYPE_INVALID);
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(
          default_meta,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          ft_word_map));

  ObObjMeta meta_pinyin;
  meta_pinyin.set_varchar();
  meta_pinyin.set_collation_type(CS_TYPE_PINYIN_BEGIN_MARK);
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(
          meta_pinyin,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          ft_word_map));

  ASSERT_EQ(OB_INIT_TWICE, parse_helper_.init(&allocator_, parser_name_, parser_properties_));

  parse_helper_.reset();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));

  parse_helper_.reset();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));
  ASSERT_EQ(
      OB_SUCCESS,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          ft_word_map));
  ASSERT_EQ(ObTestAddWord::get_word_cnt_without_stopword(), ft_word_map.size());
  for (int64_t i = 0; i < ft_word_map.size(); ++i) {
    int64_t word_cnt = 0;
    ObFTWord word(strlen(test_add_word.words_without_stopword_[i]), test_add_word.words_without_stopword_[i], meta_);
    ASSERT_EQ(OB_SUCCESS, ft_word_map.get_refactored(word, word_cnt));
    ASSERT_TRUE(word_cnt >= 1);
  }
  parse_helper_.reset();
  ft_word_map.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, "beng.1", parser_properties_));
  ASSERT_EQ(
      OB_SUCCESS,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          ft_word_map));
  ASSERT_EQ(ObTestAddWord::get_word_cnt_without_stopword(), ft_word_map.size());
  for (int64_t i = 0; i < ft_word_map.size(); ++i) {
    int64_t word_cnt = 0;
    ObFTWord word(strlen(test_add_word.words_without_stopword_[i]), test_add_word.words_without_stopword_[i], meta_);
    ASSERT_EQ(OB_SUCCESS, ft_word_map.get_refactored(word, word_cnt));
    ASSERT_TRUE(word_cnt >= 1);
  }
}

TEST_F(ObTestFTParseHelper, test_min_and_max_word_len)
{
  ObFTWordMap words;
  ASSERT_EQ(OB_SUCCESS, words.create(10, "TestParse"));
  int64_t doc_length = 0;

  // word len = 2;
  const char *word_len_2 = "ab";
  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, word_len_2, std::strlen(word_len_2), doc_length, words));
  ASSERT_EQ(0, words.size());

  // word len = 3;
  const char *word_len_3 = "abc";
  words.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, word_len_3, std::strlen(word_len_3), doc_length, words));
  ASSERT_EQ(1, words.size());

  // word len = 4;
  const char *word_len_4 = "abcd";
  words.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, word_len_4, std::strlen(word_len_4), doc_length, words));
  ASSERT_EQ(1, words.size());

  // word len = 76;
  const char *word_len_76 = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
  words.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, word_len_76, std::strlen(word_len_76), doc_length, words));
  ASSERT_EQ(1, words.size());

  // word len = 84;
  const char *word_len_84 = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz123456";
  words.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, word_len_84, std::strlen(word_len_84), doc_length, words));
  ASSERT_EQ(1, words.size());

  // word len = 85;
  const char *word_len_85 = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz1234567";
  words.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, word_len_85, std::strlen(word_len_85), doc_length, words));
  ASSERT_EQ(0, words.size());
}

class ObTestNgramFTParseHelper : public ::testing::Test
{
public:
  static const char *name_;
  static const char *properties_;
  static const int64_t TEST_WORD_COUNT = 27;
  typedef common::hash::ObHashMap<ObFTWord, int64_t> ObFTWordMap;
public:
  ObTestNgramFTParseHelper();
  virtual ~ObTestNgramFTParseHelper() = default;
  static int64_t get_word_count() { return TEST_WORD_COUNT; }

  virtual void SetUp() override;
  virtual void TearDown() override;

private:
  const common::ObString parser_name_;
  const common::ObString parser_properties_;
  const char *ngram_words_[TEST_WORD_COUNT];
  common::ObObjMeta meta_;
  common::ObArenaAllocator allocator_;
  ObFTParseHelper parse_helper_;
};

const char *ObTestNgramFTParseHelper::name_ = "ngram.1";
const char *ObTestNgramFTParseHelper::properties_ = "{\"min_token_size\":3,\"max_token_size\":84,\"stopword_table\":\"default\",\"dict_table\":\"none\",\"quanitfier_table\":\"none\",\"ngram_token_size\":2}";

ObTestNgramFTParseHelper::ObTestNgramFTParseHelper()
  : parser_name_(STRLEN(name_), name_),
    parser_properties_(STRLEN(properties_), properties_),
    ngram_words_{"oc", "ce", "ea", "an", "nb", "ba", "as", "se", "fu", "ul", "ll", "lt", "te", "ex",
                 "xt", "ar", "rc", "ch", "is", "no", "in", "th", "he", "wo", "or", "rl", "ld"},
    meta_(),
    allocator_()
{
  meta_.set_varchar();
  meta_.set_collation_type(ObCollationType::CS_TYPE_UTF8MB4_BIN);
}

void ObTestNgramFTParseHelper::SetUp()
{
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));
}

void ObTestNgramFTParseHelper::TearDown()
{
  parse_helper_.reset();
}

TEST_F(ObTestNgramFTParseHelper, test_parse_fulltext)
{
  ObFTWordMap words;
  ASSERT_EQ(OB_SUCCESS, words.create(10, "TestParse"));
  int64_t doc_length = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          words));

  ASSERT_EQ(get_word_count(), words.size());
  for (int64_t i = 0; i < words.size(); ++i) {
    int64_t word_cnt = 0;
    ObFTWord word(strlen(ngram_words_[i]), ngram_words_[i], meta_);
    ASSERT_EQ(OB_SUCCESS, words.get_refactored(word, word_cnt));
    ASSERT_TRUE(word_cnt >= 1);
  }

  ObFTWordMap ft_word_map;
  ASSERT_EQ(OB_SUCCESS, ft_word_map.create(10, "TestParse"));
  ASSERT_EQ(
      OB_SUCCESS,
      segment_and_calc_word_count(allocator_, &parse_helper_, meta_, ObTestAddWord::TEST_FULLTEXT, ft_word_map));
  ASSERT_EQ(words.size(), ft_word_map.size());

  words.clear();
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(meta_, nullptr, std::strlen(ObTestAddWord::TEST_FULLTEXT), doc_length, words));
  ASSERT_EQ(OB_INVALID_ARGUMENT, parse_helper_.segment(meta_, ObTestAddWord::TEST_FULLTEXT, 0, doc_length, words));
  ASSERT_EQ(OB_INVALID_ARGUMENT, parse_helper_.segment(meta_, ObTestAddWord::TEST_FULLTEXT, -1, doc_length, words));

  parse_helper_.reset();
  words.clear();
  ASSERT_EQ(
      OB_NOT_INIT,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          words));

  ASSERT_EQ(OB_INVALID_ARGUMENT, parse_helper_.init(nullptr, parser_name_, parser_properties_));
  ASSERT_EQ(OB_INVALID_ARGUMENT, parse_helper_.init(&allocator_, ObString(), parser_properties_));

  const char *parser_name = "space.1";
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, common::ObString(STRLEN(parser_name), parser_name), parser_properties_));

  ObObjMeta meta_invalid;
  meta_invalid.set_varchar();
  meta_invalid.set_collation_type(CS_TYPE_INVALID);
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(
          meta_invalid,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          words));

  ObObjMeta meta_pinyinbm;
  meta_pinyinbm.set_varchar();
  meta_pinyinbm.set_collation_type(CS_TYPE_PINYIN_BEGIN_MARK);
  ASSERT_EQ(
      OB_INVALID_ARGUMENT,
      parse_helper_.segment(
          meta_pinyinbm,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          words));

  ASSERT_EQ(OB_INIT_TWICE, parse_helper_.init(&allocator_, parser_name_, parser_properties_));

  parse_helper_.reset();
  words.clear();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));

  parse_helper_.reset();
  ASSERT_EQ(OB_SUCCESS, parse_helper_.init(&allocator_, parser_name_, parser_properties_));
  ASSERT_EQ(
      OB_SUCCESS,
      parse_helper_.segment(
          meta_,
          ObTestAddWord::TEST_FULLTEXT,
          std::strlen(ObTestAddWord::TEST_FULLTEXT),
          doc_length,
          words));
  ASSERT_EQ(get_word_count(), words.size());
  for (int64_t i = 0; i < words.size(); ++i) {
    int64_t word_cnt = 0;
    ObFTWord word(strlen(ngram_words_[i]), ngram_words_[i], meta_);
    ASSERT_EQ(OB_SUCCESS, words.get_refactored(word, word_cnt));
    ASSERT_TRUE(word_cnt >= 1);
  }
}

TEST_F(ObTestNgramFTParseHelper, test_parse_corner_case)
{
  ObFTWordMap words;
  ASSERT_EQ(OB_SUCCESS, words.create(10, "TParseCorner"));
  int64_t doc_length = 0;

  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, "f", std::strlen("f"), doc_length, words));
  ASSERT_EQ(0, words.size());
  ASSERT_EQ(0, doc_length);

  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, " f", std::strlen(" f"), doc_length, words));
  ASSERT_EQ(0, words.size());
  ASSERT_EQ(0, doc_length);

  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, " f ", std::strlen(" f "), doc_length, words));
  ASSERT_EQ(0, words.size());
  ASSERT_EQ(0, doc_length);

  ASSERT_EQ(OB_SUCCESS, parse_helper_.segment(meta_, "192.168.2.3", std::strlen("192.168.2.3"), doc_length, words));
  ASSERT_EQ(4, words.size());
  ASSERT_EQ(4, doc_length);
  int64_t word_cnt = 0;
  ObFTWord word_19(strlen("19"), "19", meta_);
  ASSERT_EQ(OB_SUCCESS, words.get_refactored(word_19, word_cnt));
  ASSERT_EQ(1, word_cnt);
  ObFTWord word_92(strlen("92"), "92", meta_);
  ASSERT_EQ(OB_SUCCESS, words.get_refactored(word_92, word_cnt));
  ASSERT_EQ(1, word_cnt);
  ObFTWord word_16(strlen("16"), "16", meta_);
  ASSERT_EQ(OB_SUCCESS, words.get_refactored(word_16, word_cnt));
  ASSERT_EQ(1, word_cnt);
  ObFTWord word_68(strlen("68"), "68", meta_);
  ASSERT_EQ(OB_SUCCESS, words.get_refactored(word_68, word_cnt));
  ASSERT_EQ(1, word_cnt);
}

TEST(ObTestNgramImpl, test_ngram_impl)
{
  ObFTNgramImpl ngram_impl;
  ObString fulltext = ObString::make_string("tt ad-ef gh_ij");
  ngram_impl.init(common::ObCharset::get_charset(ObCollationType::CS_TYPE_UTF8MB4_BIN),
                  fulltext.ptr_,
                  fulltext.length(),
                  2,
                  3);
  const char *word;
  int64_t word_len;
  int64_t char_cnt;
  int64_t word_freq;

  std::vector<std::string> expected_words
      = {"tt", "ad", "ef", "gh", "gh_", "h_", "h_i", "_i", "_ij", "ij"};
  std::vector<std::string> iter_words;
  while (ngram_impl.get_next_token(word, word_len, char_cnt, word_freq) != OB_ITER_END) {
    iter_words.push_back(std::string(word, word_len));
  }
  ASSERT_EQ(expected_words, iter_words);
}

} // end namespace storage
} // end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_fts_parser_helper.log");
  OB_LOGGER.set_file_name("test_fts_parser_helper.log", true);
  OB_LOGGER.set_log_level("DEBUG");
  testing::InitGoogleTest(&argc, argv);
  const int init_ret = oceanbase::storage::ObFTParseData::init_global();
  if (oceanbase::OB_SUCCESS != init_ret) {
    return init_ret;
  }
  const int test_ret = RUN_ALL_TESTS();
  oceanbase::storage::ObFTParseData::deinit_global();
  return test_ret;
}
