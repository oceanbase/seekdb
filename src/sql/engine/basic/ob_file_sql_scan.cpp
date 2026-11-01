/**
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

#include "sql/engine/basic/ob_file_sql_scan.h"

#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "common/json_type/ob_json_parse.h"
#include "common/json_type/ob_json_tree.h"
#include "common/timezone/ob_time_convert.h"
#include "lib/charset/ob_charset.h"
#include "lib/container/ob_se_array.h"
#include "lib/file/file_directory_utils.h"
#include "lib/oblog/ob_log.h"
#include "sql/engine/basic/ob_parquet_reader.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

static const int64_t FILE_SCAN_READ_BUF_SIZE = 256 * 1024;        // read chunk size
static const int64_t FILE_SCAN_MAX_LINE_LEN = 16 * 1024 * 1024;   // resource limit per logical line
static const int64_t FILE_SCAN_MAX_FIELDS = 4096;                 // resource limit per row

const char *read_file_format_to_str(ReadFileFormat format)
{
  const char *str = "UNKNOWN";
  switch (format) {
    case ReadFileFormat::CSV: {
      str = "CSV";
      break;
    }
    case ReadFileFormat::JSON: {
      str = "JSON";
      break;
    }
    case ReadFileFormat::NDJSON: {
      str = "NDJSON";
      break;
    }
    case ReadFileFormat::PARQUET: {
      str = "PARQUET";
      break;
    }
    case ReadFileFormat::GLOB_LIST: {
      str = "GLOB";
      break;
    }
    default:
      break;
  }
  return str;
}

int read_file_format_from_str(const ObString &name, ReadFileFormat &format)
{
  int ret = OB_SUCCESS;
  if (0 == name.case_compare("csv")) {
    format = ReadFileFormat::CSV;
  } else if (0 == name.case_compare("json")) {
    format = ReadFileFormat::JSON;
  } else if (0 == name.case_compare("ndjson") || 0 == name.case_compare("jsonl")) {
    format = ReadFileFormat::NDJSON;
  } else if (0 == name.case_compare("parquet")) {
    format = ReadFileFormat::PARQUET;
  } else if (0 == name.case_compare("glob")) {
    format = ReadFileFormat::GLOB_LIST;
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, static_cast<int32_t>(name.length()), name.ptr());
  }
  return ret;
}

bool ReadFileFingerprint::operator==(const ReadFileFingerprint &other) const
{
  return file_size_ == other.file_size_ && mtime_sec_ == other.mtime_sec_
      && mtime_nsec_ == other.mtime_nsec_;
}

int64_t ReadFileFingerprint::to_uint64_hash() const
{
  int64_t hash = file_size_;
  hash = hash * 1000003 + mtime_sec_;
  hash = hash * 1000003 + mtime_nsec_;
  return hash;
}

#ifdef _WIN32
static int stat_file(const char *path, struct ::_stat64 &st)
{
  return ::_stat64(path, &st);
}
#else
static int stat_file(const char *path, struct stat &st)
{
  return ::stat(path, &st);
}
#endif

int get_file_fingerprint(const ObString &path, ReadFileFingerprint &fp)
{
  int ret = OB_SUCCESS;
  fp.reset();
  if (OB_UNLIKELY(path.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid path", K(ret));
  } else {
    char buf[OB_MAX_FILE_NAME_LENGTH];
    const int64_t len = std::min(static_cast<int64_t>(path.length()), static_cast<int64_t>(sizeof(buf) - 1));
    MEMCPY(buf, path.ptr(), len);
    buf[len] = '\0';
#ifdef _WIN32
    struct ::_stat64 st;
#else
    struct stat st;
#endif
    if (0 != stat_file(buf, st)) {
      ret = OB_FILE_NOT_EXIST;
      LOG_USER_ERROR(OB_FILE_NOT_EXIST, static_cast<int32_t>(len), buf);
    } else {
      fp.file_size_ = static_cast<int64_t>(st.st_size);
      fp.mtime_sec_ = static_cast<int64_t>(st.st_mtime);
#ifdef _WIN32
      fp.mtime_nsec_ = 0;
#else
      fp.mtime_nsec_ = static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
    }
  }
  return ret;
}

bool has_wildcard(const ObString &path)
{
  for (int64_t i = 0; i < path.length(); ++i) {
    const char c = path.ptr()[i];
    if ('*' == c || '?' == c || '[' == c) {
      return true;
    }
  }
  return false;
}

// wildcard match: supports '*' (any sequence) and '?' (any single char)
static bool wild_match(const char *pat, int64_t plen, const char *str, int64_t slen)
{
  int64_t p = 0, s = 0, star = -1, mark = 0;
  while (s < slen) {
    if (p < plen && ('?' == pat[p] || pat[p] == str[s])) {
      ++p;
      ++s;
    } else if (p < plen && '*' == pat[p]) {
      star = p++;
      mark = s;
    } else if (star >= 0) {
      p = star + 1;
      s = ++mark;
    } else {
      return false;
    }
  }
  while (p < plen && '*' == pat[p]) {
    ++p;
  }
  return p == plen;
}

int discover_files(ObIAllocator &alloc, const ObString &pattern,
                   ObIArray<ObString> &out_paths)
{
  int ret = OB_SUCCESS;
  out_paths.reset();
  if (OB_UNLIKELY(pattern.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty pattern", K(ret));
  } else if (!has_wildcard(pattern)) {
    // plain path: keep as is if it exists
    char buf[OB_MAX_FILE_NAME_LENGTH];
    const int64_t len = std::min(static_cast<int64_t>(pattern.length()), static_cast<int64_t>(sizeof(buf) - 1));
    MEMCPY(buf, pattern.ptr(), len);
    buf[len] = '\0';
    bool exists = false;
    if (OB_FAIL(FileDirectoryUtils::is_exists(buf, exists))) {
      LOG_WARN("check file exists failed", K(ret), K(pattern));
    } else if (!exists) {
      ret = OB_FILE_NOT_EXIST;
      LOG_USER_ERROR(OB_FILE_NOT_EXIST, static_cast<int32_t>(len), buf);
    } else {
      ObString file_path;
      if (OB_FAIL(ob_write_string(alloc, pattern, file_path, true))) {
        LOG_WARN("copy path failed", K(ret));
      } else if (OB_FAIL(out_paths.push_back(file_path))) {
        LOG_WARN("push back path failed", K(ret));
      }
    }
  } else {
    // split into directory part and wildcard part
    int64_t dir_end = -1;
    for (int64_t i = 0; i < pattern.length(); ++i) {
      const char c = pattern.ptr()[i];
      if ('/' == c || '\\' == c) {
        dir_end = i;
      }
    }
    ObString dir_part;
    ObString name_part;
    if (dir_end < 0) {
      name_part = pattern;
      dir_part = ObString::make_string(".");
    } else {
      dir_part.assign_ptr(pattern.ptr(), static_cast<int32_t>(dir_end));
      name_part.assign_ptr(pattern.ptr() + dir_end + 1,
                           static_cast<int32_t>(pattern.length() - dir_end - 1));
    }
    if (name_part.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("pattern must contain a file name part", K(ret), K(pattern));
    } else if (has_wildcard(dir_part)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, static_cast<int32_t>(strlen("wildcard in directory part")),
                     "wildcard in directory part");
    } else {
      char dir_buf[OB_MAX_FILE_NAME_LENGTH];
      const int64_t dlen = std::min(static_cast<int64_t>(dir_part.length()), static_cast<int64_t>(sizeof(dir_buf) - 1));
      MEMCPY(dir_buf, dir_part.ptr(), dlen);
      dir_buf[dlen] = '\0';
#ifdef _WIN32
      char search_buf[OB_MAX_FILE_NAME_LENGTH];
      if (OB_UNLIKELY(dlen + name_part.length() + 3 > OB_MAX_FILE_NAME_LENGTH)) {
        ret = OB_BUF_NOT_ENOUGH;
        LOG_WARN("path too long", K(ret));
      } else {
        snprintf(search_buf, sizeof(search_buf), "%s\\%.*s", dir_buf,
                 static_cast<int>(name_part.length()), name_part.ptr());
        WIN32_FIND_DATAA fd;
        HANDLE hfind = FindFirstFileA(search_buf, &fd);
        if (INVALID_HANDLE_VALUE == hfind) {
          ret = OB_FILE_NOT_EXIST;
          LOG_USER_ERROR(OB_FILE_NOT_EXIST, static_cast<int32_t>(strlen(search_buf)), search_buf);
        } else {
          do {
            if (0 != (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
              continue;
            }
            char full_buf[OB_MAX_FILE_NAME_LENGTH];
            const int64_t flen = snprintf(full_buf, sizeof(full_buf), "%s\\%s", dir_buf, fd.cFileName);
            ObString file_path;
            if (OB_FAIL(ob_write_string(alloc, ObString(static_cast<int32_t>(flen), full_buf),
                                        file_path, true))) {
              LOG_WARN("copy path failed", K(ret));
            } else if (OB_FAIL(out_paths.push_back(file_path))) {
              LOG_WARN("push back path failed", K(ret));
            }
          } while (OB_SUCC(ret) && FindNextFileA(hfind, &fd));
          FindClose(hfind);
        }
      }
#else
      DIR *dir = opendir(dir_buf);
      if (OB_ISNULL(dir)) {
        ret = OB_FILE_NOT_EXIST;
        LOG_USER_ERROR(OB_FILE_NOT_EXIST, static_cast<int32_t>(dlen), dir_buf);
      } else {
        struct dirent *entry = NULL;
        while (OB_SUCC(ret) && NULL != (entry = readdir(dir))) {
          if (DT_DIR == entry->d_type) {
            continue;
          }
          const int64_t nlen = strlen(entry->d_name);
          if (!wild_match(name_part.ptr(), name_part.length(), entry->d_name, nlen)) {
            continue;
          }
          char full_buf[OB_MAX_FILE_NAME_LENGTH];
          const int64_t flen = snprintf(full_buf, sizeof(full_buf), "%s/%s", dir_buf, entry->d_name);
          ObString file_path;
          if (OB_FAIL(ob_write_string(alloc, ObString(static_cast<int32_t>(flen), full_buf),
                                      file_path, true))) {
            LOG_WARN("copy path failed", K(ret));
          } else if (OB_FAIL(out_paths.push_back(file_path))) {
            LOG_WARN("push back path failed", K(ret));
          }
        }
        closedir(dir);
      }
#endif
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Line feeder: streaming reader that yields complete logical lines
// (quoted newlines are handled) from a list of files.
// ---------------------------------------------------------------------------
class ObFileLineFeeder
{
public:
  ObFileLineFeeder(ObIAllocator &alloc, const ObIArray<ObString> &paths)
    : alloc_(alloc), paths_(paths), cur_file_(-1), file_reader_(), file_size_(0),
      file_offset_(0), eof_file_(false), eof_all_(false),
      pend_buf_(NULL), pend_cap_(0), pend_len_(0), scan_pos_(0),
      line_no_(0), total_lines_(0)
  {}
  ~ObFileLineFeeder() { close(); }
  int open();
  void close();
  int rescan();
  // Returns the next complete logical line. Returns OB_ITER_END when all files
  // are exhausted.
  int next_line(ObString &line, int64_t &line_no);
  int64_t get_total_lines() const { return total_lines_; }
private:
  int open_cur_file();
  int fill_buffer();  // append next chunk into pend_buf_ or move to next file
  int ensure_pend_capacity(int64_t need);
  int skip_utf8_bom();

  ObIAllocator &alloc_;
  const ObIArray<ObString> &paths_;
  int64_t cur_file_;
  common::ObFileReader file_reader_;
  int64_t file_size_;
  int64_t file_offset_;
  bool eof_file_;
  bool eof_all_;
  char *pend_buf_;
  int64_t pend_cap_;
  int64_t pend_len_;
  int64_t scan_pos_;
  int64_t line_no_;
  int64_t total_lines_;
};

int ObFileLineFeeder::open()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(paths_.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("no files to scan", K(ret));
  } else if (OB_FAIL(rescan())) {
    LOG_WARN("rescan failed", K(ret));
  }
  return ret;
}

void ObFileLineFeeder::close()
{
  file_reader_.close();
  cur_file_ = -1;
  eof_all_ = true;
  pend_len_ = 0;
  scan_pos_ = 0;
}

int ObFileLineFeeder::rescan()
{
  int ret = OB_SUCCESS;
  file_reader_.close();
  cur_file_ = -1;
  pend_len_ = 0;
  scan_pos_ = 0;
  line_no_ = 0;
  eof_all_ = false;
  eof_file_ = false;
  if (OB_FAIL(open_cur_file())) {
    LOG_WARN("open first file failed", K(ret));
  }
  return ret;
}

int ObFileLineFeeder::open_cur_file()
{
  int ret = OB_SUCCESS;
  bool opened = false;
  while (!opened && cur_file_ + 1 < paths_.count()) {
    ++cur_file_;
    file_offset_ = 0;
    eof_file_ = false;
    line_no_ = 0;
    pend_len_ = 0;   // never join lines across files
    scan_pos_ = 0;
    file_reader_.close();
    if (OB_FAIL(file_reader_.open(paths_.at(cur_file_), false))) {
      LOG_USER_ERROR(OB_FILE_NOT_EXIST,
                     static_cast<int32_t>(paths_.at(cur_file_).length()),
                     paths_.at(cur_file_).ptr());
      LOG_WARN("open file failed", K(ret), K(paths_.at(cur_file_)));
    } else if (OB_FAIL(FileDirectoryUtils::get_file_size(
                   paths_.at(cur_file_).ptr(), file_size_))) {
      LOG_WARN("get file size failed", K(ret), K(paths_.at(cur_file_)));
    } else if (OB_FAIL(skip_utf8_bom())) {
      LOG_WARN("skip bom failed", K(ret));
    } else {
      opened = true;
    }
  }
  if (!opened) {
    eof_all_ = true;
  }
  return ret;
}

int ObFileLineFeeder::skip_utf8_bom()
{
  int ret = OB_SUCCESS;
  char bom[3];
  if (file_size_ - file_offset_ >= 3) {
    int64_t read_size = 0;
    if (OB_FAIL(file_reader_.pread(bom, 3, file_offset_, read_size))) {
      LOG_WARN("read bom failed", K(ret));
    } else if (read_size == 3 && 0 == MEMCMP(bom, "\xEF\xBB\xBF", 3)) {
      file_offset_ += 3;
    }
  }
  return ret;
}

int ObFileLineFeeder::fill_buffer()
{
  int ret = OB_SUCCESS;
  if (eof_file_) {
    // move to the next file, dropping any partial line of the previous file
    if (OB_FAIL(open_cur_file())) {
      LOG_WARN("open next file failed", K(ret));
    }
  } else if (pend_cap_ - pend_len_ <= 0) {
    ret = OB_SIZE_OVERFLOW;  // logical line longer than the limit
    LOG_USER_ERROR(OB_SIZE_OVERFLOW, static_cast<int32_t>(strlen("single logical line")),
                   "single logical line");
    LOG_WARN("logical line exceeds limit", K(ret), K(pend_len_));
  } else {
    int64_t read_size = 0;
    const int64_t to_read = std::min(pend_cap_ - pend_len_, FILE_SCAN_READ_BUF_SIZE);
    if (OB_FAIL(file_reader_.pread(pend_buf_ + pend_len_, to_read, file_offset_, read_size))) {
      LOG_WARN("pread failed", K(ret));
    } else if (read_size <= 0) {
      eof_file_ = true;
    } else {
      pend_len_ += read_size;
      file_offset_ += read_size;
    }
  }
  return ret;
}

int ObFileLineFeeder::ensure_pend_capacity(int64_t need)
{
  int ret = OB_SUCCESS;
  if (need > pend_cap_) {
    int64_t new_cap = (pend_cap_ > 0 ? pend_cap_ : FILE_SCAN_READ_BUF_SIZE);
    while (new_cap < need) {
      new_cap *= 2;
    }
    if (OB_UNLIKELY(new_cap > FILE_SCAN_MAX_LINE_LEN * 2)) {
      ret = OB_SIZE_OVERFLOW;
      LOG_USER_ERROR(OB_SIZE_OVERFLOW, static_cast<int32_t>(strlen("line buffer size")),
                     "line buffer size");
      LOG_WARN("line buffer too large", K(ret), K(new_cap));
    } else {
      char *new_buf = static_cast<char *>(alloc_.alloc(new_cap));
      if (OB_ISNULL(new_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc line buffer failed", K(ret), K(new_cap));
      } else {
        if (pend_len_ > 0) {
          MEMCPY(new_buf, pend_buf_, pend_len_);
        }
        pend_buf_ = new_buf;
        pend_cap_ = new_cap;
      }
    }
  }
  return ret;
}

int ObFileLineFeeder::next_line(ObString &line, int64_t &line_no)
{
  int ret = OB_SUCCESS;
  line.reset();
  while (OB_SUCC(ret)) {
    // ensure buffer exists
    if (OB_FAIL(ensure_pend_capacity(FILE_SCAN_READ_BUF_SIZE))) {
      LOG_WARN("ensure capacity failed", K(ret));
      break;
    }
    // find the next newline (logical line terminator) in [scan_pos_, pend_len_)
    int64_t nl = scan_pos_;
    while (nl < pend_len_ && '\n' != pend_buf_[nl]) {
      ++nl;
    }
    const bool found = (nl < pend_len_);
    if (!found && !eof_file_) {
      // need more data: compact and refill
      if (scan_pos_ > 0) {
        MEMCPY(pend_buf_, pend_buf_ + scan_pos_, pend_len_ - scan_pos_);
        pend_len_ -= scan_pos_;
        scan_pos_ = 0;
      }
      if (OB_FAIL(fill_buffer())) {
        LOG_WARN("fill buffer failed", K(ret));
      }
      continue;
    }
    if (!found) {
      // all files exhausted: emit the trailing line without newline (if any)
      if (pend_len_ > scan_pos_) {
        const char *line_ptr = pend_buf_ + scan_pos_;
        int64_t line_len = pend_len_ - scan_pos_;
        scan_pos_ = pend_len_;
        if (line_len > 0 && '\r' == line_ptr[line_len - 1]) {
          --line_len;
        }
        ++line_no_;
        if (line_len > 0) {
          line.assign_ptr(line_ptr, static_cast<int32_t>(line_len));
          ++total_lines_;
          break;
        }
        continue;  // trailing blank line: keep scanning for next file's data
      } else if (eof_all_) {
        ret = OB_ITER_END;
        break;
      } else {
        // more files to read
        if (OB_FAIL(fill_buffer())) {
          LOG_WARN("fill buffer failed", K(ret));
        }
        continue;
      }
    }
    // a complete line is available
    const char *line_ptr = pend_buf_ + scan_pos_;
    int64_t line_len = nl - scan_pos_;
    scan_pos_ = nl + 1;
    if (line_len > 0 && '\r' == line_ptr[line_len - 1]) {
      --line_len;
    }
    ++line_no_;
    if (line_len == 0) {
      continue;  // blank line
    }
    line.assign_ptr(line_ptr, static_cast<int32_t>(line_len));
    ++total_lines_;
    // compact the buffer if a large portion has been consumed
    if (scan_pos_ >= FILE_SCAN_READ_BUF_SIZE) {
      MEMCPY(pend_buf_, pend_buf_ + scan_pos_, pend_len_ - scan_pos_);
      pend_len_ -= scan_pos_;
      scan_pos_ = 0;
    }
    break;
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Cell classification / type inference
// ---------------------------------------------------------------------------
enum CellKind
{
  CK_NULL = 0,
  CK_BOOL,
  CK_INT,
  CK_DOUBLE,
  CK_DATE,
  CK_DATETIME,
  CK_VARCHAR,
  CK_MAX
};

static const char *cell_kind_str(CellKind k)
{
  static const char *STR[CK_MAX] = {"NULL", "BOOLEAN", "BIGINT", "DOUBLE", "DATE",
                                    "DATETIME", "VARCHAR"};
  return STR[k];
}

static CellKind merge_kind(CellKind a, CellKind b)
{
  if (CK_NULL == a) { return b; }
  if (CK_NULL == b) { return a; }
  if (a == b) { return a; }
  // numeric widening: BOOL < INT < DOUBLE
  if ((CK_BOOL == a || CK_INT == a || CK_DOUBLE == a)
      && (CK_BOOL == b || CK_INT == b || CK_DOUBLE == b)) {
    return std::max(a, b);
  }
  // temporal widening: DATE + DATETIME = DATETIME
  if ((CK_DATE == a || CK_DATETIME == a) && (CK_DATE == b || CK_DATETIME == b)) {
    return CK_DATETIME;
  }
  return CK_VARCHAR;
}

static bool parse_bool_text(const ObString &text, bool &val)
{
  if (0 == text.case_compare("true")) {
    val = true;
  } else if (0 == text.case_compare("false")) {
    val = false;
  } else {
    return false;
  }
  return true;
}

static bool parse_int_text(const ObString &text, int64_t &val)
{
  bool ok = false;
  if (!text.empty()) {
    int err = 0;
    char *end = NULL;
    val = ObCharset::strntoll(text.ptr(), text.length(), 10, &end, &err);
    ok = (0 == err && NULL != end && end == text.ptr() + text.length());
  }
  return ok;
}

static bool parse_double_text(const ObString &text, double &val)
{
  bool ok = false;
  if (!text.empty()) {
    int err = 0;
    char *end = NULL;
    val = ObCharset::strntodv2(text.ptr(), text.length(), &end, &err);
    ok = (0 == err && NULL != end && end == text.ptr() + text.length());
  }
  return ok;
}

// classify one text cell
static CellKind classify_text(const ObString &text, int64_t &int_val, double &dbl_val,
                              int32_t &date_val, int64_t &dt_val, int64_t &len)
{
  CellKind kind = CK_VARCHAR;
  len = text.length();
  bool bool_val = false;
  if (parse_bool_text(text, bool_val)) {
    int_val = bool_val ? 1 : 0;
    kind = CK_BOOL;
  } else if (parse_int_text(text, int_val)) {
    kind = CK_INT;
  } else if (parse_double_text(text, dbl_val)) {
    kind = CK_DOUBLE;
  } else {
    // try date / datetime (strict parse)
    ObMySQLDate mdate;
    if (OB_SUCCESS == ObTimeConverter::str_to_mdate(text, mdate)) {
      kind = CK_DATE;
      date_val = mdate.date_;
    } else {
      ObTimeConvertCtx cvrt_ctx(NULL, false);
      ObMySQLDateTime mdt;
      int16_t scale = 0;
      if (OB_SUCCESS == ObTimeConverter::str_to_mdatetime(text, cvrt_ctx, mdt, &scale)) {
        kind = CK_DATETIME;
        dt_val = mdt.datetime_;
      }
    }
  }
  return kind;
}

// ---------------------------------------------------------------------------
// CSV field splitting rules shared by the reader and the inference pass
// ---------------------------------------------------------------------------
struct CsvField
{
  const char *ptr_;
  int64_t len_;
  bool is_null_;
  bool quoted_;
  CsvField() : ptr_(NULL), len_(0), is_null_(false), quoted_(false) {}
};

// split one logical line into fields according to options
// when `arena` is not NULL, quoted fields with doubled quotes / escape
// sequences are unescaped into buffers allocated from it
static int split_csv_line(const ObString &line, const ReadFileOptions &options,
                          ObIAllocator *arena, ObIArray<CsvField> &fields)
{
  int ret = OB_SUCCESS;
  fields.reset();
  const char delim = options.field_delimiter_.length() > 0
      ? options.field_delimiter_.ptr()[0] : ',';
  const bool has_quote = options.quote_char_ != INT64_MAX;
  const char quote = has_quote ? static_cast<char>(options.quote_char_) : '\0';
  const bool has_escape = options.escape_char_ != INT64_MAX;
  const char escape = has_escape ? static_cast<char>(options.escape_char_) : '\0';

  const char *p = line.ptr();
  const int64_t len = line.length();
  int64_t i = 0;
  while (OB_SUCC(ret) && i <= len) {
    if (OB_UNLIKELY(fields.count() >= FILE_SCAN_MAX_FIELDS)) {
      ret = OB_SIZE_OVERFLOW;
      LOG_USER_ERROR(OB_SIZE_OVERFLOW, static_cast<int32_t>(strlen("column count in one row")),
                     "column count in one row");
      LOG_WARN("too many fields in one row", K(ret), K(fields.count()));
      break;
    }
    CsvField f;
    int64_t start = i;
    bool in_quote = false;
    while (i <= len) {
      if (i == len) {
        break;
      }
      const char c = p[i];
      if (in_quote) {
        if (has_escape && escape == c && i + 1 <= len) {
          i += 2;
        } else if (quote == c) {
          if (i + 1 < len && quote == p[i + 1]) {
            i += 2;  // doubled quote
          } else {
            in_quote = false;
            ++i;
          }
        } else {
          ++i;
        }
      } else if (has_quote && quote == c) {
        in_quote = true;
        f.quoted_ = true;
        ++i;
      } else if (delim == c) {
        ++i;
        break;
      } else {
        ++i;
      }
    }
    f.ptr_ = p + start;
    f.len_ = i - start;
    if (f.quoted_ && f.len_ >= 2 && quote == f.ptr_[0] && quote == f.ptr_[f.len_ - 1]) {
      // strip the outer quotes
      const char *inner = f.ptr_ + 1;
      const int64_t inner_len = f.len_ - 2;
      f.ptr_ = inner;
      f.len_ = inner_len;
      if (NULL != arena) {
        bool need_rewrite = false;
        for (int64_t k = 0; k < inner_len && !need_rewrite; ++k) {
          if ((quote == inner[k] && k + 1 < inner_len && quote == inner[k + 1])
              || (has_escape && escape == inner[k])) {
            need_rewrite = true;
          }
        }
        if (need_rewrite) {
          char *out = static_cast<char *>(arena->alloc(inner_len + 1));
          if (OB_ISNULL(out)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("alloc field buffer failed", K(ret));
          } else {
            int64_t o = 0;
            for (int64_t k = 0; k < inner_len; ++k) {
              if (quote == inner[k] && k + 1 < inner_len && quote == inner[k + 1]) {
                out[o++] = quote;
                ++k;
              } else if (has_escape && escape == inner[k] && k + 1 < inner_len) {
                ++k;  // drop the escape char, keep the raw next char
                out[o++] = inner[k];
              } else {
                out[o++] = inner[k];
              }
            }
            f.ptr_ = out;
            f.len_ = o;
          }
        }
      }
    } else if (has_escape && NULL != arena) {
      // unquoted field: unescape if it contains escape sequences
      bool has_escape_seq = false;
      for (int64_t k = 0; k < f.len_ && !has_escape_seq; ++k) {
        if (escape == f.ptr_[k] && k + 1 < f.len_) {
          has_escape_seq = true;
        }
      }
      if (has_escape_seq) {
        char *out = static_cast<char *>(arena->alloc(f.len_ + 1));
        if (OB_ISNULL(out)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc field buffer failed", K(ret));
        } else {
          int64_t o = 0;
          for (int64_t k = 0; k < f.len_; ++k) {
            if (escape == f.ptr_[k] && k + 1 < f.len_) {
              ++k;
            }
            out[o++] = f.ptr_[k];
          }
          f.ptr_ = out;
          f.len_ = o;
        }
      }
    }
    // NULL determination
    if (!f.quoted_ && f.len_ == 0 && options.empty_field_as_null_) {
      f.is_null_ = true;
    } else if (options.null_text_.length() > 0 && !f.quoted_
               && f.len_ == options.null_text_.length()
               && 0 == MEMCMP(f.ptr_, options.null_text_.ptr(), f.len_)) {
      f.is_null_ = true;
    }
    if (OB_FAIL(fields.push_back(f))) {
      LOG_WARN("push field failed", K(ret));
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// CSV row reader
// ---------------------------------------------------------------------------
class ObCsvFileRowReader : public ObIFileRowReader
{
public:
  ObCsvFileRowReader(ObIAllocator &alloc, const ObIArray<ObString> &paths,
                     const ReadFileOptions &options)
    : ObIFileRowReader(alloc), feeder_(NULL), options_(options), expect_cols_(0), bad_rows_(0)
  {
    for (int64_t i = 0; i < paths.count(); ++i) {
      ObString s;
      if (OB_SUCCESS == ob_write_string(struct_arena_, paths.at(i), s, true)) {
        own_paths_.push_back(s);
      }
    }
  }
  virtual ~ObCsvFileRowReader() { close(); }
  virtual int open() override;
  virtual int rescan() override;
  virtual int close() override;
  virtual int read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows) override;
private:
  int convert_field(const CsvField &f, const ReadFileColumn &col, int64_t line_no,
                    int64_t col_idx, ObObj &out);

  ObArray<ObString> own_paths_;
  ObFileLineFeeder *feeder_;
  ReadFileOptions options_;
  ObSEArray<CsvField, 128> fields_;
  int64_t expect_cols_;
  int64_t bad_rows_;
};

int ObCsvFileRowReader::open()
{
  int ret = OB_SUCCESS;
  expect_cols_ = col_count_;
  if (OB_ISNULL(feeder_)) {
    feeder_ = new (batch_arena_.alloc(sizeof(ObFileLineFeeder)))
        ObFileLineFeeder(batch_arena_, own_paths_);
    if (OB_ISNULL(feeder_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc feeder failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(feeder_->open())) {
    LOG_WARN("open line feeder failed", K(ret));
  }
  return ret;
}

int ObCsvFileRowReader::rescan()
{
  int ret = OB_SUCCESS;
  total_rows_ = 0;
  bad_rows_ = 0;
  batch_arena_.reset();
  feeder_ = NULL;  // rebuilt on next open()
  if (OB_FAIL(open())) {
    LOG_WARN("reopen feeder failed", K(ret));
  }
  return ret;
}

int ObCsvFileRowReader::close()
{
  if (NULL != feeder_) {
    feeder_->~ObFileLineFeeder();
    feeder_ = NULL;
  }
  batch_arena_.reset();
  return OB_SUCCESS;
}

int ObCsvFileRowReader::convert_field(const CsvField &f, const ReadFileColumn &col,
                                      int64_t line_no, int64_t col_idx, ObObj &out)
{
  int ret = OB_SUCCESS;
  out.reset();
  if (f.is_null_ || common::ObNullType == col.type_) {
    out.set_null();
  } else {
    const ObString text(static_cast<int32_t>(f.len_), f.ptr_);
    switch (col.type_) {
      case common::ObVarcharType: {
        char *buf = static_cast<char *>(batch_arena_.alloc(f.len_));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc varchar failed", K(ret));
        } else {
          MEMCPY(buf, f.ptr_, f.len_);
          out.set_varchar(ObString(static_cast<int32_t>(f.len_), buf));
          out.set_collation_type(common::ObCharset::get_default_collation(
              common::ObCharset::get_default_charset()));
        }
        break;
      }
      case common::ObIntType: {
        int64_t val = 0;
        if (!parse_int_text(text, val)) {
          ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          LOG_USER_ERROR(OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, static_cast<int32_t>(strlen("BIGINT")), "BIGINT",
                         static_cast<int32_t>(text.length()), text.ptr(), line_no);
        } else {
          out.set_int(val);
        }
        break;
      }
      case common::ObDoubleType: {
        double val = 0;
        if (!parse_double_text(text, val)) {
          ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          LOG_USER_ERROR(OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, static_cast<int32_t>(strlen("DOUBLE")), "DOUBLE",
                         static_cast<int32_t>(text.length()), text.ptr(), line_no);
        } else {
          out.set_double(val);
        }
        break;
      }
      case common::ObTinyIntType: {
        bool val = false;
        if (parse_bool_text(text, val)) {
          out.set_tinyint(val ? 1 : 0);
        } else {
          ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          LOG_USER_ERROR(OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, static_cast<int32_t>(strlen("BOOLEAN")), "BOOLEAN",
                         static_cast<int32_t>(text.length()), text.ptr(), line_no);
        }
        break;
      }
      case common::ObMySQLDateType: {
        ObMySQLDate mdate;
        if (OB_SUCCESS != ObTimeConverter::str_to_mdate(text, mdate)) {
          ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          LOG_USER_ERROR(OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, static_cast<int32_t>(strlen("DATE")), "DATE",
                         static_cast<int32_t>(text.length()), text.ptr(), line_no);
        } else {
          out.set_mysql_date(mdate);
        }
        break;
      }
      case common::ObMySQLDateTimeType: {
        ObTimeConvertCtx cvrt_ctx(tz_info_, false);
        ObMySQLDateTime mdt;
        int16_t scale = 0;
        if (OB_SUCCESS != ObTimeConverter::str_to_mdatetime(text, cvrt_ctx, mdt, &scale)) {
          ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          LOG_USER_ERROR(OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, static_cast<int32_t>(strlen("DATETIME")), "DATETIME",
                         static_cast<int32_t>(text.length()), text.ptr(), line_no);
        } else {
          out.set_mysql_datetime(mdt);
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected column type", K(ret), K(col));
        break;
      }
    }
  }
  return ret;
}

int ObCsvFileRowReader::read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows)
{
  int ret = OB_SUCCESS;
  n_rows = 0;
  while (OB_SUCC(ret) && n_rows < max_rows) {
    ObString line;
    int64_t line_no = 0;
    if (OB_FAIL(feeder_->next_line(line, line_no))) {
      if (OB_ITER_END == ret) {
        break;
      }
      LOG_WARN("read line failed", K(ret));
      break;
    }
    if (OB_FAIL(split_csv_line(line, options_, &batch_arena_, fields_))) {
      if (options_.ignore_errors_) {
        ++bad_rows_;
        ret = OB_SUCCESS;
        continue;
      }
      break;
    }
    // field count check: rows with more fields than the schema are bad rows
    if (fields_.count() > expect_cols_) {
      // tolerate one extra empty string produced by a trailing delimiter
      bool only_trailing_empty = (fields_.count() == expect_cols_ + 1)
          && fields_.at(fields_.count() - 1).len_ == 0;
      if (only_trailing_empty) {
        while (fields_.count() > expect_cols_) {
          fields_.pop_back();
        }
      } else if (options_.ignore_errors_) {
        ++bad_rows_;
        ret = OB_SUCCESS;
        continue;
      } else {
        ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
        LOG_USER_ERROR(OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, static_cast<int32_t>(strlen("CSV")), "CSV",
                       static_cast<int32_t>(strlen("column count")), "column count", line_no);
        break;
      }
    }
    ObObj *row = cells + n_rows * col_count_;
    bool row_failed = false;
    for (int64_t c = 0; OB_SUCC(ret) && c < expect_cols_; ++c) {
      const ReadFileColumn &col = cols_.at(c);
      const CsvField &f = c < fields_.count() ? fields_.at(c) : CsvField();
      if (OB_FAIL(convert_field(f, col, line_no, c, row[c]))) {
        row_failed = true;
      }
    }
    if (OB_FAIL(ret)) {
      if (options_.ignore_errors_ && row_failed) {
        ret = OB_SUCCESS;
        ++bad_rows_;
        continue;
      }
      break;
    }
    ++n_rows;
    ++total_rows_;
  }
  return ret;
}

// ---------------------------------------------------------------------------
// NDJSON row reader
// ---------------------------------------------------------------------------
class ObNdjsonFileRowReader : public ObIFileRowReader
{
public:
  ObNdjsonFileRowReader(ObIAllocator &alloc, const ObIArray<ObString> &paths,
                        const ReadFileOptions &options)
    : ObIFileRowReader(alloc), feeder_(NULL), options_(options), bad_rows_(0)
  {
    for (int64_t i = 0; i < paths.count(); ++i) {
      ObString s;
      if (OB_SUCCESS == ob_write_string(struct_arena_, paths.at(i), s, true)) {
        own_paths_.push_back(s);
      }
    }
  }
  virtual ~ObNdjsonFileRowReader() { close(); }
  virtual int open() override;
  virtual int rescan() override;
  virtual int close() override;
  virtual int read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows) override;
private:
  int convert_json_value(const ObIJsonBase *value, const ReadFileColumn &col,
                         int64_t line_no, int64_t col_idx, ObObj &out);

  ObArray<ObString> own_paths_;
  ObFileLineFeeder *feeder_;
  ReadFileOptions options_;
  int64_t bad_rows_;
};

int ObNdjsonFileRowReader::open()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(feeder_)) {
    feeder_ = new (batch_arena_.alloc(sizeof(ObFileLineFeeder)))
        ObFileLineFeeder(batch_arena_, own_paths_);
    if (OB_ISNULL(feeder_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc feeder failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(feeder_->open())) {
    LOG_WARN("open line feeder failed", K(ret));
  }
  return ret;
}

int ObNdjsonFileRowReader::rescan()
{
  int ret = OB_SUCCESS;
  total_rows_ = 0;
  bad_rows_ = 0;
  batch_arena_.reset();
  feeder_ = NULL;
  if (OB_FAIL(open())) {
    LOG_WARN("reopen feeder failed", K(ret));
  }
  return ret;
}

int ObNdjsonFileRowReader::close()
{
  if (NULL != feeder_) {
    feeder_->~ObFileLineFeeder();
    feeder_ = NULL;
  }
  batch_arena_.reset();
  return OB_SUCCESS;
}

int ObNdjsonFileRowReader::convert_json_value(const ObIJsonBase *value,
                                              const ReadFileColumn &col,
                                              int64_t line_no, int64_t col_idx, ObObj &out)
{
  int ret = OB_SUCCESS;
  out.reset();
  if (OB_ISNULL(value) || ObJsonNodeType::J_NULL == value->json_type()) {
    out.set_null();
    return ret;
  }
  const ObJsonNodeType jt = value->json_type();
  if (ObJsonNodeType::J_OBJECT == jt || ObJsonNodeType::J_ARRAY == jt) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT, static_cast<int32_t>(strlen("nested object or array is not supported")),
                   "nested object or array is not supported");
    return ret;
  }
  switch (col.type_) {
    case common::ObVarcharType: {
      if (ObJsonNodeType::J_STRING != jt) {
        ret = OB_ERR_INVALID_JSON_TEXT;
      } else {
        const ObJsonString *str_node = static_cast<const ObJsonString *>(value);
        const ObString &s = str_node->value();
        char *buf = static_cast<char *>(batch_arena_.alloc(s.length()));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc varchar failed", K(ret));
        } else {
          MEMCPY(buf, s.ptr(), s.length());
          out.set_varchar(ObString(static_cast<int32_t>(s.length()), buf));
          out.set_collation_type(common::ObCharset::get_default_collation(
              common::ObCharset::get_default_charset()));
        }
      }
      break;
    }
    case common::ObIntType: {
      if (ObJsonNodeType::J_INT == jt) {
        out.set_int(static_cast<const ObJsonInt *>(value)->value());
      } else if (ObJsonNodeType::J_UINT == jt) {
        const uint64_t u = static_cast<const ObJsonUint *>(value)->value();
        if (u > static_cast<uint64_t>(INT64_MAX)) {
          ret = OB_DATA_OUT_OF_RANGE;
        } else {
          out.set_int(static_cast<int64_t>(u));
        }
      } else {
        ret = OB_ERR_INVALID_JSON_TEXT;
      }
      break;
    }
    case common::ObDoubleType: {
      if (ObJsonNodeType::J_DOUBLE == jt) {
        out.set_double(static_cast<const ObJsonDouble *>(value)->value());
      } else if (ObJsonNodeType::J_INT == jt) {
        out.set_double(static_cast<double>(static_cast<const ObJsonInt *>(value)->value()));
      } else if (ObJsonNodeType::J_UINT == jt) {
        out.set_double(static_cast<double>(static_cast<const ObJsonUint *>(value)->value()));
      } else {
        ret = OB_ERR_INVALID_JSON_TEXT;
      }
      break;
    }
    case common::ObTinyIntType: {
      if (ObJsonNodeType::J_BOOLEAN != jt) {
        ret = OB_ERR_INVALID_JSON_TEXT;
      } else {
        out.set_tinyint(static_cast<const ObJsonBoolean *>(value)->value() ? 1 : 0);
      }
      break;
    }
    case common::ObMySQLDateType: {
      if (ObJsonNodeType::J_STRING != jt) {
        ret = OB_ERR_INVALID_JSON_TEXT;
      } else {
        const ObString &s = static_cast<const ObJsonString *>(value)->value();
        ObMySQLDate mdate;
        if (OB_SUCCESS != ObTimeConverter::str_to_mdate(s, mdate)) {
          ret = OB_ERR_INVALID_JSON_TEXT;
        } else {
          out.set_mysql_date(mdate);
        }
      }
      break;
    }
    case common::ObMySQLDateTimeType: {
      if (ObJsonNodeType::J_STRING != jt) {
        ret = OB_ERR_INVALID_JSON_TEXT;
      } else {
        const ObString &s = static_cast<const ObJsonString *>(value)->value();
        ObTimeConvertCtx cvrt_ctx(tz_info_, false);
        ObMySQLDateTime mdt;
        int16_t scale = 0;
        if (OB_SUCCESS != ObTimeConverter::str_to_mdatetime(s, cvrt_ctx, mdt, &scale)) {
          ret = OB_ERR_INVALID_JSON_TEXT;
        } else {
          out.set_mysql_datetime(mdt);
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected column type", K(ret), K(col));
      break;
    }
  }
  UNUSED(line_no);
  UNUSED(col_idx);
  return ret;
}

int ObNdjsonFileRowReader::read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows)
{
  int ret = OB_SUCCESS;
  n_rows = 0;
  while (OB_SUCC(ret) && n_rows < max_rows) {
    ObString line;
    int64_t line_no = 0;
    if (OB_FAIL(feeder_->next_line(line, line_no))) {
      if (OB_ITER_END == ret) {
        break;
      }
      break;
    }
    ObJsonNode *root = NULL;
    const char *syntax_err = NULL;
    uint64_t offset = 0;
    if (OB_FAIL(ObJsonParser::parse_json_text(&batch_arena_, line.ptr(), line.length(),
                                              syntax_err, &offset, root))) {
      if (options_.ignore_errors_) {
        ++bad_rows_;
        ret = OB_SUCCESS;
        continue;
      }
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT,
                     static_cast<int32_t>(strlen("invalid json line")), "invalid json line");
      LOG_WARN("parse json line failed", K(ret), K(line_no), K(line));
      break;
    }
    if (OB_ISNULL(root) || ObJsonNodeType::J_OBJECT != root->json_type()) {
      if (options_.ignore_errors_) {
        ++bad_rows_;
        ret = OB_SUCCESS;
        continue;
      }
      ret = OB_ERR_INVALID_JSON_TEXT;
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT, static_cast<int32_t>(strlen("each ndjson line must be a flat object")),
                     "each ndjson line must be a flat object");
      LOG_WARN("ndjson line is not an object", K(ret), K(line_no));
      break;
    }
    ObObj *row = cells + n_rows * col_count_;
    bool row_failed = false;
    for (int64_t c = 0; OB_SUCC(ret) && c < col_count_; ++c) {
      const ReadFileColumn &col = cols_.at(c);
      ObIJsonBase *value = NULL;
      if (OB_FAIL(root->get_object_value(col.name_, value))) {
        // missing field -> NULL
        row[c].set_null();
        continue;
      }
      if (OB_FAIL(convert_json_value(value, col, line_no, c, row[c]))) {
        row_failed = true;
        break;
      }
    }
    if (OB_FAIL(ret)) {
      if (options_.ignore_errors_ && row_failed) {
        ret = OB_SUCCESS;
        ++bad_rows_;
        continue;
      }
      break;
    }
    ++n_rows;
    ++total_rows_;
  }
  return ret;
}

// ---------------------------------------------------------------------------
// glob() list reader: single VARCHAR column holding file paths
// ---------------------------------------------------------------------------
class ObGlobListRowReader : public ObIFileRowReader
{
public:
  ObGlobListRowReader(ObIAllocator &alloc, const ObIArray<ObString> &paths)
    : ObIFileRowReader(alloc), next_idx_(0)
  {
    for (int64_t i = 0; i < paths.count(); ++i) {
      ObString s;
      if (OB_SUCCESS == ob_write_string(batch_arena_, paths.at(i), s, true)) {
        paths_.push_back(s);
      }
    }
  }
  virtual ~ObGlobListRowReader() {}
  virtual int open() override { next_idx_ = 0; return OB_SUCCESS; }
  virtual int rescan() override { next_idx_ = 0; total_rows_ = 0; return OB_SUCCESS; }
  virtual int close() override { return OB_SUCCESS; }
  virtual int read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows) override
  {
    int ret = OB_SUCCESS;
    n_rows = 0;
    while (OB_SUCC(ret) && n_rows < max_rows && next_idx_ < paths_.count()) {
      const ObString &p = paths_.at(next_idx_++);
      char *buf = static_cast<char *>(batch_arena_.alloc(p.length()));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc path buffer failed", K(ret));
      } else {
        MEMCPY(buf, p.ptr(), p.length());
        ObObj &out = cells[n_rows * col_count_];
        out.reset();
        out.set_varchar(ObString(static_cast<int32_t>(p.length()), buf));
        out.set_collation_type(common::ObCharset::get_default_collation(
            common::ObCharset::get_default_charset()));
        ++n_rows;
        ++total_rows_;
      }
    }
    return ret;
  }
private:
  ObArray<ObString> paths_;
  int64_t next_idx_;
};

// ---------------------------------------------------------------------------
// reader factory & init
// ---------------------------------------------------------------------------
int ObIFileRowReader::init(int64_t col_count, const ObIArray<ReadFileColumn> &cols,
                           const ObTimeZoneInfo *tz_info)
{
  int ret = OB_SUCCESS;
  col_count_ = col_count;
  tz_info_ = tz_info;
  cols_.reset();
  for (int64_t i = 0; i < cols.count(); ++i) {
    ReadFileColumn c;
    c.type_ = cols.at(i).type_;
    c.accuracy_ = cols.at(i).accuracy_;
    c.nullable_ = cols.at(i).nullable_;
    c.max_length_ = cols.at(i).max_length_;
    if (OB_FAIL(ob_write_string(struct_arena_, cols.at(i).name_, c.name_))) {
      LOG_WARN("copy column name failed", K(ret));
      break;
    }
    if (OB_FAIL(cols_.push_back(c))) {
      LOG_WARN("push column failed", K(ret));
      break;
    }
  }
  return ret;
}

int create_file_row_reader(ObIAllocator &alloc, ReadFileFormat format,
                           const ObIArray<ObString> &paths, const ReadFileOptions &options,
                           const ObIArray<ReadFileColumn> &cols, const ObTimeZoneInfo *tz_info,
                           ObIFileRowReader *&reader)
{
  int ret = OB_SUCCESS;
  reader = NULL;
  switch (format) {
    case ReadFileFormat::CSV: {
      ObCsvFileRowReader *r = new (alloc.alloc(sizeof(ObCsvFileRowReader)))
          ObCsvFileRowReader(alloc, paths, options);
      if (OB_ISNULL(r)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(r->init(cols.count(), cols, tz_info))) {
        LOG_WARN("init csv reader failed", K(ret));
      } else {
        reader = r;
      }
      break;
    }
    case ReadFileFormat::NDJSON:
    case ReadFileFormat::JSON: {
      ObNdjsonFileRowReader *r = new (alloc.alloc(sizeof(ObNdjsonFileRowReader)))
          ObNdjsonFileRowReader(alloc, paths, options);
      if (OB_ISNULL(r)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(r->init(cols.count(), cols, tz_info))) {
        LOG_WARN("init ndjson reader failed", K(ret));
      } else {
        reader = r;
      }
      break;
    }
    case ReadFileFormat::GLOB_LIST: {
      ObGlobListRowReader *r = new (alloc.alloc(sizeof(ObGlobListRowReader)))
          ObGlobListRowReader(alloc, paths);
      if (OB_ISNULL(r)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(r->init(1, cols, tz_info))) {
        LOG_WARN("init glob reader failed", K(ret));
      } else {
        reader = r;
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED,
                     static_cast<int32_t>(strlen(read_file_format_to_str(format))),
                     read_file_format_to_str(format));
      break;
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Schema inference
// ---------------------------------------------------------------------------
struct InferColStat
{
  CellKind kind_;
  int64_t max_len_;
  InferColStat() : kind_(CK_NULL), max_len_(0) {}
};

static void build_inferred_column(ObIAllocator &alloc, const ObString &name,
                                  const InferColStat &st, ReadFileColumn &col)
{
  int ret = OB_SUCCESS;
  col.reset();
  col.name_ = name;
  col.max_length_ = st.max_len_;
  switch (st.kind_) {
    case CK_BOOL:
      col.type_ = common::ObTinyIntType;
      break;
    case CK_INT:
      col.type_ = common::ObIntType;
      break;
    case CK_DOUBLE:
      col.type_ = common::ObDoubleType;
      break;
    case CK_DATE:
      col.type_ = common::ObMySQLDateType;
      col.max_length_ = std::max(st.max_len_, static_cast<int64_t>(10));
      break;
    case CK_DATETIME:
      col.type_ = common::ObMySQLDateTimeType;
      col.max_length_ = std::max(st.max_len_, static_cast<int64_t>(19));
      break;
    case CK_VARCHAR:
      col.type_ = common::ObVarcharType;
      break;
    case CK_NULL:
    default:
      col.type_ = common::ObNullType;
      break;
  }
  if (common::ObVarcharType == col.type_) {
    const int64_t vlen = std::min(std::max(st.max_len_, static_cast<int64_t>(1)),
                                  static_cast<int64_t>(common::OB_MAX_VARCHAR_LENGTH));
    col.accuracy_.set_length(vlen);
  }
  col.nullable_ = true;
  UNUSED(alloc);
  UNUSED(ret);
}

static int infer_csv_schema(ObIAllocator &alloc, const ObIArray<ObString> &paths,
                            const ReadFileOptions &options, ObIArray<ReadFileColumn> &cols)
{
  int ret = OB_SUCCESS;
  ObArray<InferColStat> stats;
  ObArray<ObString> names;
  ObFileLineFeeder feeder(alloc, paths);
  int64_t expect_cols = 0;
  int64_t data_lines = 0;
  if (OB_FAIL(feeder.open())) {
    LOG_WARN("open feeder failed", K(ret));
  }
  while (OB_SUCC(ret)) {
    ObString line;
    int64_t line_no = 0;
    if (OB_FAIL(feeder.next_line(line, line_no))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      }
      break;
    }
    ObSEArray<CsvField, 128> fields;
    if (OB_FAIL(split_csv_line(line, options, NULL, fields))) {
      break;
    }
    if (options.header_ && 0 == data_lines) {
      // header row: column names
      for (int64_t c = 0; OB_SUCC(ret) && c < fields.count(); ++c) {
        ObString name;
        if (!fields.at(c).is_null_ && fields.at(c).len_ > 0) {
          if (OB_FAIL(ob_write_string(alloc,
                                      ObString(static_cast<int32_t>(fields.at(c).len_),
                                               fields.at(c).ptr_),
                                      name))) {
            LOG_WARN("copy header name failed", K(ret));
          }
        }
        if (OB_FAIL(names.push_back(name))) {
          LOG_WARN("push name failed", K(ret));
        }
      }
      ++data_lines;
      expect_cols = fields.count();
      continue;
    }
    ++data_lines;
    if (0 == expect_cols) {
      expect_cols = fields.count();
    }
    // union the stats
    for (int64_t c = 0; OB_SUCC(ret) && c < fields.count(); ++c) {
      while (c >= stats.count()) {
        InferColStat st;
        if (OB_FAIL(stats.push_back(st))) {
          LOG_WARN("push stat failed", K(ret));
          break;
        }
      }
      if (OB_FAIL(ret)) {
        break;
      }
      const CsvField &f = fields.at(c);
      InferColStat &st = stats.at(c);
      if (f.is_null_) {
        continue;
      }
      int64_t int_val = 0;
      double dbl_val = 0;
      int32_t date_val = 0;
      int64_t dt_val = 0;
      int64_t text_len = 0;
      const CellKind k = classify_text(ObString(static_cast<int32_t>(f.len_), f.ptr_),
                                       int_val, dbl_val, date_val, dt_val, text_len);
      st.kind_ = merge_kind(st.kind_, k);
      st.max_len_ = std::max(st.max_len_, text_len);
    }
  }
  if (OB_FAIL(ret)) {
  } else if (expect_cols == 0) {
    ret = OB_ERR_DATA_TRUNCATED;
    LOG_USER_ERROR(OB_ERR_DATA_TRUNCATED, static_cast<int32_t>(strlen("csv file has no data rows")),
                   "csv file has no data rows");
  } else {
    for (int64_t c = 0; OB_SUCC(ret) && c < expect_cols; ++c) {
      ReadFileColumn col;
      InferColStat st;
      if (c < stats.count()) {
        st = stats.at(c);
      }
      ObString name;
      if (c < names.count() && !names.at(c).empty()) {
        name = names.at(c);
      } else {
        char name_buf[32];
        const int64_t nlen = snprintf(name_buf, sizeof(name_buf), "c%ld", c + 1);
        if (OB_FAIL(ob_write_string(alloc, ObString(static_cast<int32_t>(nlen), name_buf),
                                    name))) {
          LOG_WARN("copy generated name failed", K(ret));
        }
      }
      if (OB_FAIL(ret)) {
      } else {
        build_inferred_column(alloc, name, st, col);
        if (OB_FAIL(cols.push_back(col))) {
          LOG_WARN("push column failed", K(ret));
        }
      }
    }
  }
  return ret;
}

static int infer_ndjson_schema(ObIAllocator &alloc, const ObIArray<ObString> &paths,
                               const ReadFileOptions &options, ObIArray<ReadFileColumn> &cols)
{
  int ret = OB_SUCCESS;
  ObFileLineFeeder feeder(alloc, paths);
  ObArray<ObString> names;   // allocated from alloc
  ObArray<InferColStat> stats;
  if (OB_FAIL(feeder.open())) {
    LOG_WARN("open feeder failed", K(ret));
  }
  ObArenaAllocator line_alloc("NdjsonInfer");
  while (OB_SUCC(ret)) {
    ObString line;
    int64_t line_no = 0;
    if (OB_FAIL(feeder.next_line(line, line_no))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      }
      break;
    }
    line_alloc.reset();
    ObJsonNode *root = NULL;
    const char *syntax_err = NULL;
    uint64_t offset = 0;
    if (OB_FAIL(ObJsonParser::parse_json_text(&line_alloc, line.ptr(), line.length(),
                                              syntax_err, &offset, root))) {
      LOG_WARN("parse json line failed", K(ret), K(line_no), K(line));
      break;
    }
    if (OB_ISNULL(root) || ObJsonNodeType::J_OBJECT != root->json_type()) {
      ret = OB_ERR_INVALID_JSON_TEXT;
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT, static_cast<int32_t>(strlen("each ndjson line must be a flat object")),
                     "each ndjson line must be a flat object");
      LOG_WARN("ndjson line is not an object", K(ret), K(line_no));
      break;
    }
    const int64_t member_cnt = static_cast<int64_t>(root->member_count());
    for (int64_t m = 0; OB_SUCC(ret) && m < member_cnt; ++m) {
      ObString key;
      ObIJsonBase *value = NULL;
      if (OB_FAIL(root->get_object_value(static_cast<uint64_t>(m), key, value))) {
        LOG_WARN("get member failed", K(ret), K(m));
      } else {
        // find or create the column (field union, first-seen order)
        int64_t col_idx = -1;
        for (int64_t c = 0; c < names.count(); ++c) {
          if (0 == names.at(c).case_compare(key)) {
            col_idx = c;
            break;
          }
        }
        if (col_idx < 0) {
          ObString name_copy;
          if (OB_FAIL(ob_write_string(alloc, key, name_copy))) {
            LOG_WARN("copy key failed", K(ret));
          } else if (OB_FAIL(names.push_back(name_copy))) {
            LOG_WARN("push name failed", K(ret));
          } else {
            InferColStat st;
            col_idx = names.count() - 1;
            if (OB_FAIL(stats.push_back(st))) {
              LOG_WARN("push stat failed", K(ret));
            }
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(value) || ObJsonNodeType::J_NULL == value->json_type()) {
          // keep all-null state
        } else if (ObJsonNodeType::J_OBJECT == value->json_type()
                   || ObJsonNodeType::J_ARRAY == value->json_type()) {
          ret = OB_ERR_INVALID_JSON_TEXT;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT,
                         static_cast<int32_t>(strlen("nested object or array is not supported")),
                         "nested object or array is not supported");
          LOG_WARN("nested value found", K(ret), K(line_no), K(key));
        } else {
          InferColStat &st = stats.at(col_idx);
          CellKind k = CK_VARCHAR;
          int64_t text_len = 0;
          int64_t int_val = 0;
          double dbl_val = 0;
          int32_t date_val = 0;
          int64_t dt_val = 0;
          switch (value->json_type()) {
            case ObJsonNodeType::J_INT: {
              k = CK_INT;
              break;
            }
            case ObJsonNodeType::J_UINT: {
              const uint64_t u = static_cast<const ObJsonUint *>(value)->value();
              k = (u > static_cast<uint64_t>(INT64_MAX)) ? CK_DOUBLE : CK_INT;
              break;
            }
            case ObJsonNodeType::J_DOUBLE:
            case ObJsonNodeType::J_DECIMAL: {
              k = CK_DOUBLE;
              break;
            }
            case ObJsonNodeType::J_BOOLEAN: {
              k = CK_BOOL;
              break;
            }
            case ObJsonNodeType::J_STRING: {
              const ObString &s = static_cast<const ObJsonString *>(value)->value();
              k = classify_text(s, int_val, dbl_val, date_val, dt_val, text_len);
              break;
            }
            default:
              k = CK_VARCHAR;
              break;
          }
          st.kind_ = merge_kind(st.kind_, k);
          st.max_len_ = std::max(st.max_len_, text_len);
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (names.empty()) {
    ret = OB_ERR_DATA_TRUNCATED;
    LOG_USER_ERROR(OB_ERR_DATA_TRUNCATED,
                   static_cast<int32_t>(strlen("ndjson file has no data rows")),
                   "ndjson file has no data rows");
  } else {
    for (int64_t c = 0; OB_SUCC(ret) && c < names.count(); ++c) {
      ReadFileColumn col;
      build_inferred_column(alloc, names.at(c), stats.at(c), col);
      if (OB_FAIL(cols.push_back(col))) {
        LOG_WARN("push column failed", K(ret));
      }
    }
  }
  return ret;
}

int infer_file_schema(ObIAllocator &alloc, ReadFileFormat format,
                      const ObIArray<ObString> &paths, const ReadFileOptions &options,
                      const ObTimeZoneInfo *tz_info, ObIArray<ReadFileColumn> &cols,
                      ReadFileFingerprint &fp)
{
  int ret = OB_SUCCESS;
  cols.reset();
  fp.reset();
  if (OB_UNLIKELY(paths.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("no files", K(ret));
  } else if (options.sample_size_ != -1) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, static_cast<int32_t>(strlen("sample_size other than -1")),
                   "sample_size other than -1");
  } else if (ReadFileFormat::GLOB_LIST == format) {
    ReadFileColumn col;
    ObString name;
    if (OB_FAIL(ob_write_string(alloc, ObString::make_string("file"), name))) {
      LOG_WARN("copy name failed", K(ret));
    } else {
      col.name_ = name;
      col.type_ = common::ObVarcharType;
      col.accuracy_.set_length(OB_MAX_FILE_NAME_LENGTH);
      col.nullable_ = false;
      col.max_length_ = OB_MAX_FILE_NAME_LENGTH;
      if (OB_FAIL(cols.push_back(col))) {
        LOG_WARN("push column failed", K(ret));
      }
    }
  } else if (ReadFileFormat::NDJSON == format || ReadFileFormat::JSON == format) {
    if (OB_FAIL(infer_ndjson_schema(alloc, paths, options, cols))) {
      LOG_WARN("infer ndjson schema failed", K(ret));
    }
  } else if (ReadFileFormat::PARQUET == format) {
    if (OB_FAIL(infer_parquet_schema(alloc, paths.at(0), cols))) {
      LOG_WARN("infer parquet schema failed", K(ret));
    }
  } else {
    if (OB_FAIL(infer_csv_schema(alloc, paths, options, cols))) {
      LOG_WARN("infer csv schema failed", K(ret), K(paths));
    }
  }
  if (OB_SUCC(ret)) {
    // fingerprint = max over files (all files must exist)
    for (int64_t i = 0; OB_SUCC(ret) && i < paths.count(); ++i) {
      ReadFileFingerprint one;
      if (OB_FAIL(get_file_fingerprint(paths.at(i), one))) {
        LOG_WARN("get fingerprint failed", K(ret), K(paths.at(i)));
      } else {
        if (one.file_size_ > fp.file_size_) {
          fp.file_size_ = one.file_size_;
        }
        if (one.mtime_sec_ > fp.mtime_sec_
            || (one.mtime_sec_ == fp.mtime_sec_ && one.mtime_nsec_ > fp.mtime_nsec_)) {
          fp.mtime_sec_ = one.mtime_sec_;
          fp.mtime_nsec_ = one.mtime_nsec_;
        }
      }
    }
  }
  UNUSED(tz_info);
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
