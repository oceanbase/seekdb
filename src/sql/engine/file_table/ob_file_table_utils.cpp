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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/file_table/ob_file_table_utils.h"

#include <dirent.h>
#include <new>
#include <unistd.h>

#include "common/json_type/ob_json_base.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

void ObFileColumnSchema::reset()
{
  names_.reset();
  types_.reset();
  null_flags_.reset();
}

int ObFileColumnSchema::add_column(ObIAllocator &allocator, const ObString &name, ObObjType type)
{
  int ret = OB_SUCCESS;
  ObString stored;
  if (OB_FAIL(ob_write_string(allocator, name, stored))) {
  } else if (OB_FAIL(names_.push_back(stored))) {
  } else if (OB_FAIL(types_.push_back(type))) {
  } else if (OB_FAIL(null_flags_.push_back(false))) {
  }
  return ret;
}

int ObFileColumnSchema::widen_column(int64_t idx, ObObjType incoming)
{
  int ret = OB_SUCCESS;
  if (idx < 0 || idx >= types_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    types_.at(idx) = ObFileTypeInferrer::merge_type(types_.at(idx), incoming);
  }
  return ret;
}

ObObjType ObFileTypeInferrer::merge_type(ObObjType a, ObObjType b)
{
  if (a == b) {
    return a;
  }
  // widen order: BOOLEAN < BIGINT < DOUBLE < DATETIME < DATE?? keep it simple:
  // numeric widening only; anything else falls back to VARCHAR.
  if (a == ObNullType) {
    return b;
  }
  if (b == ObNullType) {
    return a;
  }
  if ((a == ObIntType && b == ObDoubleType) || (a == ObDoubleType && b == ObIntType)) {
    return ObDoubleType;
  }
  if (ob_is_string_type(a) || ob_is_string_type(b)) {
    return ObVarcharType;
  }
  return ObVarcharType;
}

bool ObFileTypeInferrer::is_integer_text(const char *data, int64_t len)
{
  bool ret = false;
  if (len > 0) {
    int64_t i = 0;
    if (data[0] == '-' || data[0] == '+') {
      i = 1;
    }
    ret = i < len;
    for (; ret && i < len; i++) {
      if (data[i] < '0' || data[i] > '9') {
        ret = false;
      }
    }
  }
  return ret;
}

bool ObFileTypeInferrer::parse_boolean(const char *data, int64_t len, bool &value)
{
  bool ret = false;
  if (len == 4 && strncasecmp(data, "true", 4) == 0) {
    value = true;
    ret = true;
  } else if (len == 5 && strncasecmp(data, "false", 5) == 0) {
    value = false;
    ret = true;
  }
  return ret;
}

static bool split_date(const char *data, int64_t len, int &y, int &m, int &d, int64_t &rest_off)
{
  // formats: YYYY-MM-DD or YYYY/MM/DD
  rest_off = -1;
  if (len < 10) {
    return false;
  }
  if ((data[4] != '-' && data[4] != '/') || (data[7] != '-' && data[7] != '/')) {
    return false;
  }
  for (int64_t i = 0; i < 10; i++) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (data[i] < '0' || data[i] > '9') {
      return false;
    }
  }
  y = (data[0] - '0') * 1000 + (data[1] - '0') * 100 + (data[2] - '0') * 10 + (data[3] - '0');
  m = (data[5] - '0') * 10 + (data[6] - '0');
  d = (data[8] - '0') * 10 + (data[9] - '0');
  rest_off = 10;
  if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) {
    return false;
  }
  return true;
}

bool ObFileTypeInferrer::parse_date(const char *data, int64_t len)
{
  int y = 0, m = 0, d = 0;
  int64_t rest = 0;
  return len == 10 && split_date(data, len, y, m, d, rest);
}

bool ObFileTypeInferrer::parse_datetime(const char *data, int64_t len)
{
  int y = 0, m = 0, d = 0;
  int64_t rest_off = 0;
  if (!split_date(data, len, y, m, d, rest_off) || rest_off >= len) {
    return false;
  }
  // separator between date and time: ' ' or 'T'
  if (data[rest_off] != ' ' && data[rest_off] != 'T') {
    return false;
  }
  int64_t t_off = rest_off + 1;
  int64_t t_len = len - t_off;
  // HH:MM[:SS[.fraction]]
  if (t_len < 5) {
    return false;
  }
  for (int64_t i = 0; i < t_len; i++) {
    char c = data[t_off + i];
    if (c == ':' || c == '.') {
      continue;
    }
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return data[t_off + 2] == ':' && data[t_off + 5] == ':';
}

ObObjType ObFileTypeInferrer::classify(const char *data, int64_t len, bool is_null)
{
  ObObjType type = ObNullType;
  if (!is_null && len > 0) {
    bool bool_val = false;
    if (is_integer_text(data, len)) {
      type = ObIntType;
    } else if (parse_boolean(data, len, bool_val)) {
      type = ObTinyIntType;
    } else if (parse_datetime(data, len)) {
      type = ObDateTimeType;
    } else if (parse_date(data, len)) {
      type = ObDateType;
    } else {
      // number with fraction/exponent?
      const char *p = data;
      int64_t l = len;
      while (l > 0 && (p[0] == ' ' || p[0] == '\t')) { p++; l--; }
      while (l > 0 && (p[l-1] == ' ' || p[l-1] == '\t')) { l--; }
      if (l > 0) {
        // strict double parse
        bool valid = true;
        int64_t i = 0;
        if (p[i] == '-' || p[i] == '+') { i++; }
        int64_t digits = 0, dots = 0;
        for (; i < l; i++) {
          if (p[i] >= '0' && p[i] <= '9') { digits++; }
          else if (p[i] == '.') { dots++; }
          else if (p[i] == 'e' || p[i] == 'E') {
            // exponent part
            if (i + 1 < l && (p[i+1] == '-' || p[i+1] == '+')) { i++; }
          } else { valid = false; break; }
        }
        if (valid && dots <= 1 && digits > 0) {
          type = ObDoubleType;
        } else {
          type = ObVarcharType;
        }
      } else {
        type = ObNullType;  // whitespace-only treated as null
      }
    }
  }
  return type;
}

void ObCsvRecord::reset()
{
  field_ptrs_.reset();
  field_lens_.reset();
}

int ObCsvReader::open(const char *path, int64_t max_buffer_bytes)
{
  int ret = OB_SUCCESS;
  if (nullptr != fd_) {
    close();
  }
  fd_ = fopen(path, "rb");
  if (nullptr == fd_) {
    ret = OB_FILE_NOT_OPENED;
  } else {
    buf_size_ = MAX(max_buffer_bytes, 64 * 1024);
    buf_ = static_cast<char *>(malloc(buf_size_));
    if (nullptr == buf_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      close();
    } else {
      buf_len_ = 0;
      buf_pos_ = 0;
      eof_ = false;
      line_no_ = 0;
    }
  }
  return ret;
}

void ObCsvReader::close()
{
  if (nullptr != fd_) {
    fclose(fd_);
    fd_ = nullptr;
  }
  if (nullptr != buf_) {
    free(buf_);
    buf_ = nullptr;
  }
  buf_len_ = 0;
  buf_pos_ = 0;
  buf_size_ = 0;
  eof_ = false;
}

int ObCsvReader::fill_buffer()
{
  int ret = OB_SUCCESS;
  if (eof_) {
    ret = OB_ITER_END;
  } else {
    // move tail to front, keep the unconsumed bytes
    int64_t remain = buf_len_ - buf_pos_;
    if (remain > 0 && buf_pos_ > 0) {
      memmove(buf_, buf_ + buf_pos_, remain);
    }
    buf_pos_ = 0;
    buf_len_ = remain;
    size_t n = fread(buf_ + buf_len_, 1, buf_size_ - buf_len_, fd_);
    if (n > 0) {
      buf_len_ += static_cast<int64_t>(n);
    }
    if (n <= 0) {
      eof_ = true;
      if (0 == buf_len_) {
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}

int ObCsvReader::read_char_locked(char &c)
{
  int ret = OB_SUCCESS;
  if (buf_pos_ >= buf_len_) {
    ret = fill_buffer();
  }
  if (OB_SUCC(ret)) {
    c = buf_[buf_pos_++];
  }
  return ret;
}

int ObCsvReader::read_line(common::ObIAllocator &allocator, char *&line_buf,
                           int64_t &line_len, bool &has_more)
{
  int ret = OB_SUCCESS;
  has_more = false;
  line_buf = NULL;
  line_len = 0;
  if (OB_ISNULL(buf_)) {
    ret = OB_NOT_INIT;
  } else {
    int64_t cap = 1024;
    char *out = static_cast<char *>(allocator.alloc(cap));
    if (OB_ISNULL(out)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    bool hit_eof_flush = false;
    char c = 0;
    while (OB_SUCC(ret)) {
      ret = read_char_locked(c);
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        if (0 == line_len) {
          break;  // clean EOF
        }
        hit_eof_flush = true;
        break;
      }
      if (line_len + 1 > cap) {
        int64_t ncap = cap * 2;
        char *nb = static_cast<char *>(allocator.alloc(ncap));
        if (OB_ISNULL(nb)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          break;
        }
        memcpy(nb, out, line_len);
        out = nb;
        cap = ncap;
      }
      if ('\n' == c || '\r' == c) {
        if ('\r' == c) {
          int64_t save = buf_pos_;
          char n = 0;
          int pk = read_char_locked(n);
          if (OB_ITER_END != pk && n != '\n') {
            buf_pos_ = save;
          }
        }
        line_no_++;
        break;
      }
      out[line_len++] = c;
    }
    if (OB_SUCC(ret) && (hit_eof_flush || line_len > 0)) {
      line_buf = out;
      has_more = true;
    }
  }
  return ret;
}

int ObCsvReader::read_row(ObIAllocator &allocator, ObCsvRecord &record,
                          ObArray<bool> &field_is_null, bool &has_more)
{
  int ret = OB_SUCCESS;
  record.reset();
  field_is_null.reset();
  has_more = false;
  if (OB_ISNULL(buf_)) {
    ret = OB_NOT_INIT;
  } else {
    // Phase 1: scan the raw bytes of one logical row (quotes may hide commas
    // and newlines). Phase 2: walk those bytes again, writing unquoted content
    // into an output block and recording field boundaries.
    int64_t raw_cap = 4096;
    char *raw = static_cast<char *>(allocator.alloc(raw_cap));
    int64_t raw_len = 0;
    bool quote_open = false;
    bool row_has_content = false;
    bool hit_eof_flush = false;
    char c = 0;
    if (OB_ISNULL(raw)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    while (OB_SUCC(ret)) {
      ret = read_char_locked(c);
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        if (!row_has_content) {
          break;  // clean EOF
        }
        hit_eof_flush = true;
        break;
      }
      if (raw_len + 1 > raw_cap) {
        int64_t ncap = raw_cap * 2;
        char *nb = static_cast<char *>(allocator.alloc(ncap));
        if (OB_ISNULL(nb)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          break;
        }
        memcpy(nb, raw, raw_len);
        raw = nb;
        raw_cap = ncap;
      }
      raw[raw_len++] = c;
      if (quote_open) {
        if ('"' == c) {
          quote_open = false;
        }
      } else if ('"' == c) {
        quote_open = true;
        row_has_content = true;
      } else if (',' == c) {
        row_has_content = true;
      } else if ('\n' == c || '\r' == c) {
        if ('\r' == c) {
          int64_t save = buf_pos_;
          char n = 0;
          int pk = read_char_locked(n);
          if (OB_ITER_END != pk && n != '\n') {
            buf_pos_ = save;
          }
        }
        line_no_++;
        // a lone newline before any content: skip empty lines
        if (raw_len <= 2 && !row_has_content) {
          raw_len = 0;
          continue;
        }
        break;
      } else {
        row_has_content = true;
      }
    }

    if (OB_SUCC(ret) && (hit_eof_flush || raw_len > 0)) {
      // strip trailing newline chars from raw
      while (raw_len > 0 && ('\n' == raw[raw_len - 1] || '\r' == raw[raw_len - 1])) {
        raw_len--;
      }
      // Phase 2: split & unquote
      int64_t out_cap = raw_len + 1;
      char *out = static_cast<char *>(allocator.alloc(out_cap));
      if (OB_ISNULL(out)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int64_t out_len = 0;
        int64_t field_start = 0;
        bool in_quote = false;
        bool field_quoted = false;
        bool field_started = false;
        for (int64_t i = 0; OB_SUCC(ret) && i < raw_len; i++) {
          char ch = raw[i];
          if (in_quote) {
            if ('"' == ch) {
              int64_t nxt = i + 1;
              if (nxt < raw_len && '"' == raw[nxt]) {
                out[out_len++] = '"';
                i++;
              } else {
                in_quote = false;
              }
            } else {
              out[out_len++] = ch;
            }
          } else if ('"' == ch && !field_started) {
            in_quote = true;
            field_quoted = true;
            field_started = true;
          } else if (',' == ch) {
            bool is_null = !field_quoted && out_len == field_start;
            if (OB_FAIL(record.field_ptrs_.push_back(out + field_start))) {
            } else if (OB_FAIL(record.field_lens_.push_back(out_len - field_start))) {
            } else if (OB_FAIL(field_is_null.push_back(is_null))) {
            }
            field_start = out_len;
            field_quoted = false;
            field_started = false;
          } else {
            out[out_len++] = ch;
            field_started = true;
          }
        }
        if (OB_SUCC(ret)) {
          bool is_null = !field_quoted && out_len == field_start;
          if (OB_FAIL(record.field_ptrs_.push_back(out + field_start))) {
          } else if (OB_FAIL(record.field_lens_.push_back(out_len - field_start))) {
          } else if (OB_FAIL(field_is_null.push_back(is_null))) {
          } else {
            has_more = true;
          }
        }
      }
    }
  }
  return ret;
}

int ObCsvWriter::open(const char *path)
{
  int ret = OB_SUCCESS;
  close();
  fd_ = fopen(path, "wb");
  if (nullptr == fd_) {
    ret = OB_FILE_NOT_OPENED;
  }
  return ret;
}

void ObCsvWriter::close()
{
  if (nullptr != fd_) {
    fclose(fd_);
    fd_ = nullptr;
  }
}

int ObCsvWriter::write_field(const char *data, int64_t len, bool need_quote)
{
  int ret = OB_SUCCESS;
  if (need_quote) {
    if (1 != fwrite("\"", 1, 1, fd_)) {
      ret = OB_IO_ERROR;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < len; i++) {
      if (data[i] == '"') {
        if (1 != fwrite("\"\"", 1, 2, fd_)) {
          ret = OB_IO_ERROR;
        }
      } else if (1 != fwrite(data + i, 1, 1, fd_)) {
        ret = OB_IO_ERROR;
      }
    }
    if (OB_SUCC(ret) && 1 != fwrite("\"", 1, 1, fd_)) {
      ret = OB_IO_ERROR;
    }
  } else if (len > 0 && len != fwrite(data, 1, len, fd_)) {
    ret = OB_IO_ERROR;
  }
  return ret;
}

static bool csv_field_need_quote(const char *data, int64_t len)
{
  bool need = false;
  for (int64_t i = 0; !need && i < len; i++) {
    char c = data[i];
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      need = true;
    }
  }
  return need;
}

int ObCsvWriter::write_header(const ObIArray<ObString> &names)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < names.count(); i++) {
    if (i > 0 && 1 != fwrite(",", 1, 1, fd_)) {
      ret = OB_IO_ERROR;
    } else if (OB_FAIL(write_field(names.at(i).ptr(), names.at(i).length(),
                                   csv_field_need_quote(names.at(i).ptr(), names.at(i).length())))) {
    }
  }
  if (OB_SUCC(ret) && 1 != fwrite("\n", 1, 1, fd_)) {
    ret = OB_IO_ERROR;
  }
  return ret;
}

int ObCsvWriter::write_row(const ObIArray<ObString> &cells,
                           const ObArray<bool> &is_null,
                           const ObString &null_text)
{
  int ret = OB_SUCCESS;
  int64_t cnt = cells.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < cnt; i++) {
    if (i > 0 && 1 != fwrite(",", 1, 1, fd_)) {
      ret = OB_IO_ERROR;
    } else if (i < is_null.count() && is_null.at(i)) {
      if (null_text.length() > 0 &&
          null_text.length() != fwrite(null_text.ptr(), 1, null_text.length(), fd_)) {
        ret = OB_IO_ERROR;
      }
    } else {
      const ObString &cell = cells.at(i);
      if (OB_FAIL(write_field(cell.ptr(), cell.length(),
                              csv_field_need_quote(cell.ptr(), cell.length())))) {
      }
    }
  }
  if (OB_SUCC(ret) && 1 != fwrite("\n", 1, 1, fd_)) {
    ret = OB_IO_ERROR;
  }
  return ret;
}

bool ObFileGlobUtil::pattern_match(const char *pattern, int64_t plen, const char *str, int64_t slen)
{
  // iterative wildcard match: '?' any one char, '*' any sequence
  int64_t p = 0, s = 0, star_p = -1, star_s = -1;
  while (s < slen) {
    if (p < plen && (pattern[p] == '?' || pattern[p] == str[s])) {
      p++; s++;
    } else if (p < plen && pattern[p] == '*') {
      star_p = p++;
      star_s = s;
    } else if (star_p >= 0) {
      p = star_p + 1;
      s = ++star_s;
    } else {
      return false;
    }
  }
  while (p < plen && pattern[p] == '*') { p++; }
  return p == plen;
}

int ObFileGlobUtil::expand(ObIAllocator &allocator, const ObString &pattern,
                           ObArray<ObString> &files)
{
  int ret = OB_SUCCESS;
  // split into directory part (no meta chars) and the rest as pattern
  ObString dir = ObString("/");
  ObString pat = pattern;
  int64_t last_slash = -1;
  for (int64_t i = 0; i < pattern.length(); i++) {
    if ('/' == pattern.ptr()[i]) { last_slash = i; }
  }
  if (last_slash >= 0) {
    dir = ObString(static_cast<int32_t>(last_slash + 1), pattern.ptr());
    pat = ObString(static_cast<int32_t>(pattern.length() - last_slash - 1), pattern.ptr() + last_slash + 1);
  }
  char dir_buf[OB_MAX_FILE_NAME_LENGTH];
  if (dir.length() >= OB_MAX_FILE_NAME_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    memcpy(dir_buf, dir.ptr(), dir.length());
    dir_buf[dir.length()] = 0;
    DIR *d = opendir(dir.length() == 0 ? "." : dir_buf);
    if (nullptr == d) {
      ret = OB_FILE_NOT_OPENED;
    } else {
      struct dirent *ent = nullptr;
      while (OB_SUCC(ret) && nullptr != (ent = readdir(d))) {
        const char *name = ent->d_name;
        int64_t nlen = strlen(name);
        if (0 == strcmp(name, ".") || 0 == strcmp(name, "..")) {
          continue;
        }
        if (pattern_match(pat.ptr(), pat.length(), name, nlen)) {
          ObString full;
          int64_t need = dir.length() + nlen + 1;
          char *buf = static_cast<char *>(allocator.alloc(need));
          if (nullptr == buf) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            memcpy(buf, dir.ptr(), dir.length());
            memcpy(buf + dir.length(), name, nlen);
            buf[need - 1] = 0;
            full.assign_ptr(buf, static_cast<int32_t>(need - 1));
            if (OB_FAIL(files.push_back(full))) {
            }
          }
        }
      }
      closedir(d);
      // deterministic output order
      if (OB_SUCC(ret)) {
        for (int64_t i = 1; i < files.count(); i++) {
          ObString key = files.at(i);
          int64_t j = i - 1;
          while (j >= 0 && files.at(j).compare(key) > 0) {
            files.at(j + 1) = files.at(j);
            j--;
          }
          files.at(j + 1) = key;
        }
      }
    }
  }
  return ret;
}

// ===================== minimal parquet reader =====================

int ObFileColumnSchema::dedup_column_name(common::ObIAllocator &allocator,
                                          const common::ObString &raw_name,
                                          const common::ObIArray<common::ObString> &existing,
                                          common::ObString &final_name)
{
  int ret = OB_SUCCESS;
  ObString cur = raw_name;
  for (int64_t suffix = 2; OB_SUCC(ret); suffix++) {
    bool dup = false;
    for (int64_t i = 0; !dup && i < existing.count(); i++) {
      dup = (existing.at(i).case_compare(cur) == 0);
    }
    if (!dup) {
      break;
    }
    char buf[OB_MAX_COLUMN_NAME_LENGTH];
    if (OB_FAIL(databuff_printf(buf, sizeof(buf), "%.*s__%ld",
                                static_cast<int>(cur.length()), cur.ptr(), suffix))) {
    } else if (OB_FAIL(ob_write_string(allocator, ObString(buf), cur))) {
    }
  }
  if (OB_SUCC(ret)) {
    final_name = cur;
  }
  return ret;
}

namespace {

// thrift-compact protocol constants
enum ThriftType
{
  T_STOP = 0,
  T_TRUE = 1,
  T_FALSE = 2,
  T_I8 = 3,
  T_I16 = 4,
  T_I32 = 5,
  T_I64 = 6,
  T_DBL = 7,
  T_BIN = 8,
  T_LIST = 9,
  T_SET = 10,
  T_MAP = 11,
  T_STRUCT = 12,
};

// parquet physical types
enum ParquetType
{
  P_BOOLEAN = 0,
  P_INT32 = 1,
  P_INT64 = 2,
  P_INT96 = 3,
  P_FLOAT = 4,
  P_DOUBLE = 5,
  P_BYTE_ARRAY = 6,
  P_FIXED_LEN = 7,
};

// parquet encodings
enum ParquetEncoding
{
  E_PLAIN = 0,
  E_PLAIN_DICT = 2,
  E_RLE = 3,
  E_DELTA_BINARY_PACKED = 5,
  E_DELTA_LENGTH_BYTE_ARRAY = 6,
  E_DELTA_BYTE_ARRAY = 7,
  E_RLE_DICT = 8,
};

struct TCompactReader
{
  const char *p_;
  const char *end_;
  int16_t last_fid_;
  int16_t fid_stack_[16];
  int32_t fid_depth_;
  TCompactReader(const char *data, int64_t len)
      : p_(data), end_(data + len), last_fid_(0), fid_depth_(0)
  {}
  int read_byte(uint64_t &v)
  {
    int ret = OB_SUCCESS;
    if (p_ >= end_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      v = static_cast<uint8_t>(*p_++);
    }
    return ret;
  }
  int read_varint(uint64_t &v)
  {
    int ret = OB_SUCCESS;
    v = 0;
    int shift = 0;
    bool complete = false;
    while (p_ < end_) {
      uint8_t b = static_cast<uint8_t>(*p_++);
      v |= static_cast<uint64_t>(b & 0x7f) << shift;
      if (0 == (b & 0x80)) {
        complete = true;
        break;
      }
      shift += 7;
      if (shift > 63) {
        ret = OB_INVALID_ARGUMENT;
        break;
      }
    }
    if (OB_SUCC(ret) && !complete) {
      ret = OB_INVALID_ARGUMENT;
    }
    return ret;
  }
  static int64_t zigzag(uint64_t v)
  {
    return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1);
  }
  int read_i64(int64_t &out)
  {
    uint64_t v = 0;
    int ret = read_varint(v);
    out = zigzag(v);
    return ret;
  }
  // thrift-compact tracks the last field id per struct level
  void enter_struct() {
    if (fid_depth_ < 16) {
      fid_stack_[fid_depth_] = last_fid_;
    }
    fid_depth_++;
    last_fid_ = 0;
  }
  void leave_struct() {
    fid_depth_--;
    if (fid_depth_ >= 0 && fid_depth_ < 16) {
      last_fid_ = fid_stack_[fid_depth_];
    }
  }
  int read_field_begin(int16_t &fid, uint8_t &type)
  {
    int ret = OB_SUCCESS;
    if (p_ >= end_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      uint8_t h = static_cast<uint8_t>(*p_);
      if (T_STOP == h) {
        p_++;
        fid = 0;
        type = T_STOP;
      } else {
        uint8_t delta = (h >> 4) & 0xf;
        type = h & 0xf;
        p_++;
        if (0 == delta) {
          uint64_t z = 0;
          if (OB_FAIL(read_varint(z))) {
          } else {
            fid = static_cast<int16_t>(zigzag(z));
          }
        } else {
          fid = last_fid_ + delta;
        }
        if (OB_SUCC(ret)) {
          last_fid_ = fid;
        }
      }
    }
    return ret;
  }
  int read_binary(const char *&data, int64_t &len)
  {
    int ret = OB_SUCCESS;
    uint64_t n = 0;
    if (OB_FAIL(read_varint(n)) || p_ + n > end_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      data = p_;
      len = static_cast<int64_t>(n);
      p_ += n;
    }
    return ret;
  }
  int skip_value(uint8_t type);
};

int TCompactReader::skip_value(uint8_t type)
{
  int ret = OB_SUCCESS;
  switch (type) {
    case T_TRUE:
    case T_FALSE: {
      break;
    }
    case T_I8:
    case T_I16:
    case T_I32:
    case T_I64: {
      int64_t dummy = 0;
      ret = read_i64(dummy);
      break;
    }
    case T_DBL: {
      if (p_ + 8 > end_) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        p_ += 8;
      }
      break;
    }
    case T_BIN: {
      const char *data = NULL;
      int64_t len = 0;
      ret = read_binary(data, len);
      break;
    }
    case T_LIST:
    case T_SET: {
      uint64_t eh = 0;
      if (OB_FAIL(read_byte(eh))) {
      } else {
        uint8_t et = static_cast<uint8_t>(eh & 0xf);
        uint64_t cnt = eh >> 4;
        if (15 == cnt) {
          uint64_t n = 0;
          if (OB_FAIL(read_varint(n))) {
          } else {
            cnt = n;
          }
        }
        for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
          if (OB_FAIL(skip_value(et))) {
          }
        }
      }
      break;
    }
    case T_STRUCT: {
      enter_struct();
      int16_t fid = 0;
      uint8_t ftype = T_STOP;
      while (OB_SUCC(ret)) {
        if (OB_FAIL(read_field_begin(fid, ftype))) {
        } else if (T_STOP == ftype) {
          leave_struct();
          break;
        } else if (OB_FAIL(skip_value(ftype))) {
        }
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

struct ParquetLeaf
{
  common::ObString name_;
  int32_t type_;
  int32_t repetition_type_;
  int32_t type_length_;
  int32_t converted_type_;
  int32_t scale_;
  int32_t precision_;
  ParquetLeaf()
      : type_(-1), repetition_type_(0), type_length_(-1),
        converted_type_(-1), scale_(0), precision_(0) {}
};

struct ParquetChunk
{
  int32_t type_;
  int32_t codec_;
  int64_t num_values_;
  int64_t data_page_offset_;
  int64_t dictionary_page_offset_;
  ParquetChunk()
      : type_(-1), codec_(0), num_values_(0),
        data_page_offset_(-1), dictionary_page_offset_(-1) {}
};

int parse_schema_element(TCompactReader &r, ParquetLeaf &leaf, int32_t &num_children)
{
  int ret = OB_SUCCESS;
  leaf = ParquetLeaf();
  num_children = 0;
  r.enter_struct();
  int16_t fid = 0;
  uint8_t type = T_STOP;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(r.read_field_begin(fid, type))) {
    } else if (T_STOP == type) {
      r.leave_struct();
      break;
    } else if (1 == fid && (T_I32 == type || T_I64 == type)) {
      int64_t tmp = 0;
      if (OB_FAIL(r.read_i64(tmp))) {
      } else {
        leaf.type_ = static_cast<int32_t>(tmp);
      }
    } else if (4 == fid && T_BIN == type) {
      const char *data = NULL;
      int64_t len = 0;
      if (OB_FAIL(r.read_binary(data, len))) {
      } else {
        leaf.name_ = ObString(static_cast<int32_t>(len), data);
      }
    } else if (5 == fid && (T_I32 == type || T_I64 == type)) {
      int64_t tmp = 0;
      if (OB_FAIL(r.read_i64(tmp))) {
      } else {
        num_children = static_cast<int32_t>(tmp);
      }
    } else if ((2 == fid || 3 == fid || 6 == fid || 7 == fid || 8 == fid)
               && (T_I32 == type || T_I64 == type)) {
      int64_t tmp = 0;
      if (OB_FAIL(r.read_i64(tmp))) {
      } else if (2 == fid) {
        leaf.type_length_ = static_cast<int32_t>(tmp);
      } else if (3 == fid) {
        leaf.repetition_type_ = static_cast<int32_t>(tmp);
      } else if (6 == fid) {
        leaf.converted_type_ = static_cast<int32_t>(tmp);
      } else if (7 == fid) {
        leaf.scale_ = static_cast<int32_t>(tmp);
      } else {
        leaf.precision_ = static_cast<int32_t>(tmp);
      }
    } else if (OB_FAIL(r.skip_value(type))) {
    }
  }
  return ret;
}

int parse_column_meta(TCompactReader &r, ParquetChunk &chunk)
{
  int ret = OB_SUCCESS;
  r.enter_struct();
  int16_t fid = 0;
  uint8_t type = T_STOP;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(r.read_field_begin(fid, type))) {
    } else if (T_STOP == type) {
      r.leave_struct();
      break;
    } else if (1 == fid && (T_I32 == type || T_I64 == type)) {
      int64_t tmp = 0;
      if (OB_FAIL(r.read_i64(tmp))) {
      } else {
        chunk.type_ = static_cast<int32_t>(tmp);
      }
    } else if (4 == fid && (T_I32 == type || T_I64 == type)) {
      int64_t tmp = 0;
      if (OB_FAIL(r.read_i64(tmp))) {
      } else {
        chunk.codec_ = static_cast<int32_t>(tmp);
      }
    } else if (5 == fid && (T_I32 == type || T_I64 == type)) {
      if (OB_FAIL(r.read_i64(chunk.num_values_))) {
      }
    } else if (9 == fid && (T_I32 == type || T_I64 == type)) {
      if (OB_FAIL(r.read_i64(chunk.data_page_offset_))) {
      }
    } else if (11 == fid && (T_I32 == type || T_I64 == type)) {
      if (OB_FAIL(r.read_i64(chunk.dictionary_page_offset_))) {
      }
    } else if (OB_FAIL(r.skip_value(type))) {
    }
  }
  return ret;
}

int parse_row_group(TCompactReader &r, common::ObIArray<ParquetChunk> &chunks,
                    int64_t &num_rows)
{
  int ret = OB_SUCCESS;
  num_rows = 0;
  r.enter_struct();
  int16_t fid = 0;
  uint8_t type = T_STOP;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(r.read_field_begin(fid, type))) {
    } else if (T_STOP == type) {
      r.leave_struct();
      break;
    } else if (1 == fid && (T_LIST == type || T_SET == type)) {
      // columns list<ColumnChunk>
      uint64_t eh = 0;
      if (OB_FAIL(r.read_byte(eh))) {
      } else {
        uint8_t et = static_cast<uint8_t>(eh & 0xf);
        uint64_t cnt = eh >> 4;
        if (15 == cnt) {
          uint64_t n = 0;
          if (OB_FAIL(r.read_varint(n))) {
          } else {
            cnt = n;
          }
        }
        for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
          // ColumnChunk struct: 2:file_offset, 3:meta_data
          int16_t cfid = 0;
          uint8_t ctype = T_STOP;
          bool has_meta = false;
          ParquetChunk chunk;
          TCompactReader *chunk_reader = &r;
          chunk_reader->enter_struct();
          while (OB_SUCC(ret)) {
            if (OB_FAIL(chunk_reader->read_field_begin(cfid, ctype))) {
            } else if (T_STOP == ctype) {
              chunk_reader->leave_struct();
              break;
            } else if (3 == cfid && T_STRUCT == ctype) {
              has_meta = true;
              if (OB_FAIL(parse_column_meta(r, chunk))) {
              }
            } else if (OB_FAIL(chunk_reader->skip_value(ctype))) {
            }
          }
          if (OB_SUCC(ret) && has_meta) {
            if (OB_FAIL(chunks.push_back(chunk))) {
            }
          }
        }
      }
    } else if (3 == fid && (T_I32 == type || T_I64 == type)) {
      if (OB_FAIL(r.read_i64(num_rows))) {
      }
    } else if (OB_FAIL(r.skip_value(type))) {
    }
  }
  return ret;
}

int decimal_integer_to_cell(common::ObIAllocator &allocator, __int128 value,
                            int32_t scale, ObObjParam &cell)
{
  int ret = OB_SUCCESS;
  if (scale < 0 || scale > 38) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const bool negative = value < 0;
    unsigned __int128 magnitude = negative
        ? static_cast<unsigned __int128>(-(value + 1)) + 1
        : static_cast<unsigned __int128>(value);
    char reversed[48];
    int32_t digits = 0;
    do {
      reversed[digits++] = static_cast<char>('0' + magnitude % 10);
      magnitude /= 10;
    } while (magnitude != 0);
    char text[80];
    int32_t pos = 0;
    if (negative) {
      text[pos++] = '-';
    }
    if (scale >= digits) {
      text[pos++] = '0';
      if (scale > 0) {
        text[pos++] = '.';
        for (int32_t i = digits; i < scale; ++i) {
          text[pos++] = '0';
        }
      }
    }
    for (int32_t i = digits - 1; i >= 0; --i) {
      if (scale > 0 && scale < digits && i + 1 == scale) {
        text[pos++] = '.';
      }
      text[pos++] = reversed[i];
    }
    number::ObNumber number;
    if (OB_FAIL(number.from(text, pos, allocator))) {
    } else {
      cell.set_number(number);
    }
  }
  return ret;
}

// Decodes one PLAIN value of the given parquet type into an ObObjParam cell.
int plain_value_to_cell(common::ObIAllocator &allocator, const ParquetLeaf &leaf,
                        const char *&data, int64_t remain, ObObjParam &cell)
{
  int ret = OB_SUCCESS;
  const int32_t ptype = leaf.type_;
  cell.set_null();
  switch (ptype) {
    case P_BOOLEAN: {
      if (remain < 1) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        cell.set_bool(0 != data[0]);
        data += 1;
      }
      break;
    }
    case P_INT32: {
      if (remain < 4) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        int32_t v = 0;
        MEMCPY(&v, data, 4);
        if (5 == leaf.converted_type_) {
          ret = decimal_integer_to_cell(allocator, v, leaf.scale_, cell);
        } else {
          cell.set_int(v);
        }
        data += 4;
      }
      break;
    }
    case P_INT64: {
      if (remain < 8) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        int64_t v = 0;
        MEMCPY(&v, data, 8);
        if (5 == leaf.converted_type_) {
          ret = decimal_integer_to_cell(allocator, v, leaf.scale_, cell);
        } else {
          cell.set_int(v);
        }
        data += 8;
      }
      break;
    }
    case P_FLOAT: {
      if (remain < 4) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        float v = 0;
        MEMCPY(&v, data, 4);
        cell.set_double(static_cast<double>(v));
        data += 4;
      }
      break;
    }
    case P_DOUBLE: {
      if (remain < 8) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        double v = 0;
        MEMCPY(&v, data, 8);
        cell.set_double(v);
        data += 8;
      }
      break;
    }
    case P_BYTE_ARRAY: {
      if (remain < 4) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        int32_t len = 0;
        MEMCPY(&len, data, 4);
        data += 4;
        if (len < 0 || remain - 4 < len) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          char *copy = static_cast<char *>(allocator.alloc(len));
          if (OB_ISNULL(copy) && len > 0) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            if (len > 0) {
              MEMCPY(copy, data, len);
            }
            cell.set_varchar(ObString(len, copy));
            cell.set_collation_type(CS_TYPE_UTF8MB4_BIN);
            data += len;
          }
        }
      }
      break;
    }
    case P_FIXED_LEN: {
      // Parquet DECIMAL stores a signed big-endian integer. Convert it through
      // its exact decimal text so no precision is lost through a double.
      const int32_t len = leaf.type_length_;
      if (5 != leaf.converted_type_ || len <= 0 || len > 16
          || leaf.scale_ < 0 || leaf.scale_ > 38 || remain < len) {
        ret = OB_NOT_SUPPORTED;
      } else {
        unsigned __int128 raw = 0;
        for (int32_t i = 0; i < len; ++i) {
          raw = (raw << 8) | static_cast<uint8_t>(data[i]);
        }
        const bool negative = 0 != (data[0] & 0x80);
        if (negative && len < 16) {
          raw -= static_cast<unsigned __int128>(1) << (len * 8);
        }
        const __int128 value = static_cast<__int128>(raw);
        if (OB_FAIL(decimal_integer_to_cell(allocator, value, leaf.scale_, cell))) {
        } else {
          data += len;
        }
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

int64_t parquet_fixed_width(const ParquetLeaf &leaf)
{
  switch (leaf.type_) {
    case P_INT32: return 4;
    case P_INT64: return 8;
    case P_FLOAT: return 4;
    case P_DOUBLE: return 8;
    case P_FIXED_LEN:
      return 5 == leaf.converted_type_ ? leaf.type_length_ : -1;
    default: return -1;  // variable width (BOOLEAN is bit packed) or unsupported
  }
}

common::ObObjType parquet_type_to_obj_type(const ParquetLeaf &leaf)
{
  switch (leaf.type_) {
    case P_BOOLEAN: return ObTinyIntType;
    case P_INT32:
    case P_INT64:
      return 5 == leaf.converted_type_ ? ObNumberType : ObIntType;
    case P_FLOAT:
    case P_DOUBLE: return ObDoubleType;
    case P_BYTE_ARRAY: return ObVarcharType;
    case P_FIXED_LEN:
      return 5 == leaf.converted_type_ ? ObNumberType : ObNullType;
    default: return ObNullType;
  }
}

// Decodes RLE-bit-packed-hybrid dictionary indices. Width is in bits; values
// are indices into the dictionary array.
// Minimal snappy raw-block decompressor (the parquet codec=1 format). Output
// goes into `out` (allocator-backed); uncompressed length is the leading
// varint. Covers literal and all three copy tag forms.
int snappy_decompress(common::ObIAllocator &allocator, const char *data,
                      int64_t len, char *&out, int64_t &out_len)
{
  int ret = OB_SUCCESS;
  out = NULL;
  out_len = 0;
  const char *p = data;
  const char *end = data + len;
  uint64_t ulen = 0;
  int shift = 0;
  while (p < end) {
    uint8_t b = static_cast<uint8_t>(*p++);
    ulen |= static_cast<uint64_t>(b & 0x7f) << shift;
    if (0 == (b & 0x80)) {
      break;
    }
    shift += 7;
    if (shift > 32) {
      ret = OB_INVALID_ARGUMENT;
      break;
    }
  }
  if (OB_SUCC(ret)) {
    if (ulen <= 0 || ulen > (64ULL << 20)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_ISNULL(out = static_cast<char *>(allocator.alloc(ulen)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  }
  if (OB_SUCC(ret)) {
    char *w = out;
    char *wend = out + ulen;
    while (OB_SUCC(ret) && p < end && w < wend) {
      uint8_t tag = static_cast<uint8_t>(*p++);
      if (0 == (tag & 3)) {
        // literal
        uint64_t l = tag >> 2;
        if (l >= 60) {
          int64_t extra = static_cast<int64_t>(l) - 59;
          if (p + extra > end) {
            ret = OB_INVALID_ARGUMENT;
            break;
          }
          l = 0;
          for (int64_t i = 0; i < extra; ++i) {
            l |= static_cast<uint64_t>(static_cast<uint8_t>(*p++)) << (8 * i);
          }
        }
        l += 1;
        if (p + static_cast<int64_t>(l) > end || w + l > wend) {
          ret = OB_INVALID_ARGUMENT;
          break;
        }
        MEMCPY(w, p, l);
        p += l;
        w += l;
      } else {
        // copy with offset
        int64_t clen = 0;
        int64_t off = 0;
        if (1 == (tag & 3)) {
          if (p + 1 > end) {
            ret = OB_INVALID_ARGUMENT;
            break;
          }
          clen = 4 + ((tag >> 2) & 0x7);
          off = (static_cast<int64_t>(tag >> 5) << 8)
                + static_cast<uint8_t>(*p++);
        } else if (2 == (tag & 3)) {
          if (p + 2 > end) {
            ret = OB_INVALID_ARGUMENT;
            break;
          }
          clen = (tag >> 2) + 1;
          off = static_cast<uint8_t>(p[0])
                | (static_cast<uint64_t>(static_cast<uint8_t>(p[1])) << 8);
          p += 2;
        } else {
          if (p + 4 > end) {
            ret = OB_INVALID_ARGUMENT;
            break;
          }
          clen = (tag >> 2) + 1;
          off = static_cast<uint8_t>(p[0])
                | (static_cast<uint64_t>(static_cast<uint8_t>(p[1])) << 8)
                | (static_cast<uint64_t>(static_cast<uint8_t>(p[2])) << 16)
                | (static_cast<uint64_t>(static_cast<uint8_t>(p[3])) << 24);
          p += 4;
        }
        if (off <= 0 || off > w - out) {
          ret = OB_INVALID_ARGUMENT;
          break;
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < clen; ++i) {
          if (w >= wend) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            *w = *(w - off);
            ++w;
          }
        }
      }
    }
    if (OB_SUCC(ret) && w != wend) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_SUCC(ret)) {
    out_len = ulen;
  }
  return ret;
}

int read_hybrid_header(const char *&p, const char *end, uint64_t &header)
{
  int ret = OB_SUCCESS;
  header = 0;
  int32_t shift = 0;
  while (OB_SUCC(ret)) {
    if (p >= end || shift > 63) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const uint8_t byte = static_cast<uint8_t>(*p++);
      header |= static_cast<uint64_t>(byte & 0x7f) << shift;
      if (0 == (byte & 0x80)) {
        break;
      }
      shift += 7;
    }
  }
  return ret;
}

int read_little_endian_bits(const char *data, int64_t len, int64_t bit_offset,
                            int32_t bit_width, uint64_t &value)
{
  int ret = OB_SUCCESS;
  value = 0;
  if (bit_width < 0 || bit_width > 64 || bit_offset < 0
      || bit_offset > len * 8 || bit_width > len * 8 - bit_offset) {
    ret = OB_INVALID_ARGUMENT;
  } else if (bit_width > 0) {
    const int32_t shift = bit_offset % 8;
    const int32_t byte_cnt = (shift + bit_width + 7) / 8;
    const char *p = data + bit_offset / 8;
    unsigned __int128 packed = 0;
    for (int32_t i = 0; i < byte_cnt; ++i) {
      packed |= static_cast<unsigned __int128>(static_cast<uint8_t>(p[i])) << (8 * i);
    }
    value = static_cast<uint64_t>(packed >> shift);
    if (bit_width < 64) {
      value &= (1ULL << bit_width) - 1;
    }
  }
  return ret;
}

int delta_integer_to_cell(common::ObIAllocator &allocator, const ParquetLeaf &leaf,
                          uint64_t bits, ObObjParam &cell)
{
  int ret = OB_SUCCESS;
  if (P_INT32 == leaf.type_) {
    const uint32_t raw = static_cast<uint32_t>(bits);
    int32_t value = 0;
    MEMCPY(&value, &raw, sizeof(value));
    if (5 == leaf.converted_type_) {
      ret = decimal_integer_to_cell(allocator, value, leaf.scale_, cell);
    } else {
      cell.set_int(value);
    }
  } else if (P_INT64 == leaf.type_) {
    int64_t value = 0;
    MEMCPY(&value, &bits, sizeof(value));
    if (5 == leaf.converted_type_) {
      ret = decimal_integer_to_cell(allocator, value, leaf.scale_, cell);
    } else {
      cell.set_int(value);
    }
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

// DELTA_BINARY_PACKED stores a first value followed by blocks of deltas. Each
// block has a minimum delta, a contiguous bit-width table, then fixed-width,
// little-endian bit-packed residuals split across its miniblocks. Padding in
// the final block is consumed but not returned.
int decode_delta_binary_packed_values(const char *data, int64_t len, int64_t num_values,
                                      common::ObIArray<int64_t> &values, int64_t &consumed)
{
  int ret = OB_SUCCESS;
  const char *p = data;
  const char *end = data + len;
  uint64_t block_size = 0;
  uint64_t miniblock_cnt = 0;
  uint64_t declared_count = 0;
  uint64_t first_value = 0;
  values.reset();
  consumed = 0;
  if (num_values <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(read_hybrid_header(p, end, block_size))
             || OB_FAIL(read_hybrid_header(p, end, miniblock_cnt))
             || OB_FAIL(read_hybrid_header(p, end, declared_count))
             || OB_FAIL(read_hybrid_header(p, end, first_value))) {
  } else if (0 == block_size || 0 == miniblock_cnt || block_size % miniblock_cnt != 0
             || block_size > (1ULL << 20) || declared_count != static_cast<uint64_t>(num_values)) {
    ret = OB_INVALID_ARGUMENT;
  }
  const int64_t values_per_miniblock = 0 == miniblock_cnt ? 0 : block_size / miniblock_cnt;
  uint64_t current = static_cast<uint64_t>(TCompactReader::zigzag(first_value));
  if (OB_SUCC(ret)) {
    int64_t value = 0;
    MEMCPY(&value, &current, sizeof(value));
    if (OB_FAIL(values.push_back(value))) {
    }
  }
  while (OB_SUCC(ret) && values.count() < num_values) {
    uint64_t encoded_min_delta = 0;
    if (OB_FAIL(read_hybrid_header(p, end, encoded_min_delta))) {
      break;
    }
    const uint64_t min_delta = static_cast<uint64_t>(TCompactReader::zigzag(encoded_min_delta));
    ObSEArray<uint8_t, 16> bit_widths;
    for (uint64_t m = 0; OB_SUCC(ret) && m < miniblock_cnt; ++m) {
      if (p >= end || static_cast<uint8_t>(*p) > 64) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(bit_widths.push_back(static_cast<uint8_t>(*p++)))) {
      }
    }
    for (uint64_t m = 0; OB_SUCC(ret) && m < miniblock_cnt; ++m) {
      const int32_t bit_width = bit_widths.at(m);
      if (values_per_miniblock <= 0
          || values_per_miniblock > (INT64_MAX - 7) / MAX(1, bit_width)) {
        ret = OB_INVALID_ARGUMENT;
        break;
      }
      const int64_t miniblock_bytes = (values_per_miniblock * bit_width + 7) / 8;
      if (miniblock_bytes > end - p) {
        ret = OB_INVALID_ARGUMENT;
        break;
      }
      const char *miniblock = p;
      for (int64_t i = 0; OB_SUCC(ret) && i < values_per_miniblock && values.count() < num_values; ++i) {
        uint64_t residual = 0;
        if (OB_FAIL(read_little_endian_bits(miniblock, miniblock_bytes,
                                            i * bit_width, bit_width, residual))) {
        } else {
          current += min_delta + residual;
          int64_t value = 0;
          MEMCPY(&value, &current, sizeof(value));
          if (OB_FAIL(values.push_back(value))) {
          }
        }
      }
      p += miniblock_bytes;
    }
  }
  if (OB_SUCC(ret) && values.count() != num_values) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret)) {
    consumed = p - data;
  }
  return ret;
}

int decode_delta_binary_packed(common::ObIAllocator &allocator, const ParquetLeaf &leaf,
                               const char *data, int64_t len, int64_t num_values,
                               common::ObIArray<common::ObObjParam> &cells)
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 1024> values;
  int64_t consumed = 0;
  if ((P_INT32 != leaf.type_ && P_INT64 != leaf.type_)) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(decode_delta_binary_packed_values(data, len, num_values,
                                                       values, consumed))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < values.count(); ++i) {
      uint64_t bits = 0;
      const int64_t value = values.at(i);
      MEMCPY(&bits, &value, sizeof(bits));
      ObObjParam cell;
      if (OB_FAIL(delta_integer_to_cell(allocator, leaf, bits, cell))
          || OB_FAIL(cells.push_back(cell))) {
      }
    }
  }
  return ret;
}

int make_byte_array_cell(common::ObIAllocator &allocator, const char *data, int64_t len,
                         ObObjParam &cell)
{
  int ret = OB_SUCCESS;
  if (len < 0 || len > INT32_MAX) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    char *copy = static_cast<char *>(allocator.alloc(len));
    if (OB_ISNULL(copy) && len > 0) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      if (len > 0) {
        MEMCPY(copy, data, len);
      }
      cell.set_varchar(ObString(static_cast<int32_t>(len), copy));
      cell.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
  }
  return ret;
}

int decode_delta_length_byte_array(common::ObIAllocator &allocator, const char *data,
                                   int64_t len, int64_t num_values,
                                   common::ObIArray<common::ObObjParam> &cells)
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 1024> lengths;
  int64_t lengths_bytes = 0;
  if (OB_FAIL(decode_delta_binary_packed_values(data, len, num_values,
                                                lengths, lengths_bytes))) {
  } else {
    const char *p = data + lengths_bytes;
    const char *end = data + len;
    for (int64_t i = 0; OB_SUCC(ret) && i < lengths.count(); ++i) {
      const int64_t value_len = lengths.at(i);
      if (value_len < 0 || value_len > end - p) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        ObObjParam cell;
        if (OB_FAIL(make_byte_array_cell(allocator, p, value_len, cell))
            || OB_FAIL(cells.push_back(cell))) {
        } else {
          p += value_len;
        }
      }
    }
    if (OB_SUCC(ret) && p != end) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

int decode_delta_byte_array(common::ObIAllocator &allocator, const char *data,
                            int64_t len, int64_t num_values,
                            common::ObIArray<common::ObObjParam> &cells)
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 1024> prefix_lengths;
  ObSEArray<int64_t, 1024> suffix_lengths;
  int64_t prefix_bytes = 0;
  int64_t suffix_bytes = 0;
  if (OB_FAIL(decode_delta_binary_packed_values(data, len, num_values,
                                                prefix_lengths, prefix_bytes))
      || OB_FAIL(decode_delta_binary_packed_values(data + prefix_bytes, len - prefix_bytes,
                                                   num_values, suffix_lengths, suffix_bytes))) {
  } else {
    const char *p = data + prefix_bytes + suffix_bytes;
    const char *end = data + len;
    const char *previous = NULL;
    int64_t previous_len = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < num_values; ++i) {
      const int64_t prefix_len = prefix_lengths.at(i);
      const int64_t suffix_len = suffix_lengths.at(i);
      if (prefix_len < 0 || prefix_len > previous_len || suffix_len < 0
          || suffix_len > end - p || prefix_len + suffix_len > INT32_MAX) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        char *value = static_cast<char *>(allocator.alloc(prefix_len + suffix_len));
        if (OB_ISNULL(value) && prefix_len + suffix_len > 0) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          if (prefix_len > 0) {
            MEMCPY(value, previous, prefix_len);
          }
          if (suffix_len > 0) {
            MEMCPY(value + prefix_len, p, suffix_len);
          }
          ObObjParam cell;
          cell.set_varchar(ObString(static_cast<int32_t>(prefix_len + suffix_len), value));
          cell.set_collation_type(CS_TYPE_UTF8MB4_BIN);
          if (OB_FAIL(cells.push_back(cell))) {
          } else {
            previous = value;
            previous_len = prefix_len + suffix_len;
            p += suffix_len;
          }
        }
      }
    }
    if (OB_SUCC(ret) && p != end) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("delta byte array decode failed", K(ret), K(num_values), K(len),
             K(prefix_bytes), K(suffix_bytes), K(prefix_lengths.count()),
             K(suffix_lengths.count()), K(cells.count()));
  }
  return ret;
}

int decode_rle_indices(const char *data, int64_t len, int32_t bit_width,
                       int64_t num_values, const common::ObIArray<ObObjParam> &dict,
                       common::ObIArray<common::ObObjParam> &cells)
{
  int ret = OB_SUCCESS;
  const char *p = data;
  const char *end = data + len;
  int64_t got = 0;
  if (bit_width < 0 || bit_width > 32) {
    ret = OB_INVALID_ARGUMENT;
  }
  const int64_t byte_width = (bit_width + 7) / 8;
  while (OB_SUCC(ret) && got < num_values && p < end) {
    uint64_t header = 0;
    if (OB_FAIL(read_hybrid_header(p, end, header))) {
    } else if (0 == (header & 1)) {
      const uint64_t run = header >> 1;
      if (p + byte_width > end) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        int64_t idx = 0;
        MEMCPY(&idx, p, byte_width);
        p += byte_width;
        for (uint64_t i = 0; OB_SUCC(ret) && i < run && got < num_values; ++i) {
          if (idx < 0 || idx >= dict.count()) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(cells.push_back(dict.at(idx)))) {
          } else {
            got++;
          }
        }
      }
    } else {
      const uint64_t groups = header >> 1;
      const int64_t vals = groups * 8;
      const int64_t bytes = vals * bit_width / 8;
      if (p + bytes > end) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        uint64_t bitbuf = 0;
        int32_t bitcnt = 0;
        const char *bp = p;
        for (int64_t i = 0; OB_SUCC(ret) && i < vals && got < num_values; ++i) {
          while (bitcnt < bit_width) {
            bitbuf |= static_cast<uint64_t>(static_cast<uint8_t>(*bp++)) << bitcnt;
            bitcnt += 8;
          }
          const int64_t idx = static_cast<int64_t>(bitbuf & ((1ULL << bit_width) - 1));
          bitbuf >>= bit_width;
          bitcnt -= bit_width;
          if (idx < 0 || idx >= dict.count()) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(cells.push_back(dict.at(idx)))) {
          } else {
            got++;
          }
        }
        p += bytes;
      }
    }
  }
  if (OB_SUCC(ret) && got != num_values) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int decode_definition_levels(const char *data, int64_t len, int64_t num_values,
                             common::ObIArray<bool> &defined, int64_t &nonnull_count)
{
  int ret = OB_SUCCESS;
  const char *p = data;
  const char *end = data + len;
  nonnull_count = 0;
  while (OB_SUCC(ret) && defined.count() < num_values && p < end) {
    uint64_t header = 0;
    if (OB_FAIL(read_hybrid_header(p, end, header))) {
    } else if (0 == (header & 1)) {
      const uint64_t run = header >> 1;
      if (p >= end) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const bool value = 0 != static_cast<uint8_t>(*p++);
        for (uint64_t i = 0; OB_SUCC(ret) && i < run && defined.count() < num_values; ++i) {
          if (OB_FAIL(defined.push_back(value))) {
          } else if (value) {
            ++nonnull_count;
          }
        }
      }
    } else {
      const uint64_t groups = header >> 1;
      const uint64_t bits = groups * 8;
      if (groups > (static_cast<uint64_t>(end - p))) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        for (uint64_t i = 0; OB_SUCC(ret) && i < bits && defined.count() < num_values; ++i) {
          const bool value = 0 != (static_cast<uint8_t>(p[i / 8]) & (1U << (i % 8)));
          if (OB_FAIL(defined.push_back(value))) {
          } else if (value) {
            ++nonnull_count;
          }
        }
        p += bits / 8;
      }
    }
  }
  if (OB_SUCC(ret) && defined.count() != num_values) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

} // end anonymous namespace

namespace
{
constexpr int64_t PARQUET_MAX_METADATA_BYTES = 64LL << 20;
constexpr int64_t PARQUET_MAX_PAGE_BYTES = 64LL << 20;
constexpr int64_t PARQUET_PAGE_HEADER_READ_BYTES = 64LL << 10;

struct ParquetColumnCursor
{
  ParquetColumnCursor() : leaf_(NULL), chunk_(NULL), next_page_offset_(0),
                          decoded_(0), page_pos_(0) {}
  void reset()
  {
    leaf_ = NULL;
    chunk_ = NULL;
    next_page_offset_ = 0;
    decoded_ = 0;
    page_pos_ = 0;
    cells_.reset();
    dict_.reset();
    page_allocator_.reset();
    dict_allocator_.reset();
  }
  const ParquetLeaf *leaf_;
  const ParquetChunk *chunk_;
  int64_t next_page_offset_;
  int64_t decoded_;
  int64_t page_pos_;
  common::ObArray<ObObjParam> cells_;
  common::ObArray<ObObjParam> dict_;
  common::ObArenaAllocator page_allocator_;
  common::ObArenaAllocator dict_allocator_;
};
} // end anonymous namespace

struct ObParquetStreamReader::Impl
{
  Impl() : fd_(NULL), metadata_(NULL), file_len_(0), metadata_offset_(0),
           file_rows_(0), current_group_(0), group_row_(0), cursors_(NULL) {}
  FILE *fd_;
  char *metadata_;
  int64_t file_len_;
  int64_t metadata_offset_;
  int64_t file_rows_;
  int64_t current_group_;
  int64_t group_row_;
  common::ObArray<ParquetLeaf> leaves_;
  common::ObArray<ParquetChunk> chunks_;
  common::ObArray<int64_t> row_group_rows_;
  common::ObArray<bool> column_needed_;
  ParquetColumnCursor *cursors_;
};

namespace
{
int parquet_read_footer(FILE *fd, common::ObIAllocator &allocator,
                        ObFileColumnSchema &schema,
                        char *&metadata, int64_t &file_len, int64_t &metadata_offset,
                        common::ObIArray<ParquetLeaf> &leaves,
                        common::ObIArray<ParquetChunk> &chunks,
                        common::ObIArray<int64_t> &row_group_rows,
                        int64_t &file_rows)
{
  int ret = OB_SUCCESS;
  metadata = NULL;
  file_len = 0;
  metadata_offset = 0;
  file_rows = 0;
  char magic[4];
  char tail[8];
  if (OB_ISNULL(fd) || 0 != fseek(fd, 0, SEEK_END)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((file_len = ftell(fd)) < 12) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet file is too small");
  } else if (0 != fseek(fd, 0, SEEK_SET) || 4 != fread(magic, 1, 4, fd)
             || 0 != MEMCMP(magic, "PAR1", 4)
             || 0 != fseek(fd, -8, SEEK_END) || 8 != fread(tail, 1, 8, fd)
             || 0 != MEMCMP(tail + 4, "PAR1", 4)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "not a parquet file");
  } else {
    const uint32_t meta_len = static_cast<uint8_t>(tail[0])
        | (static_cast<uint32_t>(static_cast<uint8_t>(tail[1])) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(tail[2])) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(tail[3])) << 24);
    if (0 == meta_len || meta_len > PARQUET_MAX_METADATA_BYTES
        || static_cast<int64_t>(meta_len) > file_len - 12) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet metadata out of range");
    } else {
      metadata_offset = file_len - 8 - meta_len;
      if (0 != fseek(fd, metadata_offset, SEEK_SET)
          || OB_ISNULL(metadata = static_cast<char *>(malloc(meta_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (meta_len != fread(metadata, 1, meta_len, fd)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet metadata read truncated");
      }
    }
  }
  bool seen_schema = false;
  bool seen_row_groups = false;
  if (OB_SUCC(ret)) {
    TCompactReader r(metadata, metadata_offset >= 0 ? file_len - 8 - metadata_offset : 0);
    r.enter_struct();
    int16_t fid = 0;
    uint8_t type = T_STOP;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(r.read_field_begin(fid, type))) {
        r.leave_struct();
        ret = OB_SUCCESS;
        break;
      } else if (T_STOP == type) {
        r.leave_struct();
        break;
      } else if (2 == fid && (T_LIST == type || T_SET == type)) {
        uint64_t eh = 0;
        if (OB_FAIL(r.read_byte(eh))) {
        } else {
          const uint8_t et = static_cast<uint8_t>(eh & 0xf);
          uint64_t cnt = eh >> 4;
          if (15 == cnt) {
            uint64_t n = 0;
            if (OB_FAIL(r.read_varint(n))) {
            } else {
              cnt = n;
            }
          }
          if (T_STRUCT != et) {
            ret = OB_INVALID_ARGUMENT;
          }
          for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
            ParquetLeaf leaf;
            int32_t num_children = 0;
            if (OB_FAIL(parse_schema_element(r, leaf, num_children))) {
            } else if (num_children > 0) {
              if (!leaves.empty()) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "nested parquet schema");
              }
            } else if (parquet_type_to_obj_type(leaf) == ObNullType) {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "parquet physical type");
            } else if (OB_FAIL(leaves.push_back(leaf))) {
            }
          }
          seen_schema = true;
        }
      } else if (3 == fid && (T_I32 == type || T_I64 == type)) {
        if (OB_FAIL(r.read_i64(file_rows))) {
        }
      } else if (4 == fid && (T_LIST == type || T_SET == type)) {
        seen_row_groups = true;
        uint64_t eh = 0;
        if (OB_FAIL(r.read_byte(eh))) {
        } else {
          const uint8_t et = static_cast<uint8_t>(eh & 0xf);
          uint64_t cnt = eh >> 4;
          if (15 == cnt) {
            uint64_t n = 0;
            if (OB_FAIL(r.read_varint(n))) {
            } else {
              cnt = n;
            }
          }
          if (T_STRUCT != et) {
            ret = OB_INVALID_ARGUMENT;
          }
          for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
            int64_t group_rows = 0;
            if (OB_FAIL(parse_row_group(r, chunks, group_rows))) {
              LOG_WARN("parquet row group parse failed", K(ret), K(i), K(cnt),
                       K(group_rows), "offset", r.p_ - metadata);
            } else if (group_rows < 0 || OB_FAIL(row_group_rows.push_back(group_rows))) {
              ret = OB_INVALID_ARGUMENT;
            }
          }
        }
      } else if (OB_FAIL(r.skip_value(type))) {
      }
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("parquet footer parse failed", K(ret), K(fid), K(type),
               "offset", r.p_ - metadata, K(leaves.count()), K(chunks.count()),
               K(row_group_rows.count()));
    }
  }
  if (OB_SUCC(ret) && (!seen_schema || !seen_row_groups || leaves.empty()
                       || chunks.empty() || file_rows < 0
                       || chunks.count() != leaves.count() * row_group_rows.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet metadata mismatch");
    LOG_WARN("parquet metadata mismatch", K(seen_schema), K(seen_row_groups),
              K(leaves.count()), K(chunks.count()), K(row_group_rows.count()), K(file_rows));
  }
  int64_t total_rows = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_group_rows.count(); ++i) {
    if (row_group_rows.at(i) > file_rows - total_rows) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      total_rows += row_group_rows.at(i);
    }
  }
  if (OB_SUCC(ret) && total_rows != file_rows) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet row count mismatch");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < leaves.count(); ++i) {
    common::ObString name;
    if (OB_FAIL(ObFileColumnSchema::dedup_column_name(allocator, leaves.at(i).name_,
                                                      schema.names_, name))) {
    } else if (OB_FAIL(schema.names_.push_back(name))) {
    } else if (OB_FAIL(schema.types_.push_back(parquet_type_to_obj_type(leaves.at(i))))) {
    } else if (OB_FAIL(schema.null_flags_.push_back(1 == leaves.at(i).repetition_type_))) {
    }
  }
  return ret;
}

int parquet_start_group(ObParquetStreamReader::Impl &impl)
{
  int ret = OB_SUCCESS;
  const int64_t col_cnt = impl.leaves_.count();
  if (impl.current_group_ < 0 || impl.current_group_ >= impl.row_group_rows_.count()) {
    ret = OB_ITER_END;
  } else {
    impl.group_row_ = 0;
    for (int64_t c = 0; OB_SUCC(ret) && c < col_cnt; ++c) {
      ParquetColumnCursor &cursor = impl.cursors_[c];
      cursor.reset();
      cursor.leaf_ = &impl.leaves_.at(c);
      cursor.chunk_ = &impl.chunks_.at(impl.current_group_ * col_cnt + c);
      if (cursor.chunk_->type_ != cursor.leaf_->type_
          || cursor.chunk_->num_values_ != impl.row_group_rows_.at(impl.current_group_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet row group column mismatch");
      } else {
        cursor.next_page_offset_ = cursor.chunk_->dictionary_page_offset_ >= 0
            ? cursor.chunk_->dictionary_page_offset_ : cursor.chunk_->data_page_offset_;
        if (cursor.next_page_offset_ < 4
            || cursor.next_page_offset_ >= impl.metadata_offset_) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet page offset out of range");
        }
      }
    }
  }
  return ret;
}

int parquet_read_page(ObParquetStreamReader::Impl &impl, ParquetColumnCursor &cursor)
{
  int ret = OB_SUCCESS;
  cursor.cells_.reset();
  cursor.page_pos_ = 0;
  cursor.page_allocator_.reset();
  const ParquetLeaf &leaf = *cursor.leaf_;
  const ParquetChunk &chunk = *cursor.chunk_;
  while (OB_SUCC(ret) && cursor.cells_.empty() && cursor.decoded_ < chunk.num_values_) {
    const int64_t page_offset = cursor.next_page_offset_;
    if (page_offset < 4 || page_offset >= impl.metadata_offset_) {
      ret = OB_INVALID_ARGUMENT;
      break;
    }
    const int64_t header_cap = MIN(PARQUET_PAGE_HEADER_READ_BYTES,
                                   impl.metadata_offset_ - page_offset);
    char *header = static_cast<char *>(cursor.page_allocator_.alloc(header_cap));
    if (OB_ISNULL(header)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (0 != fseek(impl.fd_, page_offset, SEEK_SET)
               || header_cap != static_cast<int64_t>(fread(header, 1, header_cap, impl.fd_))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet page header read failed");
    }
    TCompactReader r(header, header_cap);
    r.enter_struct();
    int16_t fid = 0;
    uint8_t type = T_STOP;
    int32_t page_type = -1;
    int64_t comp_size = 0;
    int64_t encoding = E_PLAIN;
    int64_t v1_def_encoding = 3;
    int64_t v1_nv = 0;
    int64_t v2_nv = 0;
    int64_t v2_nulls = 0;
    int64_t v2_defl = 0;
    int64_t v2_repl = 0;
    bool v2_compressed = false;
    int64_t dict_nv = 0;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(r.read_field_begin(fid, type))) {
      } else if (T_STOP == type) {
        r.leave_struct();
        break;
      } else if (1 == fid && (T_I32 == type || T_I64 == type)) {
        int64_t tmp = 0;
        if (OB_FAIL(r.read_i64(tmp))) {
        } else {
          page_type = static_cast<int32_t>(tmp);
        }
      } else if (3 == fid && (T_I32 == type || T_I64 == type)) {
        if (OB_FAIL(r.read_i64(comp_size))) {
        }
      } else if (5 == fid && T_STRUCT == type) {
        r.enter_struct();
        int16_t dfid = 0;
        uint8_t dtype = T_STOP;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(r.read_field_begin(dfid, dtype))) {
          } else if (T_STOP == dtype) {
            r.leave_struct();
            break;
          } else if (1 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(v1_nv))) {
            }
          } else if (2 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(encoding))) {
            }
          } else if (3 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(v1_def_encoding))) {
            }
          } else if (OB_FAIL(r.skip_value(dtype))) {
          }
        }
      } else if (7 == fid && T_STRUCT == type) {
        r.enter_struct();
        int16_t dfid = 0;
        uint8_t dtype = T_STOP;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(r.read_field_begin(dfid, dtype))) {
          } else if (T_STOP == dtype) {
            r.leave_struct();
            break;
          } else if (1 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(dict_nv))) {
            }
          } else if (OB_FAIL(r.skip_value(dtype))) {
          }
        }
      } else if (8 == fid && T_STRUCT == type) {
        r.enter_struct();
        int16_t dfid = 0;
        uint8_t dtype = T_STOP;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(r.read_field_begin(dfid, dtype))) {
          } else if (T_STOP == dtype) {
            r.leave_struct();
            break;
          } else if (1 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(v2_nv))) {
            }
          } else if (2 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(v2_nulls))) {
            }
          } else if (4 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(encoding))) {
            }
          } else if (5 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(v2_defl))) {
            }
          } else if (6 == dfid && (T_I32 == dtype || T_I64 == dtype)) {
            if (OB_FAIL(r.read_i64(v2_repl))) {
            }
          } else if (7 == dfid && (T_TRUE == dtype || T_FALSE == dtype)) {
            v2_compressed = (T_TRUE == dtype);
          } else if (OB_FAIL(r.skip_value(dtype))) {
          }
        }
      } else if (OB_FAIL(r.skip_value(type))) {
      }
    }
    const int64_t header_len = r.p_ - header;
    if (OB_SUCC(ret) && (comp_size <= 0 || comp_size > PARQUET_MAX_PAGE_BYTES
                         || header_len <= 0
                         || page_offset + header_len + comp_size > impl.metadata_offset_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet page size out of range");
    }
    char *payload = NULL;
    if (OB_SUCC(ret)) {
      payload = static_cast<char *>(cursor.page_allocator_.alloc(comp_size));
      if (OB_ISNULL(payload)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (0 != fseek(impl.fd_, page_offset + header_len, SEEK_SET)
                 || comp_size != static_cast<int64_t>(fread(payload, 1, comp_size, impl.fd_))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet page read truncated");
      }
    }
    const char *base = payload;
    int64_t sec_len = comp_size;
    char *dec_buf = NULL;
    if (OB_SUCC(ret) && 3 != page_type && 0 != chunk.codec_) {
      if (1 != chunk.codec_) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "parquet compression codec");
      } else if (OB_FAIL(snappy_decompress(cursor.page_allocator_, payload, comp_size,
                                           dec_buf, sec_len))) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet snappy decompress");
      } else {
        base = dec_buf;
      }
    }
    if (OB_SUCC(ret) && 2 == page_type) {
      const char *dp = base;
      const char *dict_end = base + sec_len;
      for (int64_t i = 0; OB_SUCC(ret) && i < dict_nv; ++i) {
        ObObjParam cell;
        if (OB_FAIL(plain_value_to_cell(cursor.dict_allocator_, leaf, dp,
                                        dict_end - dp, cell))) {
        } else if (OB_FAIL(cursor.dict_.push_back(cell))) {
        }
      }
      if (OB_SUCC(ret) && dp != dict_end) {
        ret = OB_INVALID_ARGUMENT;
      }
    } else if (OB_SUCC(ret) && (0 == page_type || 3 == page_type)) {
      const int64_t nv = (3 == page_type) ? v2_nv : v1_nv;
      int64_t vals_off = (3 == page_type) ? v2_defl + v2_repl : 0;
      if (nv < 0 || cursor.decoded_ + nv > chunk.num_values_
          || (3 == page_type && (v2_repl != 0 || vals_off < 0 || vals_off > comp_size))) {
        ret = OB_INVALID_ARGUMENT;
      } else if (3 == page_type && v2_compressed) {
        const char *vsec = payload + vals_off;
        const int64_t vsec_len = comp_size - vals_off;
        if (1 != chunk.codec_ || vsec_len <= 0) {
          ret = OB_NOT_SUPPORTED;
        } else if (OB_FAIL(snappy_decompress(cursor.page_allocator_, vsec, vsec_len,
                                             dec_buf, sec_len))) {
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet snappy decompress");
        }
      }
      ObSEArray<bool, 1024> defined;
      int64_t value_count = nv;
      if (OB_SUCC(ret) && 0 == leaf.repetition_type_) {
        for (int64_t i = 0; OB_SUCC(ret) && i < nv; ++i) {
          if (OB_FAIL(defined.push_back(true))) {
          }
        }
      } else if (OB_SUCC(ret) && 1 == leaf.repetition_type_) {
        const char *level_data = base;
        int64_t level_len = 0;
        if (3 == page_type) {
          level_len = v2_defl;
        } else {
          int32_t v1_level_len = 0;
          if (3 != v1_def_encoding || sec_len < 4) {
            ret = OB_NOT_SUPPORTED;
          } else {
            MEMCPY(&v1_level_len, base, 4);
            level_data = base + 4;
            level_len = v1_level_len;
            vals_off = 4 + v1_level_len;
          }
        }
        if (OB_FAIL(ret)) {
        } else if (level_len < 0 || level_len > sec_len - (level_data - base)
                   || vals_off < 0 || vals_off > sec_len) {
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(decode_definition_levels(level_data, level_len, nv,
                                                    defined, value_count))) {
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet definition levels");
        }
      } else if (OB_SUCC(ret)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "repeated parquet column");
      }
      if (OB_SUCC(ret) && (defined.count() != nv || value_count < 0
                           || (3 == page_type && v2_nulls != nv - value_count))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet null count mismatch");
      }
      const char *vstart = (3 == page_type && v2_compressed) ? dec_buf : base + vals_off;
      const int64_t vsec_len = (3 == page_type && v2_compressed) ? sec_len : sec_len - vals_off;
      ObSEArray<ObObjParam, 1024> page_values;
      const int64_t width = parquet_fixed_width(leaf);
      if (OB_FAIL(ret) || 0 == value_count) {
        if (OB_SUCC(ret) && 0 != vsec_len) {
          ret = OB_INVALID_ARGUMENT;
        }
      } else if (vsec_len < 0) {
        ret = OB_INVALID_ARGUMENT;
      } else if (P_BOOLEAN == leaf.type_ && E_PLAIN == encoding
                 && vsec_len == (value_count + 7) / 8) {
        for (int64_t i = 0; OB_SUCC(ret) && i < value_count; ++i) {
          ObObjParam cell;
          cell.set_bool(0 != (vstart[i / 8] >> (i % 8) & 1));
          if (OB_FAIL(page_values.push_back(cell))) {
          }
        }
      } else if (width > 0 && E_PLAIN == encoding && vsec_len == value_count * width) {
        const char *vp = vstart;
        for (int64_t i = 0; OB_SUCC(ret) && i < value_count; ++i) {
          ObObjParam cell;
          if (OB_FAIL(plain_value_to_cell(cursor.page_allocator_, leaf, vp,
                                          vsec_len - (vp - vstart), cell))) {
          } else if (OB_FAIL(page_values.push_back(cell))) {
          }
        }
      } else if (E_DELTA_BINARY_PACKED == encoding
                 && (P_INT32 == leaf.type_ || P_INT64 == leaf.type_)) {
        if (OB_FAIL(decode_delta_binary_packed(cursor.page_allocator_, leaf,
                                               vstart, vsec_len, value_count, page_values))) {
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet delta binary packed decode");
        }
      } else if (P_BYTE_ARRAY == leaf.type_ && E_DELTA_LENGTH_BYTE_ARRAY == encoding) {
        if (OB_FAIL(decode_delta_length_byte_array(cursor.page_allocator_, vstart, vsec_len,
                                                   value_count, page_values))) {
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet delta length byte array decode");
        }
      } else if (P_BYTE_ARRAY == leaf.type_ && E_DELTA_BYTE_ARRAY == encoding) {
        if (OB_FAIL(decode_delta_byte_array(cursor.page_allocator_, vstart, vsec_len,
                                            value_count, page_values))) {
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet delta byte array decode");
        }
      } else if ((E_RLE_DICT == encoding || 2 == encoding) && !cursor.dict_.empty()) {
        if (vsec_len <= 0) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const int32_t bit_width = static_cast<uint8_t>(vstart[0]);
          if (OB_FAIL(decode_rle_indices(vstart + 1, vsec_len - 1, bit_width,
                                         value_count, cursor.dict_, page_values))) {
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet dictionary page decode");
          }
        }
      } else if (P_BYTE_ARRAY == leaf.type_ && E_PLAIN == encoding) {
        const char *vp = vstart;
        const char *vend = vstart + vsec_len;
        for (int64_t i = 0; OB_SUCC(ret) && i < value_count; ++i) {
          ObObjParam cell;
          if (OB_FAIL(plain_value_to_cell(cursor.page_allocator_, leaf, vp,
                                          vend - vp, cell))) {
          } else if (OB_FAIL(page_values.push_back(cell))) {
          }
        }
        if (OB_SUCC(ret) && vp != vend) {
          ret = OB_INVALID_ARGUMENT;
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "parquet page value encoding");
        LOG_WARN("unsupported parquet value encoding", K(encoding), K(leaf.name_));
      }
      int64_t value_idx = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < nv; ++i) {
        if (defined.at(i)) {
          if (value_idx >= page_values.count()) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(cursor.cells_.push_back(page_values.at(value_idx++)))) {
          }
        } else {
          ObObjParam cell;
          cell.set_null();
          if (OB_FAIL(cursor.cells_.push_back(cell))) {
          }
        }
      }
      if (OB_SUCC(ret) && value_idx != page_values.count()) {
        ret = OB_INVALID_ARGUMENT;
      }
      if (OB_SUCC(ret)) {
        cursor.decoded_ += nv;
      }
    }
    if (OB_SUCC(ret)) {
      cursor.next_page_offset_ = page_offset + header_len + comp_size;
    }
  }
  if (OB_SUCC(ret) && cursor.cells_.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parquet column values truncated");
  }
  return ret;
}
} // end anonymous namespace

ObParquetStreamReader::ObParquetStreamReader() : impl_(NULL) {}
ObParquetStreamReader::~ObParquetStreamReader() { close(); }

void ObParquetStreamReader::close()
{
  if (OB_NOT_NULL(impl_)) {
    if (OB_NOT_NULL(impl_->fd_)) {
      fclose(impl_->fd_);
    }
    if (OB_NOT_NULL(impl_->metadata_)) {
      free(impl_->metadata_);
    }
    delete [] impl_->cursors_;
    delete impl_;
    impl_ = NULL;
  }
}

int ObParquetStreamReader::open(common::ObIAllocator &allocator, const char *path,
                                ObFileColumnSchema &schema,
                                const common::ObIArray<int64_t> *projected_columns)
{
  int ret = OB_SUCCESS;
  close();
  if (OB_ISNULL(path) || OB_ISNULL(impl_ = new (std::nothrow) Impl())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(impl_->fd_ = fopen(path, "rb"))) {
    ret = OB_FILE_NOT_OPENED;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "cannot open parquet file");
  } else if (OB_FAIL(parquet_read_footer(impl_->fd_, allocator, schema, impl_->metadata_,
                                         impl_->file_len_, impl_->metadata_offset_,
                                         impl_->leaves_, impl_->chunks_, impl_->row_group_rows_,
                                         impl_->file_rows_))) {
  } else if (OB_ISNULL(impl_->cursors_ = new (std::nothrow) ParquetColumnCursor[impl_->leaves_.count()])) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (int64_t c = 0; OB_SUCC(ret) && c < impl_->leaves_.count(); ++c) {
      if (OB_FAIL(impl_->column_needed_.push_back(NULL == projected_columns))) {
      }
    }
    if (OB_NOT_NULL(projected_columns)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < projected_columns->count(); ++i) {
        const int64_t c = projected_columns->at(i);
        if (c < 0 || c >= impl_->column_needed_.count()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          impl_->column_needed_.at(c) = true;
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    close();
  }
  return ret;
}

int64_t ObParquetStreamReader::row_count() const
{
  return OB_NOT_NULL(impl_) ? impl_->file_rows_ : 0;
}

int ObParquetStreamReader::next_row(common::ObIAllocator &allocator,
                                    common::ObIArray<ObObjParam> &row, bool &has_row)
{
  int ret = OB_SUCCESS;
  UNUSED(allocator);
  has_row = false;
  row.reset();
  if (OB_ISNULL(impl_) || OB_ISNULL(impl_->cursors_)) {
    ret = OB_NOT_INIT;
  }
  while (OB_SUCC(ret) && !has_row) {
    if (impl_->current_group_ >= impl_->row_group_rows_.count()) {
      break;
    } else if (0 == impl_->group_row_) {
      if (OB_FAIL(parquet_start_group(*impl_))) {
      } else if (0 == impl_->row_group_rows_.at(impl_->current_group_)) {
        ++impl_->current_group_;
        continue;
      }
    }
    for (int64_t c = 0; OB_SUCC(ret) && c < impl_->leaves_.count(); ++c) {
      ParquetColumnCursor &cursor = impl_->cursors_[c];
      if (!impl_->column_needed_.at(c)) {
        ObObjParam null_cell;
        null_cell.set_null();
        if (OB_FAIL(row.push_back(null_cell))) {
        }
      } else if (cursor.page_pos_ >= cursor.cells_.count()
                 && OB_FAIL(parquet_read_page(*impl_, cursor))) {
      } else if (OB_FAIL(row.push_back(cursor.cells_.at(cursor.page_pos_++)))) {
      }
    }
    if (OB_SUCC(ret)) {
      has_row = true;
      ++impl_->group_row_;
      if (impl_->group_row_ >= impl_->row_group_rows_.at(impl_->current_group_)) {
        ++impl_->current_group_;
        impl_->group_row_ = 0;
      }
    }
  }
  return ret;
}

int ObParquetReader::read_metadata(common::ObIAllocator &allocator, const char *path,
                                   ObFileColumnSchema &schema, int64_t &row_count)
{
  int ret = OB_SUCCESS;
  row_count = 0;
  ObParquetStreamReader reader;
  if (OB_FAIL(reader.open(allocator, path, schema))) {
  } else {
    row_count = reader.row_count();
  }
  reader.close();
  return ret;
}

int ObParquetReader::read_file(common::ObIAllocator &allocator, const char *path,
                               ObFileColumnSchema &schema,
                               common::ObIArray<common::ObObjParam> &flat_cells,
                               int64_t &row_count)
{
  int ret = OB_SUCCESS;
  row_count = 0;
  ObParquetStreamReader reader;
  if (OB_FAIL(reader.open(allocator, path, schema))) {
  } else {
    bool has_row = false;
    do {
      ObArray<ObObjParam> row;
      if (OB_FAIL(reader.next_row(allocator, row, has_row))) {
      } else if (has_row) {
        for (int64_t i = 0; OB_SUCC(ret) && i < row.count(); ++i) {
          if (OB_FAIL(flat_cells.push_back(row.at(i)))) {
          }
        }
        if (OB_SUCC(ret)) {
          ++row_count;
        }
      }
    } while (OB_SUCC(ret) && has_row);
  }
  reader.close();
  return ret;
}

} // namespace sql
} // namespace oceanbase