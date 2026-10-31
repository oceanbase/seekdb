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

#include <gtest/gtest.h>
#include <cstring>
#include "data_plane/fts/ob_fts_parser_helper.h"
#include "storage/fts/ik/ob_ik_char_util.h"
#include "storage/fts/ob_fts_parser_property.h"

namespace oceanbase
{
namespace storage
{

using ObFTCharUtil::CharType;

// The IK segmenters classify every character of the document and then run a
// few more block predicates on it. The predicates below pin down the expected
// classification of the characters, so that a change of the classification
// code can not silently change the produced tokens.
class TestFtsCharClassify : public ::testing::Test
{
protected:
  static CharType classify(const char *str, const int64_t len)
  {
    CharType type = CharType::USELESS;
    EXPECT_EQ(OB_SUCCESS,
              ObFTCharUtil::classify_first_char(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                str,
                                                static_cast<uint8_t>(len),
                                                type));
    return type;
  }
};

TEST_F(TestFtsCharClassify, ascii_characters)
{
  EXPECT_EQ(CharType::ENGLISH_LETTER, classify("a", 1));
  EXPECT_EQ(CharType::ENGLISH_LETTER, classify("Z", 1));
  EXPECT_EQ(CharType::ARABIC_LETTER, classify("0", 1));
  EXPECT_EQ(CharType::ARABIC_LETTER, classify("9", 1));
  EXPECT_EQ(CharType::USELESS, classify(" ", 1));
  EXPECT_EQ(CharType::USELESS, classify(",", 1));
  EXPECT_EQ(CharType::USELESS, classify("~", 1));
}

TEST_F(TestFtsCharClassify, cjk_characters)
{
  // U+4E2D
  EXPECT_EQ(CharType::CHINESE, classify("\xe4\xb8\xad", 3));
  // U+4E00
  EXPECT_EQ(CharType::CHINESE, classify("\xe4\xb8\x80", 3));
  // U+3042, hiragana
  EXPECT_EQ(CharType::OTHER_CJK, classify("\xe3\x81\x82", 3));
  // U+D55C, hangul
  EXPECT_EQ(CharType::OTHER_CJK, classify("\xed\x95\x9c", 3));
}

TEST_F(TestFtsCharClassify, full_width_characters)
{
  // U+FF21, full width 'A'
  EXPECT_EQ(CharType::ENGLISH_LETTER, classify("\xef\xbc\xa1", 3));
  // U+FF11, full width '1'
  EXPECT_EQ(CharType::ARABIC_LETTER, classify("\xef\xbc\x91", 3));
}

TEST_F(TestFtsCharClassify, cn_number_predicate)
{
  const char *numbers[] = {"一", "二", "三", "四", "五", "六", "七", "八",
                           "九", "十", "百", "千", "万", "亿", "零"};
  for (const char *number : numbers) {
    bool is_cn_number = false;
    EXPECT_EQ(OB_SUCCESS,
              ObFTCharUtil::check_cn_number(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                            number,
                                            static_cast<uint8_t>(strlen(number)),
                                            is_cn_number));
    EXPECT_TRUE(is_cn_number) << number;
  }

  bool is_cn_number = true;
  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::check_cn_number(ObCollationType::CS_TYPE_UTF8MB4_BIN, "a", 1, is_cn_number));
  EXPECT_FALSE(is_cn_number);
}

TEST_F(TestFtsCharClassify, connector_predicates)
{
  bool is_connector = false;
  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::check_num_connector(ObCollationType::CS_TYPE_UTF8MB4_BIN, ",", 1, is_connector));
  EXPECT_TRUE(is_connector);

  is_connector = true;
  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::check_num_connector(ObCollationType::CS_TYPE_UTF8MB4_BIN, "a", 1, is_connector));
  EXPECT_FALSE(is_connector);

  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::check_letter_connector(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                 "#",
                                                 1,
                                                 is_connector));
  EXPECT_TRUE(is_connector);

  is_connector = true;
  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::check_letter_connector(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                 "a",
                                                 1,
                                                 is_connector));
  EXPECT_FALSE(is_connector);
}

TEST_F(TestFtsCharClassify, ignore_single_cjk_predicate)
{
  bool is_ignore = false;
  // U+FF0C, full width comma, it is dropped when it is output alone
  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::is_ignore_single_cjk(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                               "\xef\xbc\x8c",
                                               3,
                                               is_ignore));
  EXPECT_TRUE(is_ignore);

  is_ignore = true;
  // U+4E2D
  EXPECT_EQ(OB_SUCCESS,
            ObFTCharUtil::is_ignore_single_cjk(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                               "\xe4\xb8\xad",
                                               3,
                                               is_ignore));
  EXPECT_FALSE(is_ignore);
}

// The properties of a parser are parsed from a json string and the result is
// cached. Make sure that the cached result is the same as the parsed one.
class TestFtsParserProperty : public ::testing::Test
{
protected:
  static ObFTParser make_ik_parser()
  {
    ObFTParser parser;
    EXPECT_EQ(OB_SUCCESS, parser.init(ObString("ik")));
    return parser;
  }
};

TEST_F(TestFtsParserProperty, ik_properties)
{
  ObFTParser parser = make_ik_parser();
  ObFTParserProperty property;
  ObString props("{\"ik_mode\":\"SMART\"}");
  ASSERT_EQ(OB_SUCCESS, property.parse_for_parser_helper(parser, props));
  EXPECT_TRUE(property.ik_mode_smart_);
  EXPECT_EQ(ObString(ObFTSLiteral::CONFIG_NAME_DICT_TABLE), property.dict_table_);
  EXPECT_EQ(ObString(ObFTSLiteral::CONFIG_NAME_STOPWORD_TABLE), property.stopword_table_);
  EXPECT_EQ(ObString(ObFTSLiteral::CONFIG_NAME_QUANTIFIER_TABLE), property.quantifier_table_);

  // The second call is served by the cache and has to return the same result.
  ObFTParserProperty cached_property;
  ASSERT_EQ(OB_SUCCESS, cached_property.parse_for_parser_helper(parser, props));
  EXPECT_TRUE(cached_property.is_equal(property));
}

TEST_F(TestFtsParserProperty, ik_max_word_mode)
{
  ObFTParser parser = make_ik_parser();
  ObFTParserProperty property;
  ObString props("{\"ik_mode\":\"MAX_WORD\"}");
  ASSERT_EQ(OB_SUCCESS, property.parse_for_parser_helper(parser, props));
  EXPECT_FALSE(property.ik_mode_smart_);
}

} // namespace storage
} // namespace oceanbase
