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

#ifndef OB_CHARSET_STRING_HELPER_H
#define OB_CHARSET_STRING_HELPER_H

#include "lib/charset/ob_charset.h"
#include "lib/charset/mb_wc.h"

namespace oceanbase
{
namespace common
{

template<ObCharsetType cs_type>
inline int ob_charset_char_len(const unsigned char *s, const unsigned char *e)
{
  return OB_LIKELY(s < e) ? 1 : OB_CS_TOOSMALL;
}

template<>
inline int ob_charset_char_len<CHARSET_UTF8MB4>(const unsigned char *s, const unsigned char *e)
{
  int mb_len = OB_CS_TOOSMALL;
  if (OB_LIKELY(s < e)) {
    unsigned char c = *s;
    if (c < 0x80) {
      mb_len = 1;
    } else if (c < 0xc2) {
      mb_len = 1;
    } else if (c < 0xe0) {
      mb_len = 2;
    } else if (c < 0xf0) {
      mb_len = 3;
    } else if (c < 0xf8) {
      mb_len = 4;
    } else {
      mb_len = 1;
    }
    if (s + mb_len > e) {
      mb_len = OB_CS_TOOSMALL;
    }
  }
  return mb_len;
}

template<ObCharsetType cs_type>
inline int ob_charset_decode_unicode(const unsigned char *s, const unsigned char *e, ob_wc_t &unicode_value)
{
  unicode_value = 0;
  return ob_charset_char_len<CHARSET_BINARY>(s, e);
}

template<ObCharsetType cs_type>
inline int ob_charset_encode_unicode(ob_wc_t unicode_value, unsigned char *buf, unsigned char *buf_end)
{
  UNUSED(unicode_value);
  UNUSED(buf);
  UNUSED(buf_end);
  return OB_CS_ILUNI;
}

template<>
inline int ob_charset_decode_unicode<CHARSET_BINARY>(
    const unsigned char *s,
    const unsigned char *e,
    ob_wc_t &unicode_value)
{
  if (s >= e) {
    unicode_value = 0;
    return OB_CS_TOOSMALL;
  } else {
    unicode_value = *s;
    return 1;
  }
}

template<>
inline int ob_charset_decode_unicode<CHARSET_UTF8MB4>(
    const unsigned char *s,
    const unsigned char *e,
    ob_wc_t &unicode_value)
{
  return ob_mb_wc_utf8_prototype<true, true>(&unicode_value, s, e);
}

template<>
inline int ob_charset_encode_unicode<CHARSET_UTF8MB4>(
    ob_wc_t unicode_value,
    unsigned char *s,
    unsigned char *e)
{
  ob_wc_t wc = unicode_value;
  int bytes = 0;
  int ret = 0;
  int64_t len = static_cast<int64_t>(e - s);
  if (OB_UNLIKELY(len <= 0)) {
    ret = OB_CS_TOOSMALL;
  } else if (wc < 0x80) {
    bytes = 1;
  } else if (wc < 0x800) {
    bytes = 2;
  } else if (wc < 0x10000) {
    bytes = 3;
  } else if (wc < 0x200000) {
    bytes = 4;
  } else {
    ret = OB_CS_ILUNI;
  }
  if (OB_UNLIKELY(ret != 0)) {
  } else if (OB_UNLIKELY(bytes > len)) {
    ret = OB_CS_TOOSMALLN(bytes);
  } else {
    switch (bytes) {
      case 4:
        s[3] = static_cast<unsigned char>(0x80 | (wc & 0x3f));
        wc >>= 6;
        wc |= 0x10000;
      case 3:
        s[2] = static_cast<unsigned char>(0x80 | (wc & 0x3f));
        wc >>= 6;
        wc |= 0x800;
      case 2:
        s[1] = static_cast<unsigned char>(0x80 | (wc & 0x3f));
        wc >>= 6;
        wc |= 0xc0;
      case 1:
        s[0] = static_cast<unsigned char>(wc);
    }
    ret = bytes;
  }
  return ret;
}

class ObFastStringScanner {
public:
  template<ObCharsetType CS_TYPE, typename HANDLE_FUNC, bool DO_DECODE = true>
  static int foreach_char_prototype(const ObString &str,
                                    HANDLE_FUNC &func,
                                    bool ignore_convert_failed = false,
                                    bool stop_when_truncated = false,
                                    int64_t *truncated_len = NULL)
  {
    int ret = OB_SUCCESS;
    const char *begin = str.ptr();
    const char *end = str.ptr() + str.length();
    int64_t step = 0;
    ob_wc_t unicode = -1;
    for (; OB_SUCC(ret) && begin < end; begin += step) {
      if (DO_DECODE) {
        step = ob_charset_decode_unicode<CS_TYPE>(
            pointer_cast<const unsigned char *>(begin),
            pointer_cast<const unsigned char *>(end),
            unicode);
      } else {
        step = ob_charset_char_len<CS_TYPE>(
            pointer_cast<const unsigned char *>(begin),
            pointer_cast<const unsigned char *>(end));
      }
      if (OB_UNLIKELY(step <= 0)) {
        if (ignore_convert_failed && !(stop_when_truncated && step <= OB_CS_TOOSMALL)) {
          ret = OB_SUCCESS;
          step = 1;
          unicode = -1;
        } else if (step <= OB_CS_TOOSMALL) {
          ret = OB_ERR_DATA_TRUNCATED;
          if (OB_NOT_NULL(truncated_len)) {
            *truncated_len = end - begin;
          }
        } else {
          ret = OB_ERR_INCORRECT_STRING_VALUE;
        }
      }
      if (OB_SUCC(ret)) {
        ret = func(ObString(step, begin), unicode);
      }
    }
    return ret;
  }

  template<typename HANDLE_FUNC>
  static int foreach_char(const ObString &str,
                          const ObCharsetType cs_type,
                          HANDLE_FUNC &func,
                          bool convert_unicode = true,
                          bool ignore_convert_failed = false,
                          bool stop_when_truncated = false,
                          int64_t *truncated_len = NULL)
  {
    int ret = OB_SUCCESS;
    switch (cs_type) {
      case CHARSET_UTF8MB4:
        ret = convert_unicode ?
              foreach_char_prototype<CHARSET_UTF8MB4, HANDLE_FUNC, true>(
                  str, func, ignore_convert_failed, stop_when_truncated, truncated_len)
            : foreach_char_prototype<CHARSET_UTF8MB4, HANDLE_FUNC, false>(
                  str, func, ignore_convert_failed, stop_when_truncated, truncated_len);
        break;
      case CHARSET_BINARY:
        ret = convert_unicode ?
              foreach_char_prototype<CHARSET_BINARY, HANDLE_FUNC, true>(
                  str, func, ignore_convert_failed, stop_when_truncated, truncated_len)
            : foreach_char_prototype<CHARSET_BINARY, HANDLE_FUNC, false>(
                  str, func, ignore_convert_failed, stop_when_truncated, truncated_len);
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        break;
    }
    return ret;
  }

  template<ObCharsetType CS_TYPE>
  struct Encoder {
    Encoder(char *buf, const int64_t buf_len, int64_t &pos, const ob_wc_t replaced_char)
        : ptr_(buf), end_(buf + buf_len), pos_(pos), replaced_char_(replaced_char)
    {}

    inline int operator()(const ObString &encoded_char, const ob_wc_t &unicode)
    {
      UNUSED(encoded_char);
      UNUSED(replaced_char_);
      int ret = OB_SUCCESS;
      int write_len = ob_charset_encode_unicode<CS_TYPE>(
          unicode,
          pointer_cast<unsigned char *>(ptr_),
          pointer_cast<unsigned char *>(end_));
      if (write_len <= 0) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        pos_ += write_len;
        ptr_ += write_len;
      }
      return ret;
    }

    char *ptr_;
    char *end_;
    int64_t &pos_;
    ob_wc_t replaced_char_;
  };

  static int convert_charset(const ObString &str,
                             ObCollationType src_coll_type,
                             ObCollationType out_coll_type,
                             char *buf,
                             int64_t buf_len,
                             int64_t &pos,
                             const bool trim_incomplete_tail = true,
                             const bool report_error = true,
                             const ob_wc_t replaced_char = '?')
  {
    int ret = OB_SUCCESS;
    ObCharsetType in_cs_type = ObCharset::charset_type_by_coll(src_coll_type);
    ObCharsetType out_cs_type = ObCharset::charset_type_by_coll(out_coll_type);
    int64_t truncated_len = 0;
    bool stop_when_truncated = false;
    switch (out_cs_type) {
      case CHARSET_UTF8MB4: {
        Encoder<CHARSET_UTF8MB4> encoder(buf, buf_len, pos, replaced_char);
        ret = foreach_char(str, in_cs_type, encoder, true, !report_error, stop_when_truncated, &truncated_len);
        break;
      }
      default: {
        uint32_t result_len = 0;
        ret = ObCharset::charset_convert(src_coll_type, str.ptr(), str.length(),
                                         out_coll_type, buf, buf_len,
                                         result_len, trim_incomplete_tail,
                                         report_error, replaced_char);
        pos = result_len;
        break;
      }
    }
    if (OB_ERR_DATA_TRUNCATED == ret && truncated_len > 0) {
      if (!report_error || trim_incomplete_tail) {
        ret = OB_SUCCESS;
        if (!trim_incomplete_tail) {
          int32_t tmp_len = 0;
          if (pos + ObCharset::MAX_MB_LEN >= buf_len) {
            ret = OB_SIZE_OVERFLOW;
          } else if (OB_FAIL(ObCharset::wc_mb(out_coll_type, replaced_char, buf + pos, buf_len - pos, tmp_len))) {
          } else {
            pos += tmp_len;
          }
        }
      }
    }
    return ret;
  }
};

}
}

#endif // OB_CHARSET_STRING_HELPER_H
