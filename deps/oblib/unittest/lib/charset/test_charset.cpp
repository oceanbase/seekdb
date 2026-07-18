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

#include <codecvt>
#include "gtest/gtest.h"
#define protected public
#define private public

#include "lib/allocator/page_arena.h"
#include "lib/utility/data_buffer.h"
#include "lib/charset/ob_charset_string_helper.h"
#define USING_LOG_PREFIX SQL

using namespace oceanbase::common;

class TestCharset: public ::testing::Test
{
public:
  TestCharset();
  virtual ~TestCharset();
  virtual void SetUp();
  virtual void TearDown();
protected:
  void gen_random_unicode_string(const int len, char *res, int &real_len);
  int random_range(const int low, const int high);
};

TestCharset::TestCharset()
{
}

TestCharset::~TestCharset()
{
}

void TestCharset::SetUp()
{
  srand((unsigned)time(NULL ));
}

void TestCharset::TearDown()
{
}

int TestCharset::random_range(const int low, const int high)
{
  return std::rand() % (high - low) + low;
}

void TestCharset::gen_random_unicode_string(const int len, char *res, int &real_len)
{
  int i = 0;
  int unicode_point = 0;
  std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
#ifdef __APPLE__
  auto is_valid_scalar = [](int code_point) {
    return code_point >= 0 && code_point <= 0x10FFFF &&
           !(code_point >= 0xD800 && code_point <= 0xDFFF);
  };
#endif
  for (i = 0; i < len; ) {
    const int bytes = random_range(1, 7);
#ifdef __APPLE__
    do {
      if (bytes < 4) {
        unicode_point = random_range(0, 127);
      } else if (bytes < 6) {
        unicode_point = random_range(0xFF, 0xFFFF);
      } else if (bytes < 7) {
        unicode_point = random_range(0XFFFF, 0X10FFFF);
      }
    } while (!is_valid_scalar(unicode_point));
#else
    if (bytes < 4) {
      unicode_point = random_range(0, 127);
    } else if (bytes < 6) {
      unicode_point = random_range(0xFF, 0xFFFF);
    } else if (bytes < 7) {
      unicode_point = random_range(0XFFFF, 0X10FFFF);
    }
#endif
    std::string utf_str = converter.to_bytes(unicode_point);
    //fprintf(stdout, "code_point=%d\n", unicode_point);
    //fprintf(stdout, "utf8_str=%s\n", utf_str.c_str());
    for (int j = 0; j < utf_str.size(); ++j) {
      res[i++] = utf_str[j];
    }
  }
  real_len = i;
}

TEST_F(TestCharset, strcmp)
{
  ObString a;
  ObString b;
  int ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, a.ptr(), a.length(), b.ptr(), b.length());
  fprintf(stdout, "ret:%d\n", ret);
  ASSERT_EQ(0, ret);
  char aa[10] = "abd";
  char bb[10] = "aBd ";
  char cc[10] = " aBd";
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, aa, 3, bb, 4);
  fprintf(stdout, "ret:%d\n", ret);
  ASSERT_EQ(-1, ret);
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, aa, 3, cc, 4);
  fprintf(stdout, "ret:%d\n", ret);
  ASSERT_EQ(1, ret);
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_BIN, aa, 3, bb, 4);
  fprintf(stdout, "ret:%d\n", ret);
  ASSERT_TRUE(ret > 0);
  ObString c(aa);
  ObString d(bb);
  fprintf(stdout, "c:%.*s\n", c.length(), c.ptr());
  fprintf(stdout, "d:%.*s\n", d.length(), d.ptr());
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, c, d);
  fprintf(stdout, "ret:%d\n", ret);
  ASSERT_EQ(-1, ret);
  fprintf(stdout, "ret:%d\n", ret);
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_BIN, c, d);
  fprintf(stdout, "ret:%d\n", ret);
  ASSERT_TRUE(ret > 0);
  ObString empty;
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, empty, d);
  ASSERT_EQ(-1, ret);
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, d, empty);
  ASSERT_EQ(1, ret);
  ObString empty1;
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI, empty1, empty);
  ASSERT_EQ(0, ret);
  ret = ObCharset::strcmp(CS_TYPE_UTF8MB4_BIN, empty1, empty);
  ASSERT_EQ(0, ret);
}

TEST_F(TestCharset, sortkey)
{
  char aa[10] = "abc";
  char aa1[10];
  char bb[10] = "abc ";
  char bb1[10];
  bool is_valid_unicode = false;
  size_t size1 = ObCharset::sortkey(CS_TYPE_UTF8MB4_GENERAL_CI, aa, strlen(aa), aa1, 10, is_valid_unicode);
  size_t size2 = ObCharset::sortkey(CS_TYPE_UTF8MB4_GENERAL_CI, bb, strlen(bb), bb1, 10, is_valid_unicode);
  ASSERT_NE(size1, size2);
  ASSERT_TRUE(is_valid_unicode);

  char space[10] = "  ";
  size1 = ObCharset::sortkey(CS_TYPE_UTF8MB4_GENERAL_CI, space, strlen(space), aa1, 10, is_valid_unicode);
  ASSERT_EQ(size1, 4);
  ASSERT_TRUE(is_valid_unicode);

  char empty[10] = "";
  size1 = ObCharset::sortkey(CS_TYPE_UTF8MB4_GENERAL_CI, empty, strlen(empty), aa1, 10, is_valid_unicode);
  ASSERT_EQ(size1, 0);
  ASSERT_TRUE(is_valid_unicode);

  char invalid[10];
  invalid[0] = char(0x10);
  invalid[1] = char(0x80);
  invalid[2] = '\0';
  size1 = ObCharset::sortkey(CS_TYPE_UTF8MB4_GENERAL_CI, invalid, strlen(invalid), aa1, 10, is_valid_unicode);
  ASSERT_EQ(size1, 2);
  ASSERT_FALSE(is_valid_unicode);
  
  const char ascii_string[] = {'\x7f','\0'};
  const char non_ascii_string[] = {'\xff','\0'};
  const char utf8_string[] = { '\xe4', '\xbd', '\xa0', '\xe5', '\xa5', '\xbd','\0'};//meaning is '你好'
  struct SortkeyCase {
    ObCollationType type;
    const char *valid_str;
    const char *invalid_str;
    int64_t valid_size;
    bool valid_unicode;
    int64_t invalid_size;
    bool invalid_unicode;
  };
  const SortkeyCase cases[] = {
    {CS_TYPE_UTF8MB4_GENERAL_CI, utf8_string, non_ascii_string, 4, true, 0, false},
    {CS_TYPE_UTF8MB4_BIN, utf8_string, non_ascii_string, 6, true, 0, false},
    {CS_TYPE_BINARY, ascii_string, non_ascii_string, 1, true, 1, true},
  };
  for (const auto &test_case : cases) {
    ASSERT_TRUE(ObCharset::is_valid_collation(test_case.type));
    size1 = ObCharset::sortkey(test_case.type, test_case.valid_str, strlen(test_case.valid_str),
                               aa1, 10, is_valid_unicode);
    ASSERT_EQ(test_case.valid_size, size1);
    ASSERT_EQ(test_case.valid_unicode, is_valid_unicode);

    size1 = ObCharset::sortkey(test_case.type, test_case.invalid_str, strlen(test_case.invalid_str),
                               aa1, 10, is_valid_unicode);
    ASSERT_EQ(test_case.invalid_size, size1);
    ASSERT_EQ(test_case.invalid_unicode, is_valid_unicode);
  }
  // The parameter of sortkey cannot be NULL
  //char *p = NULL;
  //size1 = ObCharset::sortkey(CS_TYPE_UTF8MB4_GENERAL_CI, true, p, 0, aa1, 10);
}

TEST_F(TestCharset, casedn)
{
  char a1[14] = "Variable_name";
  char a2[14] = "Variable_NAME";
  char a3[14] = "variable_name";
  ObString y1;
  ObString y2;
  ObString y3;
  a1[13] = '1';
  a2[13] = '1';
  a3[13] = '1';
  y1.assign_ptr(a1, 14);
  y2.assign_ptr(a2, 14);
  y3.assign_ptr(a3, 14);
  fprintf(stdout, "ret:%p, %d\n", y1.ptr(), y1.length() );
  size_t size1 = ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, y1);
  EXPECT_TRUE(y1 == y3);
  size_t size2 = ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, y2);
  fprintf(stdout, "y1:%.*s, y2:%.*s, y3:%.*s\n", y1.length(), y1.ptr(), y2.length(), y2.ptr(), y3.length(), y3.ptr());
  EXPECT_TRUE(y2 == y3);
  ASSERT_EQ(y1.length(), 14);
  ASSERT_EQ(y2.length(), 14);
  ASSERT_EQ(size1, 14);
  ASSERT_EQ(size2, 14);
}

TEST_F(TestCharset, case_insensitive_equal)
{
  ObString y1= "Variable_name";
  ObString y2= "variable_name";
  ObString y3= "variable_name1";
  ObString y4= "variable_name1";
  bool yy = ObCharset::case_insensitive_equal(y1, y2, CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_TRUE(yy);
  yy = ObCharset::case_insensitive_equal(y2, y3, CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_FALSE(yy);
  yy = ObCharset::case_insensitive_equal(y3, y4, CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_TRUE(yy);
}

TEST_F(TestCharset, hash_sort)
{
  ObString s;
  uint64_t ret = ObCharset::hash(CS_TYPE_UTF8MB4_GENERAL_CI, s.ptr(), s.length(), 0);
  const char *a = "abd";
  const char *b = "aBD";
  uint64_t ret1 = ObCharset::hash(CS_TYPE_UTF8MB4_GENERAL_CI, a, 3, 0);
  uint64_t ret2 = ObCharset::hash(CS_TYPE_UTF8MB4_GENERAL_CI, b, 3, 0);
  fprintf(stdout, "ret:%lu, ret1:%lu, ret2:%lu\n", ret, ret1, ret2);
  //uint64_t ret3 = ObCharset::hash(CS_TYPE_UTF8MB4_GENERAL_CI, ObString::make_string(b));
  ASSERT_EQ(ret1, ret2);
}

TEST_F(TestCharset, case_mode_equal)
{
  ObString y1= "Variable_name";
  ObString y2= "variable_name";
  ObString y3= "variable_name1";
  ObString y4= "variable_name1";
  bool is_equal = false;
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_SENSITIVE, y1, y2);
  ASSERT_FALSE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_SENSITIVE, y1, y1);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_SENSITIVE, y3, y4);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_SENSITIVE, y1, y3);
  ASSERT_FALSE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_INSENSITIVE, y1, y2);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_INSENSITIVE, y1, y1);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_INSENSITIVE, y3, y4);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_ORIGIN_AND_INSENSITIVE, y1, y3);
  ASSERT_FALSE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_LOWERCASE_AND_INSENSITIVE, y1, y2);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_LOWERCASE_AND_INSENSITIVE, y1, y1);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_LOWERCASE_AND_INSENSITIVE, y3, y4);
  ASSERT_TRUE(is_equal);
  is_equal = ObCharset::case_mode_equal(OB_LOWERCASE_AND_INSENSITIVE, y1, y3);
  ASSERT_FALSE(is_equal);
}

TEST_F(TestCharset, well_formed_length)
{
  int ret = OB_SUCCESS;
  const char *str = "\0123";
  ObCollationType cs_type =  CS_TYPE_UTF8MB4_GENERAL_CI;
  int64_t well_formed_length = 0;
  int64_t str_len = 1;

  ret = ObCharset::well_formed_len(cs_type, str, str_len, well_formed_length);
  ASSERT_TRUE(OB_SUCC(ret));
  ASSERT_TRUE(1 == well_formed_length);
  ret = ObCharset::well_formed_len(cs_type, str, 0, well_formed_length);
  ASSERT_TRUE(OB_SUCC(ret));
  ASSERT_TRUE(0 == well_formed_length);
  ret = ObCharset::well_formed_len(cs_type, NULL, 0, well_formed_length);
  ASSERT_TRUE(OB_SUCC(ret));
  ASSERT_TRUE(0 == well_formed_length);
  ret = ObCharset::well_formed_len(cs_type, NULL, str_len, well_formed_length);
  ASSERT_TRUE(OB_INVALID_ARGUMENT == ret);
}

TEST_F(TestCharset, test_max_byte_char_pos)
{
  int ret = OB_SUCCESS;
  const ObCollationType types[] = {CS_TYPE_BINARY, CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_BIN};
  for (int64_t i = 0; OB_SUCC(ret) && i < sizeof(types) / sizeof(ObCollationType); ++i) {
    int real_len = 0;
    int64_t char_len = 0;
    char buf[25600];
    gen_random_unicode_string(25500, buf, real_len);
    std::cout << "real_len" << real_len << std::endl;
    int64_t left_bytes = real_len;
    const int64_t block_size = 16000;
    char *pos = buf;
    while (left_bytes > 0) {
      int64_t well_formed_len = 0;
      int32_t well_formed_error = 0;
      int64_t calc_char_len = 0;
      const int64_t write_bytes = std::min(left_bytes, block_size);
      const int64_t real_bytes = ObCharset::max_bytes_charpos(types[i], pos, left_bytes, write_bytes, char_len);
      std::cout << "real_bytes" << real_bytes << std::endl;
      ASSERT_TRUE(real_bytes <= 16000);
      ret = ObCharset::well_formed_len(types[i], pos, real_bytes, well_formed_len, well_formed_error);
      ASSERT_EQ(OB_SUCCESS, ret);
      ASSERT_EQ(real_bytes, well_formed_len);
      ASSERT_EQ(0, well_formed_error);
      calc_char_len = ObCharset::strlen_char(types[i], pos, real_bytes);
      ASSERT_EQ(calc_char_len, char_len);
      left_bytes -= real_bytes;
      pos += real_bytes;
    }
  }
}

TEST_F(TestCharset, test_ascii_list_for_all_charset)
{
  const int64_t buf_len = 100;
  char buf[buf_len] = {0};

  const int64_t chunk_size = 8192;
  char chunk[chunk_size] = {0};
  ObDataBuffer allocator(chunk, chunk_size);

  std::cout<< "ascii";
  for (int cs_i = CHARSET_INVALID; cs_i < CHARSET_MAX; ++cs_i) {
    auto charset_type = static_cast<ObCharsetType>(cs_i);
    if (!ObCharset::is_valid_charset(charset_type))
      continue;
    ObCollationType cs_type = ObCharset::get_default_collation(charset_type);
    ASSERT_TRUE(ObCharset::is_valid_collation(cs_type));
    std::cout << "\t" << ObCharset::charset_name(cs_type);
  }
  std::cout << std::endl;

  for (int ascii_wc = 0; ascii_wc <= INT8_MAX; ascii_wc++) {
    std::cout<< ascii_wc;
    for (int cs_i = CHARSET_INVALID; cs_i < CHARSET_MAX; ++cs_i) {
      auto charset_type = static_cast<ObCharsetType>(cs_i);
      if (!ObCharset::is_valid_charset(charset_type))
        continue;
      ObCollationType cs_type = ObCharset::get_default_collation(charset_type);
      ASSERT_TRUE(ObCharset::is_valid_collation(cs_type));
      int64_t result_len = 0;
      ObString str = ObCharsetUtils::get_const_str(cs_type, ascii_wc);
      ASSERT_EQ (OB_SUCCESS, hex_print(str.ptr(), str.length(), buf, buf_len, result_len));
      buf[result_len] = '\0';
      std::cout <<"\t" << buf;
    }

    std::cout << std::endl;
  }

}

TEST_F(TestCharset, tolower)
{
  ObArenaAllocator allocator;
  char a1[] = "Variable_name";
  char a2[] = "Variable_NAME";
  char a3[] = "variable_name";
  ObString y1;
  ObString y2;
  ObString y3;
  y1.assign_ptr(a1, static_cast<int64_t>(strlen(a1)));
  y2.assign_ptr(a2, static_cast<int64_t>(strlen(a2)));
  y3.assign_ptr(a3, static_cast<int64_t>(strlen(a3)));
  fprintf(stdout, "ret:%p, %d\n", y1.ptr(), y1.length() );
  for (int cs_i = CHARSET_INVALID; cs_i < CHARSET_MAX; ++cs_i) {
    auto charset_type = static_cast<ObCharsetType>(cs_i);
    if (!ObCharset::is_valid_charset(charset_type) || CHARSET_UTF16 == charset_type
        || CHARSET_UTF16LE == charset_type || CHARSET_BINARY == charset_type)
      continue;
    ObCollationType cs_type = ObCharset::get_default_collation(charset_type);
    ASSERT_TRUE(ObCharset::is_valid_collation(cs_type));
    const char *cs_name = ObCharset::charset_name(cs_type);

    ObString y1_res;
    ASSERT_TRUE(OB_SUCCESS == ObCharset::tolower(cs_type, y1, y1_res, allocator));
    fprintf(stdout, "charset=%s, src:%.*s, src_lower:%.*s, dst:%.*s\n", cs_name,
            y1.length(), y1.ptr(), y1_res.length(), y1_res.ptr(), y3.length(), y3.ptr());
    EXPECT_TRUE(y1_res == y3);
    ObString y2_res;
    ASSERT_TRUE(OB_SUCCESS == ObCharset::tolower(cs_type, y2, y2_res, allocator));
    fprintf(stdout, "charset=%s, src:%.*s, src_lower:%.*s, dst:%.*s\n", cs_name,
            y2.length(), y2.ptr(), y2_res.length(), y2_res.ptr(), y3.length(), y3.ptr());
    EXPECT_TRUE(y2_res == y3);
  }
}


TEST_F(TestCharset, toupper)
{
  ObArenaAllocator allocator;
  char a1[] = "Variable_name";
  char a2[] = "Variable_NAME";
  char a3[] = "VARIABLE_NAME";
  ObString y1;
  ObString y2;
  ObString y3;
  y1.assign_ptr(a1, static_cast<int64_t>(strlen(a1)));
  y2.assign_ptr(a2, static_cast<int64_t>(strlen(a2)));
  y3.assign_ptr(a3, static_cast<int64_t>(strlen(a3)));
  fprintf(stdout, "ret:%p, %d\n", y1.ptr(), y1.length() );
  for (int cs_i = CHARSET_INVALID; cs_i < CHARSET_MAX; ++cs_i) {
    auto charset_type = static_cast<ObCharsetType>(cs_i);
    if (!ObCharset::is_valid_charset(charset_type) || CHARSET_UTF16 == charset_type 
    || CHARSET_UTF16LE == charset_type || CHARSET_BINARY == charset_type)
      continue;
    ObCollationType cs_type = ObCharset::get_default_collation(charset_type);
    ASSERT_TRUE(ObCharset::is_valid_collation(cs_type));
    const char *cs_name = ObCharset::charset_name(cs_type);

    ObString y1_res;
    ASSERT_TRUE(OB_SUCCESS == ObCharset::toupper(cs_type, y1, y1_res, allocator));
    fprintf(stdout, "charset=%s, src:%.*s, src_upper:%.*s, dst:%.*s\n", cs_name,
            y1.length(), y1.ptr(), y1_res.length(), y1_res.ptr(), y3.length(), y3.ptr());
    EXPECT_TRUE(y1_res == y3);
    ObString y2_res;
    ASSERT_TRUE(OB_SUCCESS == ObCharset::toupper(cs_type, y2, y2_res, allocator));
    fprintf(stdout, "charset=%s, src:%.*s, src_upper:%.*s, dst:%.*s\n", cs_name,
            y2.length(), y2.ptr(), y2_res.length(), y2_res.ptr(), y3.length(), y3.ptr());
    EXPECT_TRUE(y2_res == y3);
  }
}

TEST_F(TestCharset, check_mbmaxlenlen)
{
  for (int64_t type = ObCollationType::CS_TYPE_INVALID; type < ObCollationType::CS_TYPE_MAX; ++type) {
    if (nullptr != ObCharset::charset_arr[type]) {
      const uint mbmaxlenlen = ob_mbmaxlenlen(ObCharset::charset_arr[type]);
      const char *cs_name = ObCharset::charset_name(static_cast<ObCollationType>(type));
      std::cout << "charset=" << cs_name << ", mbmaxlenlen=" << mbmaxlenlen << ", type=" << type << std::endl;
      ASSERT_EQ(1, mbmaxlenlen);
    }
  }
}

std::vector<const char *> test_strings = {"1", "abcdef", "ab1dc4", "好", "b今a天", "1abad    "};


TEST_F(TestCharset, basic_collation_handler_test)
{
  ObArenaAllocator alloc;
  for (int i = CS_TYPE_INVALID; i < CS_TYPE_MAX; i++) {
    ObCollationType coll = static_cast<ObCollationType>(i);
    if (!ObCharset::is_valid_collation(coll)) {
      continue;
    }
    const ObCharsetInfo * cs = ObCharset::get_charset(coll);
      const char *coll_name = ObCharset::collation_name(coll);
    if (OB_NOT_NULL(cs)) {
      std::cout << "#TEST Coll = " << coll_name << std::endl;
      for (const char* utf8_str:test_strings) {
        ObString dst;
        if (cs->mbmaxlen <= 1) {
          dst = ObString(utf8_str);
        } else {
          ASSERT_EQ(0, ObCharset::charset_convert(alloc, ObString(utf8_str), CS_TYPE_UTF8MB4_BIN, coll, dst));
        }
        char*str = dst.ptr();
        char*end = dst.ptr() + dst.length();

        if (OB_NOT_NULL(cs->coll->strnncoll)) {
          fprintf(stdout, ">> strnncoll = %d for text = \"%s\"\n",
                    cs->coll->strnncoll(cs, pointer_cast<const uchar*>(str), end-str, pointer_cast<const uchar*>(str), end-str, true), utf8_str);
        }
        if (OB_NOT_NULL(cs->coll->strnncollsp)) {
          fprintf(stdout, ">> strnncollsp = %d for text = \"%s\"\n",
                    cs->coll->strnncollsp(cs, pointer_cast<const uchar*>(str), end-str, pointer_cast<const uchar*>(str), end-str, true), utf8_str);
        }
        if (OB_NOT_NULL(cs->coll->strnxfrm)) {
          char temp[100];
          bool is_valid_unicode = false;
          fprintf(stdout, ">> strnxfrm = %ld for text = \"%s\", is_valid_unicode = %d\n",
                    cs->coll->strnxfrm(cs, reinterpret_cast<unsigned char *>(temp), 100, UINT32_MAX,
                    reinterpret_cast<unsigned char *>(str), end-str, 0, &is_valid_unicode), utf8_str, is_valid_unicode);
        }
        if (OB_NOT_NULL(cs->coll->strnxfrmlen)) {
          fprintf(stdout, ">> strnxfrmlen = %ld for text = \"%s\"\n",
                    cs->coll->strnxfrmlen(cs, end-str), utf8_str);
        }
        if (OB_NOT_NULL(cs->coll->strnxfrm_varlen)) {
          fprintf(stdout, ">> strnxfrmlen = %ld for text = \"%s\"\n",
                    cs->coll->strnxfrmlen(cs, end-str), utf8_str);
        }
        if (OB_NOT_NULL(cs->coll->like_range)) {
          char temp1[100];
          char temp2[100];
          size_t len1, len2, prefix_len;
          fprintf(stdout, ">> like_range = %d for text = \"%s\", min = %.*s, max = %.*s\n",
                    cs->coll->like_range(cs, str, end-str, '\\', '_', '%', 100, temp1, temp2, &len1, &len2, &prefix_len), utf8_str,
                    (int)len1, temp1, (int)len2, temp2);
        }
        if (OB_NOT_NULL(cs->coll->wildcmp)) {
          const char *wild_str = "%";
          fprintf(stdout, ">> wildcmp = %d for text = \"%s\"\n",
                    cs->coll->wildcmp(cs, str, end, wild_str, wild_str + strlen(wild_str), '\\', '_', '%'), utf8_str);
        }
        if (OB_NOT_NULL(cs->coll->strcasecmp)) {
          fprintf(stdout, ">> strcasecmp = %d for text = \"%s\"\n",
                    cs->coll->strcasecmp(cs, str, end), utf8_str);
        }
        if (OB_NOT_NULL(cs->coll->instr)) {
          ob_match_t m_match_t[2];
          unsigned int nmatch = 1;
          const char *temp = "1";
          fprintf(stdout, ">> instr = %d for text = \"%s\" nmatch = %u match_mb_len = %u\n",
                    cs->coll->instr(cs, temp, strlen(temp), str, end-str, m_match_t, nmatch), utf8_str,
                    nmatch, m_match_t[0].mb_len);
        }
        if (OB_NOT_NULL(cs->coll->hash_sort)) {
          ulong nr1, nr2;
          cs->coll->hash_sort(cs, pointer_cast<const uchar*>(str), end-str, &nr1, &nr2, true, NULL);
          fprintf(stdout, ">> hash_sort for text = \"%s\" nr1 = %lu nr1 = %lu\n", utf8_str, nr1, nr2);
        }
        if (OB_NOT_NULL(cs->coll->propagate)) {
          fprintf(stdout, ">> propagate = %d for text = \"%s\"\n",
                    cs->coll->propagate(cs, pointer_cast<const uchar*>(str), end-str), utf8_str);
        }
      }
    }
  }
}

TEST_F(TestCharset, foreach_char) {
  const char *data = "豫章故郡，洪都新府。星分翼轸，地接衡庐。襟三江而带五湖，控蛮荆而引瓯越。物华天宝，龙光射牛斗之墟"
               "人杰地灵，徐孺下陈蕃之榻。雄州雾列，俊采星驰。台隍枕夷夏之交，宾主尽东南之美。都督阎公之雅望，棨戟遥临"
               "宇文新州之懿范，襜帷暂驻。十旬休假，胜友如云；千里逢迎，高朋满座。腾蛟起凤，孟学士之词宗；紫电青霜"
               "王将军之武库。家君作宰，路出名区；童子何知，躬逢胜饯。时维九月，序属三秋。潦水尽而寒潭清，烟光凝而暮山紫"
               "俨骖騑于上路，访风景于崇阿。临帝子之长洲，得天人之旧馆。层峦耸翠，上出重霄；飞阁流丹，下临无地。鹤汀凫渚"
               "穷岛屿之萦回；桂殿兰宫，即冈峦之体势。披绣闼，俯雕甍，山原旷其盈视，川泽纡其骇瞩。闾阎扑地，钟鸣鼎食之家"
               "舸舰弥津，青雀黄龙之舳。云销雨霁，彩彻区明。落霞与孤鹜齐飞，秋水共长天一色。渔舟唱晚，响穷彭蠡之滨"
               "雁阵惊寒，声断衡阳之浦。遥襟甫畅，逸兴遄飞。爽籁发而清风生，纤歌凝而白云遏。睢园绿竹，气凌彭泽之樽"
               "邺水朱华，光照临川之笔。四美具，二难并。穷睇眄于中天，极娱游于暇日。天高地迥，觉宇宙之无穷；兴尽悲来"
               "识盈虚之有数。望长安于日下，目吴会于云间。地势极而南溟深，天柱高而北辰远。关山难越，谁悲失路之人"
               "萍水相逢，尽是他乡之客。怀帝阍而不见，奉宣室以何年？嗟乎！时运不齐，命途多舛。冯唐易老，李广难封"
               "屈贾谊于长沙，非无圣主；窜梁鸿于海曲，岂乏明时？所赖君子见机，达人知命。老当益壮，宁移白首之心"
               "穷且益坚，不坠青云之志。酌贪泉而觉爽，处涸辙以犹欢。北海虽赊，扶摇可接；东隅已逝，桑榆非晚。孟尝高洁"
               "空余报国之情；阮籍猖狂，岂效穷途之哭！勃，三尺微命，一介书生。无路请缨，等终军之弱冠；有怀投笔，慕宗悫之长风"
               "舍簪笏于百龄，奉晨昏于万里。非谢家之宝树，接孟氏之芳邻。他日趋庭，叨陪鲤对；今兹捧袂，喜托龙门。杨意不逢"
               "抚凌云而自惜；钟期既遇，奏流水以何惭？呜呼！胜地不常，盛筵难再；兰亭已矣，梓泽丘墟。临别赠言，幸承恩于伟饯"
               "登高作赋，是所望于群公。敢竭鄙怀，恭疏短引；一言均赋，四韵俱成。请洒潘江，各倾陆海云尔：滕王高阁临江渚"
               "佩玉鸣鸾罢歌舞。画栋朝飞南浦云，珠帘暮卷西山雨。闲云潭影日悠悠，物换星移几度秋。阁中帝子今何在？槛外长江空自流。";

       const char *data1 =   "豫章故郡，洪都新府。星分翼軒，地接衡廬。襟三江而帶五湖，控蠻荊而引甌越。物華天寶，龍光射牛斗之墟。落霞與孤鷺齊飛，秋水共長天一色。"
                              "人傑地靈，徐孺下陳蕃之榻。雄州霧列，俊採星馳。台隍枕夷夏之交，賓主盡東南之美。都督閻之雅望，棨戟遙臨"
                              "時維九月，序屬三秋。潦水盡而寒潭清，煙光凝而暮山紫。物華天寶";
       const char *data2 = "豫章故郡，洪都新府。星分翼轸，地接衡庐。"
                           "人杰地灵，徐孺下陈蕃之榻。";
       const char *data_kr = "한국의 사계절은 아름답습니다. 봄에는벚꽃이 피고, 여름에는 바다에서 수영을 합니다. 가을에는 단풍을 즐기며, 겨울에는 눈 내리는 풍경을 감상합니다."
                             "한국의 문화도 다양하고 풍부하며,찻집이나 축제 등 많은 전통 행사가 있습니다. 또한 한국 음식도 매우 다양하여 김치찌개, 불고기, 비빔밥 등이 인기가 많습니다."
                             "이러한 문화와 자연의 아름다움이 한국의 매력을 만들어냅니다.";
       const char *data_jp = "朝になったら, 二人目を合わせて""たわいないこと, 少し話ししたいな"
                              "晴れた午後は, そっと手を繋いで""穏やかな街を, 少し歩いてみたり"
                              "いつまでも同じ時間を""一緒に過ごしてたくて"
                              "だって, 朝も夜も""伝えたいこと, たくさんあって"
                              "今日も明日も""「きだ」なんて あぁ、言えたら";
       const char *data_ascii = "I hear America singing, the varied carols I hear,Those of mechanics, "
                                "each one singing his as it should be blithe and strong,The carpenter "
                                "singing his as he measures his plank or beam,The mason singing his as "
                                "he makes ready for work, or leaves off work,The boatman singing what "
                                "belongs to him in his boat, the deckhand singing on the steamboat deck,"
                                "The shoemaker singing as he sits on his bench, the hatter singing as "
                                "he stands,The wood-cutter's song, the ploughboy's on his way in the morning, "
                                "or at noon intermission or at sundown,The delicious singing of the mother, "
                                "or of the young wife at work, or of the girl sewing or washing,Each singing "
                                "what belongs to him or her and to none else,The day what belongs to the day-at "
                                "night the party of young fellows, robust, friendly,Singing with open "
                                "mouths their strong melodious songs.";
  int64_t word_cnt = 0;
  auto do_nothing = [&word_cnt] (const ObString &str, ob_wc_t wchar) -> int {
    int ret = OB_SUCCESS;
    word_cnt++;
    return ret;
  };

  int repeat = 10000;
  int64_t total_bytes = 0;
  int64_t time_start = 0;
  int64_t time_dur = 0;

  auto start_timer = [&]() { time_start = ObTimeUtility::current_time(); word_cnt = 0; };
  auto end_timer = [&]() {
    time_dur = ObTimeUtility::current_time() - time_start;
    fprintf(stdout, "==> speed:%ldM/s, word_cnt=>%ld\n", (total_bytes >> 20) * 1000000 / time_dur, word_cnt);
  };

  ObString data_in(data);
  ObString data_in1(data1);
  ObString data_in2(data2);
  ObString data_in_jp(data_jp);
  ObString data_in_kr(data_kr);
  ObString data_in_ascii(data_ascii);
  ObArenaAllocator alloc;

  for (int i = CHARSET_BINARY + 1; i <= CHARSET_GB18030; i++) {
    ObCharsetType test_cs_type = static_cast<ObCharsetType>(i);
    if (!ObCharset::is_valid_charset(test_cs_type)) {
      continue;
    }
    ObCollationType test_collation_type = ObCharset::get_default_collation(test_cs_type);
    ObString data_out;
    char *buf = NULL;
    buf = static_cast<char*>(alloc.alloc(data_in.length() * repeat));
    ASSERT_TRUE(NULL != buf);
    ObString input(data_in.length() * repeat, buf);
    for (int i = 0; i < repeat; i++) {
      MEMCPY(buf + i*data_in.length(), data_in.ptr(), data_in.length());
    }

    int32_t buf_len = input.length() * ObCharset::MAX_MB_LEN;
    buf = static_cast<char*>(alloc.alloc(buf_len));
    ASSERT_TRUE(NULL != buf);
    data_out.assign_buffer(buf, buf_len);

    total_bytes = input.length();
    fprintf(stdout, "\n# For charset: %s, ConvertDataLen: %d\n", ObCharset::charset_name(test_collation_type), input.length());


    uint32_t result_len;
    start_timer();
    ASSERT_EQ(OB_SUCCESS,
              ObCharset::charset_convert(CS_TYPE_UTF8MB4_BIN, input.ptr(), input.length(),
                                         test_collation_type, data_out.ptr(), data_out.size(),
                                         result_len));
    end_timer();

    start_timer();
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS,
              ObFastStringScanner::convert_charset(
                input, CS_TYPE_UTF8MB4_BIN, test_collation_type, buf, buf_len, pos));
    end_timer();
    fprintf(stdout, "input.length = %d, data_out.length = %ld\n", input.length(), pos);
  }


  for (int i = CHARSET_INVALID + 1; i < CHARSET_MAX; i++) {
    ObCharsetType test_cs_type = static_cast<ObCharsetType>(i);
    if (!ObCharset::is_valid_charset(test_cs_type)) {
      continue;
    }
    ObCollationType test_collation_type = ObCharset::get_default_collation(test_cs_type);
    ObString data_out;
    ASSERT_TRUE(ObCharset::is_valid_collation(test_collation_type));
    if (ObCharset::get_charset(test_collation_type)->mbmaxlen == 1 || test_cs_type == CHARSET_SJIS) {
      data_out = data_in;
      continue;
    } else if (test_cs_type == CHARSET_BIG5 || test_cs_type == CHARSET_HKSCS || test_cs_type == CHARSET_HKSCS31) {
      ASSERT_TRUE(OB_SUCCESS == ObCharset::charset_convert(alloc, data_in1, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if (test_cs_type == CHARSET_GB2312) {
      ASSERT_TRUE(OB_SUCCESS == ObCharset::charset_convert(alloc, data_in2, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if (test_cs_type == CHARSET_UJIS || test_cs_type == CHARSET_CP932 || test_cs_type == CHARSET_EUCJPMS) {
      ASSERT_TRUE(OB_SUCCESS == ObCharset::charset_convert(alloc, data_in_jp, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if (test_cs_type == CHARSET_EUCKR) {
      ASSERT_TRUE(OB_SUCCESS == ObCharset::charset_convert(alloc, data_in_kr, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else {
      ASSERT_TRUE(OB_SUCCESS == ObCharset::charset_convert(alloc, data_in, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    }


    int data_len = data_out.length();
    total_bytes = static_cast<int64_t>(data_len) * repeat;
    char *buf = (char*)(alloc.alloc(data_len * repeat));
    ObString input(data_len * repeat, buf);
    for (int i = 0; i < repeat; i++) {
      MEMCPY(buf + i*data_len, data_out.ptr(), data_len);
    }
    fprintf(stdout, "\n# For charset: %s, TestDataLen: %d\n", ObCharset::charset_name(test_collation_type), data_len * repeat);
    ASSERT_TRUE(NULL != buf);

    fprintf(stdout, "Raw Impl\n");
    start_timer();
    ASSERT_EQ(OB_SUCCESS, ObCharsetUtils::foreach_char(input, test_collation_type, do_nothing, true));
    end_timer();
    int64_t raw_word_cnt = word_cnt;


    fprintf(stdout, "Inline Impl\n");
    start_timer();
    ASSERT_EQ(OB_SUCCESS, ObFastStringScanner::foreach_char(input, test_cs_type, do_nothing));
    end_timer();
    int64_t inline_word_cnt = word_cnt;
    ASSERT_EQ(inline_word_cnt, raw_word_cnt);

    fprintf(stdout, "Skip encoding Impl\n");
    start_timer();
    ASSERT_EQ(OB_SUCCESS, ObFastStringScanner::foreach_char(input, test_cs_type, do_nothing, false));
    end_timer();
    int64_t skip_word_cnt = word_cnt;
    ASSERT_EQ(skip_word_cnt, raw_word_cnt);
  }
  
  ObCollationType test_cs_type_;
  auto test_decode = [&test_cs_type_] (const ObString &str, ob_wc_t wchar) -> int {
    int ret = OB_SUCCESS;
    int32_t right_wc;
    ret = ObCharset::mb_wc(test_cs_type_, str, right_wc);
    if(right_wc != wchar) {
      fprintf(stdout, "[foreach decode check ERROR] wchar = %ld, right_wc = %d\n", wchar, right_wc);
      ret = OB_ERR_INCORRECT_STRING_VALUE;
    }
    return ret;
  };
  
  for (int i = CHARSET_BINARY + 1; i < CHARSET_MAX; i++) {
    ObCharsetType test_cs_type = static_cast<ObCharsetType>(i);
    if (!ObCharset::is_valid_charset(test_cs_type)) {
      continue;
    }
    ObCollationType test_collation_type = ObCharset::get_default_collation(test_cs_type);
    test_cs_type_ = test_collation_type;
    ObString data_out;
    ASSERT_TRUE(ObCharset::is_valid_collation(test_collation_type));
    
    
    fprintf(stdout, "\n# For charset(decode): %s\n", ObCharset::charset_name(test_collation_type));
    
    if(ObCharset::get_charset(test_collation_type)->mbmaxlen == 1) { // latin1, ascii, tis620, dec8
      ASSERT_EQ(OB_SUCCESS, ObCharset::charset_convert(alloc, data_in_ascii, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if (test_cs_type == CHARSET_BIG5 || test_cs_type == CHARSET_HKSCS || test_cs_type == CHARSET_HKSCS31) { // traditional chinese
      ASSERT_EQ(OB_SUCCESS, ObCharset::charset_convert(alloc, data_in1, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if(test_cs_type == CHARSET_SJIS
           || test_cs_type == CHARSET_UJIS
           || test_cs_type == CHARSET_CP932
           || test_cs_type == CHARSET_EUCJPMS) { // japanese
      ASSERT_EQ(OB_SUCCESS, ObCharset::charset_convert(alloc, data_in_jp, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if(test_cs_type == CHARSET_EUCKR) { // korean
      ASSERT_EQ(OB_SUCCESS, ObCharset::charset_convert(alloc, data_in_kr, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else if(test_cs_type == CHARSET_GB2312) {
      ASSERT_EQ(OB_SUCCESS, ObCharset::charset_convert(alloc, data_in2, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    } else { // simplify chinese
      ASSERT_EQ(OB_SUCCESS, ObCharset::charset_convert(alloc, data_in, CS_TYPE_UTF8MB4_BIN, test_collation_type, data_out));
    }
    
    ASSERT_EQ(OB_SUCCESS, ObFastStringScanner::foreach_char(data_out, test_cs_type, test_decode));
  }
}




TEST_F(TestCharset, basic_charset_handler_test)
{
  ObArenaAllocator alloc;
  for (int i = CHARSET_INVALID; i < CHARSET_MAX; i++) {
    if (ObCharset::is_valid_charset(static_cast<ObCharsetType>(i))) {
      ObCollationType coll = static_cast<ObCollationType>(ObCharset::get_default_collation(static_cast<ObCharsetType>(i)));
      const char *coll_name = ObCharset::collation_name(coll);
      const ObCharsetInfo * cs = ObCharset::get_charset(coll);
      std::cout << "#TEST Coll = " << coll_name << std::endl;
      for (const char* utf8_str:test_strings) {
        ObString dst;
        if (cs->mbmaxlen <= 1) {
          dst = ObString(utf8_str);
        } else {
          ASSERT_EQ(0, ObCharset::charset_convert(alloc, ObString(utf8_str), CS_TYPE_UTF8MB4_BIN, coll, dst));
        }
        const char*str = dst.ptr();
        const char*end = dst.ptr() + dst.length();
        if (OB_NOT_NULL(cs->cset->ismbchar)) {
          fprintf(stdout, ">> ismbchar = %d for text = \"%s\"\n",
                    cs->cset->ismbchar(cs, str, end), utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->mbcharlen)) {
          fprintf(stdout, ">> mbcharlen = %d for text = \"%s\"\n",
                    cs->cset->mbcharlen(cs, str[0]), utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->numchars)) {
          fprintf(stdout, ">> numchars = %ld for text = \"%s\"\n",
                    cs->cset->numchars(cs, str, end), utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->charpos)) {
          size_t pos = 3;
          fprintf(stdout, ">> charpos = %ld pos = %ld for text = \"%s\"\n",
                    cs->cset->charpos(cs, str, end, pos), pos, utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->max_bytes_charpos)) {
          size_t max_bytes = 5;
          size_t char_len = 0;
          fprintf(stdout, ">> max_bytes_charpos = %ld max_bytes = %ld char_len = %ld for text = \"%s\"\n",
                    cs->cset->max_bytes_charpos(cs, str, end, max_bytes, &char_len), max_bytes, char_len, utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->well_formed_len)) {
          int error = 0;
          fprintf(stdout, ">> well_formed_len = %ld error = %d text = \"%s\"\n",
                    cs->cset->well_formed_len(cs, str, end, INT64_MAX, &error), error, utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->lengthsp)) {
          fprintf(stdout, ">> lengthsp = %ld text = \"%s\"\n",
                    cs->cset->lengthsp(cs, str, end-str), utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->mb_wc)) {
          ob_wc_t wchar = 0;
          fprintf(stdout, ">> mb_wc = %d wchar = %ld text = \"%s\"\n",
                    cs->cset->mb_wc(cs, &wchar, pointer_cast<const uchar*>(str), pointer_cast<const uchar*>(end)), wchar, utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->wc_mb)) {
          ob_wc_t wchar = 41;
          unsigned char temp[10];
          MEMSET(temp, 0, 10);
          fprintf(stdout, ">> wc_mb = %d A = %.*s text = \"%s\"\n",
                    cs->cset->wc_mb(cs, wchar, temp, temp + 10), 10, temp, utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->ctype)) {
          int ctype = 0;
          fprintf(stdout, ">> ctype = %d ctype = %d text = \"%s\"\n",
                    cs->cset->ctype(cs, &ctype, pointer_cast<const uchar*>(str), pointer_cast<const uchar*>(end)), ctype, utf8_str);
        }
        if (cs->casedn_multiply <= 1 && OB_NOT_NULL(cs->cset->casedn)) {
          ObString temp;
          ASSERT_EQ(0, ob_write_string(alloc, dst, temp));
          fprintf(stdout, ">> casedn = %ld res = %.*s text = \"%s\"\n",
                    cs->cset->casedn(cs, temp.ptr(), temp.length(), temp.ptr(), temp.length()), temp.length(), temp.ptr(), utf8_str);
        }
        if (cs->caseup_multiply <= 1 && OB_NOT_NULL(cs->cset->caseup)) {
          ObString temp;
          ASSERT_EQ(0, ob_write_string(alloc, dst, temp));
          fprintf(stdout, ">> caseup = %ld res = %.*s text = \"%s\"\n",
                    cs->cset->caseup(cs, temp.ptr(), temp.length(), temp.ptr(), temp.length()), temp.length(), temp.ptr(), utf8_str);
        }
        if (OB_NOT_NULL(cs->cset->fill)) {
          char temp[10];
          cs->cset->fill(cs, temp, 10, 0x42);
          fprintf(stdout, ">> fill res = %.*s text = \"%s\"\n", 10, temp, utf8_str);
        }
      }
    }
  }
}

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc,argv);
  int ret = ObCharset::init_charset();
  fprintf(stdout, "ret=%d\n", ret);
  return RUN_ALL_TESTS();
}
