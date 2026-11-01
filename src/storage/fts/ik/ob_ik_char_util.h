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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_IK_CHAR_UTIL_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_IK_CHAR_UTIL_H_

#include "lib/charset/ob_charset.h"
#include "lib/charset/ob_charset_string_helper.h"
#include "lib/charset/ob_ctype.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/utils/unicode_utils.h"

#include <cstdint>
#include <type_traits>

namespace oceanbase
{
namespace storage
{
// See: lang/Character.java
class ObFTCharUtil
{
public:
  enum class CharType : int8_t
  {
    USELESS = 0,
    ARABIC_LETTER = 1,
    ENGLISH_LETTER = 2,
    CHINESE = 3,
    OTHER_CJK = 4,
    SURROGATE_HIGH = 5,
    SURROGATE_LOW = 6,
  };

  static int classify_first_char(ObCollationType coll_type,
                                 const char *input,
                                 const uint8_t char_len,
                                 CharType &type);

  static int first_valid_char_fast(ObCollationType coll_type,
                                   const char *buf,
                                   const int64_t buf_size,
                                   int64_t &char_len);

  static int classify_utf8mb4_fast(const char *input,
                                   const uint8_t char_len,
                                   CharType &type);

  static int check_cn_number(ObCollationType coll_type,
                             const char *input,
                             const uint8_t char_len,
                             bool &is_cn_number);

  static int check_num_connector(ObCollationType coll_type,
                                 const char *input,
                                 const uint8_t char_len,
                                 bool &is_connector);

  static int check_letter_connector(ObCollationType coll_type,
                                    const char *input,
                                    const uint8_t char_len,
                                    bool &is_connector);

  // some cjk word should be ignored
  static int is_ignore_single_cjk(ObCollationType coll_type,
                                  const char *input,
                                  const uint8_t char_len,
                                  bool &ignore);

private:
  static inline bool is_continuation_byte(const char ch)
  {
    return 0x02 == (static_cast<unsigned char>(ch) >> 6);
  }

  template <ObCharsetType CS_TYPE>
  static int do_classify(const char *input, const uint8_t char_len, CharType &type);

private:
  /*************************  Alpha: english letter abc *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_alpha(const char *input, const uint8_t char_len, bool &is_alpha);
  /****************************************************************************************/

  /*************************  Arabic: num letter 123 *********************/

  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_arabic(const char *input, const uint8_t char_len, bool &is_arabic);
  /****************************************************************************************/

  /*************************  Chinese: Characters *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_chinese(const char *input, const uint8_t char_len, bool &is_chinese);
  /****************************************************************************************/

  /*************************  Other CJK:  Japanese Korean Chinese *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_other_cjk(const char *input, const uint8_t char_len, bool &is_other_cjk);
  /****************************************************************************************/

  /*************************  Surrogate: only for UTF16 *********************/
  template <ObCharsetType CS_TYPE>
  static
      typename std::enable_if<(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
      is_surrogate_high(const char *input, const uint8_t char_len, bool &is_surrogate_high);

  template <ObCharsetType CS_TYPE>
  static
      typename std::enable_if<(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
      is_surrogate_low(const char *input, const uint8_t char_len, bool &is_surrogate_low);

  template <ObCharsetType CS_TYPE>
  static
      typename std::enable_if<!(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
      is_surrogate_high(const char *input, const uint8_t char_len, bool &is_surrogate_high);

  template <ObCharsetType CS_TYPE>
  static
      typename std::enable_if<!(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
      is_surrogate_low(const char *input, const uint8_t char_len, bool &is_surrogate_low);
  /****************************************************************************************/

  /*************************  Ignore: some char should not output itself *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_ignore(const char *input, const uint8_t char_len, bool &ignore);
  /****************************************************************************************/

  /*************************  CN Number: one two three two *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_cn_number(const char *input, const uint8_t char_len, bool &is_cn_number);
  /****************************************************************************************/

  /*************************  English letter connector: #,-@ *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_letter_connector(const char *input, const uint8_t char_len, bool &is_connector);
  /****************************************************************************************/

  /*************************  Number letter connector: ,. *********************/
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  is_num_connector(const char *input, const uint8_t char_len, bool &is_connector);
  /****************************************************************************************/

private:
  // only for unicode charset
  template <ObCharsetType CS_TYPE>
  static typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                  || CS_TYPE == CHARSET_UTF16LE),
                                 int>::type
  decode_unicode(const char *input, const uint8_t char_len, ob_wc_t &unicode)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(input)) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_FTS_LOG(WARN, "Invalid input", K(input), K(ret));
    } else {
      const unsigned char *ustart = reinterpret_cast<const unsigned char *>(input);
      const unsigned char *uend = reinterpret_cast<const unsigned char *>(&input[char_len]);
      int code_size = common::ob_charset_decode_unicode<CS_TYPE>(ustart, uend, unicode);
      if (code_size < 0) {
        ret = OB_UNEXPECT_INTERNAL_ERROR;
        STORAGE_FTS_LOG(WARN, "Failed to decode unicode", K(code_size), K(ret));
      }
    }
    return ret;
  }
}; // End of class.

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_alpha(const char *input, const uint8_t char_len, bool &is_alpha)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_alpha = ObUnicodeBlockUtils::is_alpha(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_arabic(const char *input, const uint8_t char_len, bool &is_arabic)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_arabic = ObUnicodeBlockUtils::is_arabic(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_chinese(const char *input, const uint8_t char_len, bool &is_chinese)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_chinese = ObUnicodeBlockUtils::is_chinese(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_other_cjk(const char *input, const uint8_t char_len, bool &is_other_cjk)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_other_cjk = ObUnicodeBlockUtils::is_other_cjk(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
ObFTCharUtil::is_surrogate_high(const char *input, const uint8_t char_len, bool &is_surrogate_high)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_surrogate_high = ObUnicodeBlockUtils::check_high_surrogate(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
ObFTCharUtil::is_surrogate_low(const char *input, const uint8_t char_len, bool &is_surrogate_low)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_surrogate_low = ObUnicodeBlockUtils::check_low_surrogate(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<!(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
ObFTCharUtil::is_surrogate_high(const char *input, const uint8_t char_len, bool &is_surrogate_high)
{
  int ret = OB_SUCCESS;
  is_surrogate_high = false;
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<!(CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE), int>::type
ObFTCharUtil::is_surrogate_low(const char *input, const uint8_t char_len, bool &is_surrogate_low)
{
  int ret = OB_SUCCESS;
  is_surrogate_low = false;
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_ignore(const char *input, const uint8_t char_len, bool &ignore)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    ignore = ObUnicodeBlockUtils::check_ignore_as_single(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_cn_number(const char *input, const uint8_t char_len, bool &is_cn_number)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_cn_number = ObUnicodeBlockUtils::is_unicode_cn_number(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_letter_connector(const char *input, const uint8_t char_len, bool &is_connector)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_connector = ObUnicodeBlockUtils::check_letter_connector(unicode);
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline typename std::enable_if<(CS_TYPE == CHARSET_UTF8MB4 || CS_TYPE == CHARSET_UTF16
                                || CS_TYPE == CHARSET_UTF16LE),
                               int>::type
ObFTCharUtil::is_num_connector(const char *input, const uint8_t char_len, bool &is_connector)
{
  int ret = OB_SUCCESS;
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
  } else {
    is_connector = ObUnicodeBlockUtils::check_num_connector(unicode);
  }
  return ret;
}

// Support GBK

// Implementation of some frame;

inline int ObFTCharUtil::check_cn_number(ObCollationType coll_type,
                                         const char *input,
                                         const uint8_t char_len,
                                         bool &is_cn_number)
{
  int ret = OB_SUCCESS;
  ObCharsetType cs_type = ObCharset::charset_type_by_coll(coll_type);

  switch (cs_type) {
  case CHARSET_UTF8MB4: {
    ret = ObFTCharUtil::is_cn_number<CHARSET_UTF8MB4>(input, char_len, is_cn_number);
    break;
  }
  default:
    ret = OB_NOT_SUPPORTED;
    STORAGE_FTS_LOG(WARN, "Not supported charset type", K(ret), K(cs_type));
  }
  return ret;
}

inline int ObFTCharUtil::check_num_connector(ObCollationType coll_type,
                                             const char *input,
                                             const uint8_t char_len,
                                             bool &is_connector)
{
  int ret = OB_SUCCESS;
  ObCharsetType cs_type = ObCharset::charset_type_by_coll(coll_type);

  switch (cs_type) {
  case CHARSET_UTF8MB4: {
    ret = ObFTCharUtil::is_num_connector<CHARSET_UTF8MB4>(input, char_len, is_connector);
    break;
  }
  default:
    ret = OB_NOT_SUPPORTED;
    STORAGE_FTS_LOG(WARN, "Not supported charset type", K(ret), K(cs_type));
  }
  return ret;
}

inline int ObFTCharUtil::check_letter_connector(ObCollationType coll_type,
                                                const char *input,
                                                const uint8_t char_len,
                                                bool &is_connector)
{
  int ret = OB_SUCCESS;
  ObCharsetType cs_type = ObCharset::charset_type_by_coll(coll_type);

  switch (cs_type) {
  case CHARSET_UTF8MB4: {
    // ret = ObFTCharUtil::do_check_letter_connector<CHARSET_UTF8MB4>(input, char_len,
    // is_connector);
    ret = ObFTCharUtil::is_letter_connector<CHARSET_UTF8MB4>(input, char_len, is_connector);
    break;
  }
  default:
    ret = OB_NOT_SUPPORTED;
    STORAGE_FTS_LOG(WARN, "Not supported charset type", K(ret), K(cs_type));
  }
  return ret;
}

inline int ObFTCharUtil::is_ignore_single_cjk(ObCollationType coll_type,
                                              const char *input,
                                              const uint8_t char_len,
                                              bool &ignore)
{
  int ret = OB_SUCCESS;
  ObCharsetType cs_type = ObCharset::charset_type_by_coll(coll_type);

  switch (cs_type) {
  case CHARSET_UTF8MB4: {
    ret = ObFTCharUtil::is_ignore<CHARSET_UTF8MB4>(input, char_len, ignore);
    break;
  }
  default:
    ret = OB_NOT_SUPPORTED;
    STORAGE_FTS_LOG(WARN, "Not supported charset type", K(ret), K(cs_type));
    break;
  }
  return ret;
}

template <ObCharsetType CS_TYPE>
inline int ObFTCharUtil::do_classify(const char *input, const uint8_t char_len, CharType &type)
{
  int ret = OB_SUCCESS;
  type = CharType::USELESS;

  if (1 == char_len) {
    const uint8_t ch = static_cast<uint8_t>(input[0]);
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
      type = CharType::ENGLISH_LETTER;
      return ret;
    } else if (ch >= '0' && ch <= '9') {
      type = CharType::ARABIC_LETTER;
      return ret;
    }
  }

  // Decode the codepoint exactly once, then run all block predicates on it.
  // The previous implementation called is_alpha/is_arabic/is_chinese/... in
  // sequence, and each wrapper decoded the same bytes again — up to 3 redundant
  // decodes per character. Predicate order and results are unchanged.
  ob_wc_t unicode = 0;
  if (OB_FAIL(decode_unicode<CS_TYPE>(input, char_len, unicode))) {
    // decode failure: leave type as USELESS, same as before
  } else if ((CS_TYPE != CHARSET_UTF8MB4 || char_len > 1)
             && ObUnicodeBlockUtils::is_chinese(unicode)) {
    type = CharType::CHINESE;
  } else if (ObUnicodeBlockUtils::is_alpha(unicode)) {
    type = CharType::ENGLISH_LETTER;
  } else if (ObUnicodeBlockUtils::is_arabic(unicode)) {
    type = CharType::ARABIC_LETTER;
  } else if (ObUnicodeBlockUtils::is_other_cjk(unicode)) {
    type = CharType::OTHER_CJK;
  } else if ((CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE)
             && ObUnicodeBlockUtils::check_high_surrogate(unicode)) {
    type = CharType::SURROGATE_HIGH;
  } else if ((CS_TYPE == CHARSET_UTF16 || CS_TYPE == CHARSET_UTF16LE)
             && ObUnicodeBlockUtils::check_low_surrogate(unicode)) {
    type = CharType::SURROGATE_LOW;
  }
  return ret;
}

inline int ObFTCharUtil::classify_utf8mb4_fast(const char *input,
                                                const uint8_t char_len,
                                                CharType &type)
{
  int ret = OB_SUCCESS;
  type = CharType::USELESS;
  if (OB_ISNULL(input) || OB_UNLIKELY(0 == char_len)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const unsigned char first = static_cast<unsigned char>(input[0]);
    if (first < 0x80) {
      if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
        type = CharType::ENGLISH_LETTER;
      } else if (first >= '0' && first <= '9') {
        type = CharType::ARABIC_LETTER;
      }
    } else if (first < 0xe0) {
      ret = do_classify<CHARSET_UTF8MB4>(input, char_len, type);
    } else if (first < 0xf0) {
      if (3 == char_len) {
        const unsigned char second = static_cast<unsigned char>(input[1]);
        // U+4E00..U+9FFF is the dominant CJK block in the tokenize workload.
        if (first >= 0xe4 && first <= 0xe9 && (first > 0xe4 || second >= 0xb8)
            && is_continuation_byte(input[2])) {
          type = CharType::CHINESE;
        } else {
          ret = do_classify<CHARSET_UTF8MB4>(input, char_len, type);
        }
      } else {
        ret = do_classify<CHARSET_UTF8MB4>(input, char_len, type);
      }
    } else {
      ret = do_classify<CHARSET_UTF8MB4>(input, char_len, type);
    }
  }
  return ret;
}

inline int ObFTCharUtil::classify_first_char(ObCollationType coll_type,
                                             const char *input,
                                             const uint8_t char_len,
                                             CharType &type)
{
  int ret = OB_SUCCESS;
  ObCharsetType cs_type = ObCharset::charset_type_by_coll(coll_type);

  switch (cs_type) {
  case CHARSET_UTF8MB4: {
    ret = classify_utf8mb4_fast(input, char_len, type);
    break;
  }
  case CHARSET_UTF16: {
    ret = do_classify<CHARSET_UTF16>(input, char_len, type);
    break;
  }
  case CHARSET_UTF16LE: {
    ret = do_classify<CHARSET_UTF16LE>(input, char_len, type);
    break;
  }
  default:
    ret = OB_NOT_SUPPORTED;
    STORAGE_FTS_LOG(WARN, "Not supported charset type", K(ret), K(cs_type));
    break;
  }
  return ret;
}

inline int ObFTCharUtil::first_valid_char_fast(ObCollationType coll_type,
                                                const char *buf,
                                                const int64_t buf_size,
                                                int64_t &char_len)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObCharset::charset_type_by_coll(coll_type) != CHARSET_UTF8MB4)) {
    ret = ObCharset::first_valid_char(coll_type, buf, buf_size, char_len);
  } else if (OB_ISNULL(buf)) {
    ret = OB_NOT_INIT;
  } else if (buf_size <= 0) {
    char_len = 0;
  } else {
    const unsigned char first = static_cast<unsigned char>(buf[0]);
    if (first < 0x80) {
      char_len = 1;
    } else if (first < 0xc2) {
      ret = OB_INVALID_ARGUMENT;
    } else if (first < 0xe0) {
      if (buf_size < 2 || !is_continuation_byte(buf[1])) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        char_len = 2;
      }
    } else if (first < 0xf0) {
      if (buf_size < 3 || !is_continuation_byte(buf[1]) || !is_continuation_byte(buf[2])
          || !(first >= 0xe1 || static_cast<unsigned char>(buf[1]) >= 0xa0)
          || (first == 0xed && static_cast<unsigned char>(buf[1]) >= 0xa0)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        char_len = 3;
      }
    } else if (first < 0xf5) {
      if (buf_size < 4 || !is_continuation_byte(buf[1]) || !is_continuation_byte(buf[2])
          || !is_continuation_byte(buf[3])
          || !(first >= 0xf1 || static_cast<unsigned char>(buf[1]) >= 0x90)
          || !(first <= 0xf3 || static_cast<unsigned char>(buf[1]) <= 0x8f)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        char_len = 4;
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_CHAR_UTIL_H_
