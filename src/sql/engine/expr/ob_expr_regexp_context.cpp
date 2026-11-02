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

#define USING_LOG_PREFIX LIB
#include "sql/engine/expr/ob_expr_regexp_context.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{
// The SQL expression layer uses big-endian UTF-16 strings. Keep that contract
// while using seekdb's UTF-8 codec instead of ICU for conversion.
uint16_t read_utf16_unit(const char *ptr)
{
  return (static_cast<uint16_t>(static_cast<unsigned char>(ptr[0])) << 8)
      | static_cast<unsigned char>(ptr[1]);
}

void write_utf16_unit(char *ptr, uint16_t unit)
{
  ptr[0] = static_cast<char>(unit >> 8);
  ptr[1] = static_cast<char>(unit & 0xff);
}

int next_utf16(const ObString &src, int64_t &pos, int32_t &codepoint)
{
  int ret = OB_SUCCESS;
  if (pos < 0 || pos + 2 > src.length()) {
    ret = OB_ERR_INCORRECT_STRING_VALUE;
  } else {
    const uint16_t first = read_utf16_unit(src.ptr() + pos);
    pos += 2;
    if (first >= 0xd800 && first <= 0xdbff) {
      if (pos + 2 > src.length()) {
        ret = OB_ERR_INCORRECT_STRING_VALUE;
      } else {
        const uint16_t second = read_utf16_unit(src.ptr() + pos);
        if (second < 0xdc00 || second > 0xdfff) {
          ret = OB_ERR_INCORRECT_STRING_VALUE;
        } else {
          codepoint = 0x10000 + ((first - 0xd800) << 10) + second - 0xdc00;
          pos += 2;
        }
      }
    } else if (first >= 0xdc00 && first <= 0xdfff) {
      ret = OB_ERR_INCORRECT_STRING_VALUE;
    } else {
      codepoint = first;
    }
  }
  return ret;
}
}

ObExprRegexContext::ObExprRegexContext()
  : ObExprOperatorCtx(), inited_(false), cflags_(0), stack_limit_(0),
    time_limit_(0), regexp_engine_(NULL)
{
}

ObExprRegexContext::~ObExprRegexContext()
{
  destroy();
}

void ObExprRegexContext::reset()
{
  destroy();
}

void ObExprRegexContext::destroy()
{
  if (regexp_engine_ != NULL) {
    regex_free(regexp_engine_);
    regexp_engine_ = NULL;
  }
  inited_ = false;
  cflags_ = 0;
  stack_limit_ = 0;
  time_limit_ = 0;
  pattern_.reset();
}

int ObExprRegexContext::convert_to_regexp_utf16(ObIAllocator &alloc,
                                               const ObString &src,
                                               const ObCollationType src_coll,
                                               ObString &dst)
{
  int ret = OB_SUCCESS;
  dst.reset();
  if (src.length() < 0 || (src.length() > 0 && OB_ISNULL(src.ptr()))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (CS_TYPE_UTF16_BIN == src_coll || CS_TYPE_UTF16_GENERAL_CI == src_coll) {
    if (0 != src.length() % 2) {
      ret = OB_ERR_INCORRECT_STRING_VALUE;
    } else {
      dst = src;
    }
  } else if (CS_TYPE_BINARY == src_coll) {
    const int64_t len = static_cast<int64_t>(src.length()) * 2;
    char *buf = NULL;
    if (len > INT32_MAX) {
      ret = OB_SIZE_OVERFLOW;
    } else if (len == 0) {
    } else if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      for (int64_t i = 0; i < src.length(); ++i) {
        write_utf16_unit(buf + i * 2, static_cast<unsigned char>(src.ptr()[i]));
      }
      dst.assign_ptr(buf, static_cast<int32_t>(len));
    }
  } else if (CHARSET_UTF8MB4 == ObCharset::charset_type_by_coll(src_coll)) {
    int64_t len = 0;
    for (int64_t pos = 0; OB_SUCC(ret) && pos < src.length();) {
      int32_t consumed = 0;
      int32_t codepoint = 0;
      if (OB_FAIL(ObCharset::mb_wc(CS_TYPE_UTF8MB4_BIN, src.ptr() + pos,
                                  src.length() - pos, consumed, codepoint))) {
      } else if (codepoint < 0 || codepoint > 0x10ffff
                 || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        ret = OB_ERR_INCORRECT_STRING_VALUE;
      } else {
        len += codepoint > 0xffff ? 4 : 2;
        pos += consumed;
        if (len > INT32_MAX) {
          ret = OB_SIZE_OVERFLOW;
        }
      }
    }
    char *buf = NULL;
    if (OB_FAIL(ret) || len == 0) {
    } else if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t out = 0;
      for (int64_t pos = 0; OB_SUCC(ret) && pos < src.length();) {
        int32_t consumed = 0;
        int32_t codepoint = 0;
        if (OB_FAIL(ObCharset::mb_wc(CS_TYPE_UTF8MB4_BIN, src.ptr() + pos,
                                    src.length() - pos, consumed, codepoint))) {
        } else {
          if (codepoint > 0xffff) {
            codepoint -= 0x10000;
            write_utf16_unit(buf + out, 0xd800 + (codepoint >> 10));
            write_utf16_unit(buf + out + 2, 0xdc00 + (codepoint & 0x3ff));
            out += 4;
          } else {
            write_utf16_unit(buf + out, static_cast<uint16_t>(codepoint));
            out += 2;
          }
          pos += consumed;
        }
      }
      if (OB_SUCC(ret)) {
        dst.assign_ptr(buf, static_cast<int32_t>(out));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObExprRegexContext::convert_from_regexp_utf16(ObIAllocator &alloc,
                                                 const ObString &src,
                                                 const ObCollationType dst_coll,
                                                 ObString &dst)
{
  int ret = OB_SUCCESS;
  dst.reset();
  if (src.length() < 0 || (src.length() > 0 && OB_ISNULL(src.ptr()))
      || src.length() % 2 != 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (src.empty()) {
  } else if (CS_TYPE_UTF16_BIN == dst_coll || CS_TYPE_UTF16_GENERAL_CI == dst_coll) {
    dst = src;
  } else if (CS_TYPE_BINARY == dst_coll) {
    const int32_t len = src.length() / 2;
    char *buf = static_cast<char *>(alloc.alloc(len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      for (int32_t i = 0; i < len; ++i) {
        buf[i] = static_cast<char>(read_utf16_unit(src.ptr() + i * 2) & 0xff);
      }
      dst.assign_ptr(buf, len);
    }
  } else if (CHARSET_UTF8MB4 == ObCharset::charset_type_by_coll(dst_coll)) {
    int64_t len = 0;
    char scratch[4];
    for (int64_t pos = 0; OB_SUCC(ret) && pos < src.length();) {
      int32_t codepoint = 0;
      int32_t written = 0;
      if (OB_FAIL(next_utf16(src, pos, codepoint))) {
      } else if (OB_FAIL(ObCharset::wc_mb(CS_TYPE_UTF8MB4_BIN, codepoint,
                                         scratch, sizeof(scratch), written))) {
      } else {
        len += written;
        if (len > INT32_MAX) {
          ret = OB_SIZE_OVERFLOW;
        }
      }
    }
    char *buf = NULL;
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t out = 0;
      for (int64_t pos = 0; OB_SUCC(ret) && pos < src.length();) {
        int32_t codepoint = 0;
        int32_t written = 0;
        if (OB_FAIL(next_utf16(src, pos, codepoint))) {
        } else if (OB_FAIL(ObCharset::wc_mb(CS_TYPE_UTF8MB4_BIN, codepoint,
                                           buf + out, static_cast<int32_t>(len - out), written))) {
        } else {
          out += written;
        }
      }
      if (OB_SUCC(ret)) {
        dst.assign_ptr(buf, static_cast<int32_t>(out));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObExprRegexContext::init(ObExprStringBuf &string_buf,
                             const ObExprRegexpSessionVariables &regex_vars,
                             const ObString &origin_pattern,
                             const uint32_t cflags,
                             const bool reusable,
                             const ObCollationType pattern_cs_type)
{
  int ret = OB_SUCCESS;
  ObString pattern;
  if (inited_ && !reusable) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(convert_to_regexp_utf16(string_buf, origin_pattern,
                                            pattern_cs_type, pattern))) {
  } else if (reusable && inited_ && pattern_ == pattern
             && cflags_ == static_cast<int>(cflags)
             && stack_limit_ == regex_vars.regexp_stack_limit_
             && time_limit_ == regex_vars.regexp_time_limit_) {
  } else {
    reset();
    RegexEngine *engine = NULL;
    RegexError error = {};
    if (regex_compile(reinterpret_cast<const uint8_t *>(pattern.ptr()),
                             pattern.length(), cflags, regex_vars.regexp_stack_limit_,
                             regex_vars.regexp_time_limit_, &engine, &error) != 0) {
      ret = check_regexp_status(error);
    } else {
      pattern_allocator_.prepare(string_buf);
      char *saved = NULL;
      if (!pattern.empty() && OB_ISNULL(saved = static_cast<char *>(pattern_allocator_.alloc(pattern.length())))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        regex_free(engine);
      } else {
        if (!pattern.empty()) {
          MEMCPY(saved, pattern.ptr(), pattern.length());
          pattern_.assign_ptr(saved, pattern.length());
        }
        regexp_engine_ = engine;
        cflags_ = cflags;
        stack_limit_ = regex_vars.regexp_stack_limit_;
        time_limit_ = regex_vars.regexp_time_limit_;
        inited_ = true;
      }
    }
  }
  return ret;
}

int ObExprRegexContext::match(ObExprStringBuf &string_buf, const ObString &text,
                              const ObCollationType cs_type, const int64_t start,
                              bool &result) const
{
  UNUSEDx(string_buf, cs_type);
  int ret = OB_SUCCESS;
  result = false;
  RegexMatch found = {};
  RegexError error = {};
  if (!inited_ || OB_ISNULL(regexp_engine_)) {
    ret = OB_NOT_INIT;
  } else if (regex_find(regexp_engine_, reinterpret_cast<const uint8_t *>(text.ptr()),
                              text.length(), start, 1, 0, &found, &error) != 0) {
    ret = check_regexp_status(error);
  } else {
    result = found.matched != 0;
  }
  return ret;
}

int ObExprRegexContext::find(ObExprStringBuf &string_buf, const ObString &text,
                             const ObCollationType cs_type, const int64_t start,
                             const int64_t occurrence, const int64_t return_option,
                             const int64_t subexpr, int64_t &result) const
{
  UNUSEDx(string_buf, cs_type);
  int ret = OB_SUCCESS;
  result = 0;
  RegexMatch found = {};
  RegexError error = {};
  if (!inited_ || OB_ISNULL(regexp_engine_)) {
    ret = OB_NOT_INIT;
  } else if (text.empty()) {
    // Preserve the SQL function's existing empty-input behavior.
  } else if (regex_find(regexp_engine_, reinterpret_cast<const uint8_t *>(text.ptr()),
                              text.length(), start, occurrence == 0 ? 1 : occurrence,
                              subexpr, &found, &error) != 0) {
    ret = check_regexp_status(error);
  } else if (found.matched) {
    result = (return_option ? found.end : found.start) + 1;
  }
  return ret;
}

int ObExprRegexContext::substr(ObExprStringBuf &string_buf, const ObString &text,
                               const ObCollationType cs_type, const int64_t start,
                               const int64_t occurrence, const int64_t subexpr,
                               ObString &result) const
{
  UNUSEDx(string_buf, cs_type);
  int ret = OB_SUCCESS;
  result.reset();
  RegexMatch found = {};
  RegexError error = {};
  if (!inited_ || OB_ISNULL(regexp_engine_)) {
    ret = OB_NOT_INIT;
  } else if (regex_find(regexp_engine_, reinterpret_cast<const uint8_t *>(text.ptr()),
                              text.length(), start, occurrence, subexpr, &found, &error) != 0) {
    ret = check_regexp_status(error);
  } else if (found.matched && found.end > found.start) {
    if (found.start < 0 || found.end > text.length() / 2) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      result.assign_ptr(text.ptr() + found.start * 2,
                        static_cast<int32_t>((found.end - found.start) * 2));
    }
  }
  return ret;
}

int ObExprRegexContext::replace(ObExprStringBuf &string_buf, const ObString &text,
                                const ObCollationType cs_type, const ObString &replacement,
                                const int64_t start, const int64_t occurrence,
                                ObString &result) const
{
  UNUSED(cs_type);
  int ret = OB_SUCCESS;
  result.reset();
  RegexBuffer buffer = {};
  RegexError error = {};
  if (!inited_ || OB_ISNULL(regexp_engine_)) {
    ret = OB_NOT_INIT;
  } else if (text.empty()) {
    result = text;
  } else if (regex_replace(regexp_engine_, reinterpret_cast<const uint8_t *>(text.ptr()),
                                 text.length(), reinterpret_cast<const uint8_t *>(replacement.ptr()),
                                 replacement.length(), start, occurrence, &buffer, &error) != 0) {
    ret = check_regexp_status(error);
  } else if (buffer.len > INT32_MAX) {
    ret = OB_SIZE_OVERFLOW;
  } else if (buffer.len != 0) {
    char *buf = static_cast<char *>(string_buf.alloc(buffer.len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buf, buffer.data, buffer.len);
      result.assign_ptr(buf, static_cast<int32_t>(buffer.len));
    }
  }
  regex_buffer_free(&buffer);
  return ret;
}

int ObExprRegexContext::check_regexp_status(const RegexError &error) const
{
  int ret = OB_SUCCESS;
  switch (static_cast<RegexErrorKind>(error.kind)) {
    case RegexErrorKind::None:
      break;
    case RegexErrorKind::Paren:
      ret = OB_ERR_REGEXP_EPAREN;
      break;
    case RegexErrorKind::Bracket:
      ret = OB_ERR_REGEXP_EBRACK;
      break;
    case RegexErrorKind::Escape:
      ret = OB_ERR_REGEXP_EESCAPE;
      break;
    case RegexErrorKind::InvalidEncoding:
      ret = OB_ERR_INCORRECT_STRING_VALUE;
      break;
    case RegexErrorKind::Allocation:
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    case RegexErrorKind::Argument:
      ret = OB_INVALID_ARGUMENT;
      break;
    default:
      ret = OB_ERR_REGEXP_ERROR;
      LOG_USER_ERROR(OB_ERR_REGEXP_ERROR, error.message.data());
      break;
  }
  return ret;
}

int ObExprRegexContext::get_regexp_flags(const ObString &match_param,
                                        const bool is_case_sensitive,
                                        const bool is_som_leftmost,
                                        const bool is_single_match,
                                        uint32_t &flags)
{
  UNUSEDx(is_som_leftmost, is_single_match);
  int ret = OB_SUCCESS;
  flags = is_case_sensitive ? 0 : static_cast<uint32_t>(RegexFlags::CaseInsensitive);
  for (int i = 0; OB_SUCC(ret) && i < match_param.length(); ++i) {
    switch (match_param.ptr()[i]) {
      case 'c': flags &= ~static_cast<uint32_t>(RegexFlags::CaseInsensitive); break;
      case 'i': flags |= static_cast<uint32_t>(RegexFlags::CaseInsensitive); break;
      case 'm': flags |= static_cast<uint32_t>(RegexFlags::Multiline); break;
      case 'n': flags |= static_cast<uint32_t>(RegexFlags::DotAll); break;
      case 'u': flags |= static_cast<uint32_t>(RegexFlags::UnixLines); break;
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "use match param in regexp expression");
        break;
    }
  }
  return ret;
}

int ObExprRegexContext::check_need_utf8(ObRawExpr *expr, bool &need_utf8)
{
  int ret = OB_SUCCESS;
  need_utf8 = false;
  const ObRawExpr * real_expr = NULL;
  if (OB_FAIL(ObRawExprUtils::get_real_expr_without_cast(expr, real_expr))) {
  } else if (OB_ISNULL(real_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    need_utf8 = real_expr->get_result_type().is_blob() ||
                real_expr->get_result_type().is_binary() ||
                real_expr->get_result_type().is_varbinary();
  }
  return ret;
}

int ObExprRegexContext::check_binary_compatible(const ObExprResType *types, int64_t num) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(types)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t binary_param_idx = -1;
    int64_t nobinary_param_idx = -1;
    for (int64_t i = 0; i < num; ++i) {
      if (ObExprRegexContext::is_binary_string(types[i])) {
        binary_param_idx = i;
      } else if (!ObExprRegexContext::is_binary_compatible(types[i])) {
        nobinary_param_idx = i;
      }
    }
    if (-1 != binary_param_idx && -1 != nobinary_param_idx) {
      const char *coll_name1 = ObCharset::collation_name(types[binary_param_idx].get_collation_type());
      const char *coll_name2 = ObCharset::collation_name(types[nobinary_param_idx].get_collation_type());
      ObString collation1 = ObString::make_string(coll_name1);
      ObString collation2 = ObString::make_string(coll_name2);
      ret = OB_ERR_MYSQL_CHARACTER_SET_MISMATCH;
      LOG_USER_ERROR(OB_ERR_MYSQL_CHARACTER_SET_MISMATCH, collation1.length(), collation1.ptr(), collation2.length(), collation2.ptr());
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObExprRegexpSessionVariables, regexp_stack_limit_, regexp_time_limit_);

}
}
