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

#pragma once

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_array.h"
#include "lib/allocator/ob_allocator.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{

// Deterministic type inference result. The enum order is the widening order used
// by FileScanUtils::merge_infer_type().
enum class FileInferType : int64_t
{
  FIT_NULL = 0,
  FIT_BOOLEAN = 1,
  FIT_BIGINT = 2,
  FIT_DOUBLE = 3,
  FIT_DATE = 4,
  FIT_DATETIME = 5,
  FIT_VARCHAR = 6,
};

struct FileScanOptions
{
  bool header_ = true;
  // Only -1 (infer from the whole file) is supported by this task.
  int64_t sample_size_ = -1;
  char delimiter_ = ',';
  char quote_ = '"';
  char escape_ = '"';
  // Text representing SQL NULL, empty means "an empty field is NULL".
  ObString null_str_;
  bool null_str_configured_ = false;

  TO_STRING_KV(K_(header), K_(sample_size), K_(delimiter), K_(quote), K_(escape),
               K_(null_str), K_(null_str_configured));
};

struct FileColumnDesc
{
  ObString name_;
  ObObjType type_ = ObNullType;
  bool nullable_ = true;

  TO_STRING_KV(K_(name), K_(type), K_(nullable));
};

// Identity of a file, used to detect changes between batches and rescans.
struct FileStat
{
  int64_t device_ = -1;
  int64_t inode_ = -1;
  int64_t size_ = -1;
  int64_t mtime_ns_ = -1;

  bool operator==(const FileStat &other) const
  {
    return device_ == other.device_ && inode_ == other.inode_
        && size_ == other.size_ && mtime_ns_ == other.mtime_ns_;
  }
  bool operator!=(const FileStat &other) const { return !(*this == other); }
  TO_STRING_KV(K_(device), K_(inode), K_(size), K_(mtime_ns));
};

// Stateless helpers shared by the CSV reader and by COPY ... TO.
class FileScanUtils
{
public:
  // Resolve `path` and verify that it stays inside `secure_file_priv`. Symlinks
  // are resolved before the prefix check so they cannot be used to escape it.
  // An empty secure_file_priv disables file access completely.
  static int resolve_and_check_path(const ObString &secure_file_priv,
                                    const ObString &path,
                                    ObIAllocator &allocator,
                                    ObString &resolved_path);
  // Same check for a file that does not exist yet (COPY target): the parent
  // directory is resolved and checked instead.
  static int resolve_and_check_parent_path(const ObString &secure_file_priv,
                                           const ObString &path,
                                           ObIAllocator &allocator,
                                           ObString &resolved_path);
  static int glob_files(ObIAllocator &allocator,
                        const ObString &pattern,
                        ObIArray<ObString> &files);
  static int stat_file(const char *path, FileStat &file_stat);

  static const char *infer_type_name(const FileInferType type);
  static ObObjType infer_to_obj_type(const FileInferType type);
  static FileInferType merge_infer_type(const FileInferType left, const FileInferType right);

  // Narrowest type able to hold `text`, empty input yields FIT_NULL.
  static int sniff_scalar(const ObString &text, FileInferType &type);
  // Convert `text` (already sniffed as `type`) into an ObObj. The varchar case
  // keeps a pointer into `text`.
  static int convert_value(const ObString &text, const FileInferType type, ObObj &obj);

  static int parse_int64(const ObString &text, int64_t &value, bool &fit);
  static int parse_double(const ObString &text, double &value);
  // YYYY-MM-DD, the calendar is validated (leap years included).
  static int parse_date(const ObString &text, int32_t &days);
  // "YYYY-MM-DD HH:MM:SS[.ffffff]" or with a 'T' separator.
  static int parse_datetime(const ObString &text, int64_t &usec);

  // True when `text` equals the configured null string (or is empty when no
  // null string is configured).
  static bool is_null_field(const ObString &text, const FileScanOptions &options);
};

// Streaming RFC4180 CSV reader. Fields returned by get_next_row() point into the
// reader owned row buffer and stay valid until the next call.
class CsvFileReader
{
public:
  CsvFileReader();
  ~CsvFileReader();
  int open(const char *path, const FileScanOptions &options);
  int close();
  // Rewind, used by rescan and by the schema inference second pass.
  int rescan();
  // Full scan: merges the type of every column and records whether a column saw
  // an empty field. The reader is rewound afterwards.
  int infer_schema(ObIAllocator &allocator, ObIArray<FileColumnDesc> &columns);
  // OB_ITER_END at end of file.
  int get_next_row(ObIArray<ObString> &fields);
  int get_next_batch(ObIAllocator &allocator,
                     ObIArray<ObArray<ObString>> &rows,
                     const int64_t capacity,
                     int64_t &row_count);
  int64_t get_line_no() const { return line_no_; }
  int64_t get_column_count() const { return column_count_; }
  const FileStat &get_stat() const { return stat_; }
  int check_unchanged() const;
  bool is_opened() const { return nullptr != fp_; }
  TO_STRING_KV(K_(line_no), K_(column_count), K_(is_opened));

private:
  int refill();
  int read_char(char &ch);
  int append_char(const char ch);
  // Reads one record, returns OB_ITER_END when the file is exhausted.
  int parse_row();
  void close_file();

  char *row_buf_ = nullptr;
  int64_t row_buf_cap_ = 0;
  int64_t row_buf_pos_ = 0;
  char *read_buf_ = nullptr;
  int64_t read_buf_cap_ = 0;
  int64_t read_buf_pos_ = 0;
  int64_t read_buf_len_ = 0;
  FILE *fp_ = nullptr;
  ObString path_;
  FileScanOptions options_;
  FileStat stat_;
  int64_t line_no_ = 0;
  int64_t column_count_ = 0;
  bool eof_ = false;
  bool opened_ = false;
  ObSEArray<ObString, 32> row_fields_;
};

// CSV writer used by COPY ... TO. The target must not exist; callers either
// commit() or abort(), abort() removes the partially written file.
class CsvFileWriter
{
public:
  CsvFileWriter();
  ~CsvFileWriter();
  int open(const char *path, const FileScanOptions &options);
  int write_header(const ObIArray<ObString> &column_names);
  int write_row(const ObIArray<ObString> &values, const ObIArray<bool> &is_null);
  int commit();
  int abort();
  int64_t get_row_count() const { return row_count_; }
  TO_STRING_KV(K_(row_count), K_(committed));

private:
  int write_field(const ObString &value, const bool is_null);
  int write_raw(const char *data, const int64_t length);
  FILE *fp_ = nullptr;
  ObString path_;
  FileScanOptions options_;
  int64_t row_count_ = 0;
  bool opened_ = false;
  bool committed_ = false;
};

} // namespace sql
} // namespace oceanbase
