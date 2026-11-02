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

#include "common/ob_accuracy.h"
#include "common/object/ob_obj_type.h"
#include "common/object/ob_object.h"
#include "common/timezone/ob_timezone_info.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "lib/file/ob_file.h"
#include "lib/string/ob_string.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace sql
{

// Format of the file read by read_csv / read_json / read_ndjson / read_parquet
// and the pseudo format used by glob() (a single VARCHAR column of matched file names).
enum class ReadFileFormat
{
  CSV = 0,
  JSON = 1,
  NDJSON = 2,
  PARQUET = 3,
  GLOB_LIST = 4,
  MAX
};

const char *read_file_format_to_str(ReadFileFormat format);
int read_file_format_from_str(const common::ObString &name, ReadFileFormat &format);

// Fingerprint used for change detection between schema inference and scanning.
struct ReadFileFingerprint
{
  ReadFileFingerprint() : file_size_(0), mtime_sec_(0), mtime_nsec_(0) {}
  void reset() { file_size_ = 0; mtime_sec_ = 0; mtime_nsec_ = 0; }
  bool operator==(const ReadFileFingerprint &other) const;
  bool operator!=(const ReadFileFingerprint &other) const { return !(*this == other); }
  bool is_valid() const { return file_size_ > 0; }
  int64_t to_uint64_hash() const;
  int64_t file_size_;
  int64_t mtime_sec_;
  int64_t mtime_nsec_;
  TO_STRING_KV(K_(file_size), K_(mtime_sec), K_(mtime_nsec));
};

// Options carried from the SQL layer into the scan layer.
struct ReadFileOptions
{
  ReadFileOptions()
    : header_(true),
      field_delimiter_(","),
      quote_char_('"'),             // INT64_MAX means "no quote char"
      escape_char_('\\'),           // INT64_MAX means "no escape char"
      null_text_("\\N"),
      empty_field_as_null_(true),
      skip_blank_lines_(true),
      trim_space_(false),
      ignore_errors_(false),
      sample_size_(-1)              // only -1 (full scan) is supported
  {}
  bool header_;
  common::ObString field_delimiter_;
  int64_t quote_char_;
  int64_t escape_char_;
  common::ObString null_text_;
  bool empty_field_as_null_;
  bool skip_blank_lines_;
  bool trim_space_;
  bool ignore_errors_;
  int64_t sample_size_;
  TO_STRING_KV(K_(header), K_(field_delimiter), K_(quote_char), K_(escape_char),
               K_(null_text), K_(empty_field_as_null), K_(skip_blank_lines),
               K_(trim_space), K_(ignore_errors), K_(sample_size));
};

// One inferred column. type_ is one of the seven supported types:
//   ObIntType (BIGINT), ObDoubleType, ObTinyIntType (BOOLEAN), ObVarcharType,
//   ObMySQLDateType (DATE), ObMySQLDateTimeType (DATETIME), ObNullType (all-NULL column).
struct ReadFileColumn
{
  ReadFileColumn() : type_(common::ObNullType), accuracy_(), nullable_(true), max_length_(0) {}
  void reset()
  {
    name_.reset();
    type_ = common::ObNullType;
    accuracy_ = common::ObAccuracy();
    nullable_ = true;
    max_length_ = 0;
  }
  common::ObString name_;
  common::ObObjType type_;
  common::ObAccuracy accuracy_;
  bool nullable_;
  int64_t max_length_;  // max byte length seen for string/date values, used to size VARCHAR
  TO_STRING_KV(K_(name), K_(type), K_(accuracy), K_(nullable), K_(max_length));
};

class ObIFileRowReader;

// Streaming row reader over a set of files of one format.
// Contract:
//  - open() must be called first; rescan() may be called any time to restart
//    from the first file (files are re-opened).
//  - read_batch() fills `cells` (capacity col_count * max_rows) row by row and
//    returns the number of rows read. Strings point into memory owned by the
//    reader and stay valid until the next read_batch()/rescan()/close() call.
//  - Multiple files are concatenated transparently.
class ObIFileRowReader
{
public:
  explicit ObIFileRowReader(common::ObIAllocator &alloc)
    : alloc_(alloc), struct_arena_("FileSqlStruct"),
      col_count_(0), batch_arena_("FileSqlBatch"), tz_info_(NULL), total_rows_(0)
  {}
  virtual ~ObIFileRowReader() {}
  int init(int64_t col_count, const common::ObIArray<ReadFileColumn> &cols,
           const ObTimeZoneInfo *tz_info);
  virtual int open() = 0;
  virtual int rescan() = 0;
  virtual int close() = 0;
  // Read up to max_rows rows. *n_rows == 0 means iteration end.
  virtual int read_batch(common::ObObj *cells, int64_t max_rows, int64_t &n_rows) = 0;
  int64_t get_total_rows() const { return total_rows_; }
  int64_t get_col_count() const { return col_count_; }
protected:
  common::ObIAllocator &alloc_;
  common::ObArenaAllocator struct_arena_;  // owns config copies, never reset while open
  int64_t col_count_;
  common::ObArray<ReadFileColumn> cols_;
  common::ObArenaAllocator batch_arena_;   // owns string payloads of the current batch
  const ObTimeZoneInfo *tz_info_;
  int64_t total_rows_;
};

// Factory. Returns a reader over `paths` (order preserved). For GLOB_LIST the
// single column holds the file path text itself.
int create_file_row_reader(common::ObIAllocator &alloc,
                           ReadFileFormat format,
                           const common::ObIArray<common::ObString> &paths,
                           const ReadFileOptions &options,
                           const common::ObIArray<ReadFileColumn> &cols,
                           const ObTimeZoneInfo *tz_info,
                           ObIFileRowReader *&reader);

// ---------------------------------------------------------------------------
// Utilities shared by resolver / executor / unittests
// ---------------------------------------------------------------------------

// Expand a glob pattern into matching files. Supports '*' and '?' (within one
// path component). `pattern` may be a plain file path as well. All returned
// paths are c-style (nul terminated) strings allocated from `alloc`.
int discover_files(common::ObIAllocator &alloc,
                   const common::ObString &pattern,
                   common::ObIArray<common::ObString> &out_paths);

// Whether the pattern contains wildcard characters.
bool has_wildcard(const common::ObString &path);

// Fingerprint one file (size + mtime). OB_FILE_NOT_EXIST if missing.
int get_file_fingerprint(const common::ObString &path, ReadFileFingerprint &fp);

// ---------------------------------------------------------------------------
// Deterministic type inference (full-file scan, sample_size only supports -1)
// ---------------------------------------------------------------------------

// Infer column schema from the whole file. `cols` output is deterministic for
// identical file content.
int infer_file_schema(common::ObIAllocator &alloc,
                      ReadFileFormat format,
                      const common::ObIArray<common::ObString> &paths,
                      const ReadFileOptions &options,
                      const ObTimeZoneInfo *tz_info,
                      common::ObIArray<ReadFileColumn> &cols,
                      ReadFileFingerprint &fp);

}  // end namespace sql
}  // end namespace oceanbase
