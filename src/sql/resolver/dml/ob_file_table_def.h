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

#pragma once

#include "common/object/ob_object.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief The file formats that can be queried by the file table functions.
 */
enum class ObFileScanFormat : int32_t
{
  CSV = 0,
  JSON = 1,
  PARQUET = 2,
  GLOB = 3,
};

/**
 * @brief The flag of the value of a file table expression that marks the
 *        describe mode.
 *
 * A file table in the describe mode returns the description of the columns of
 * the file instead of the rows of the file, this is the way the DESCRIBE
 * statement of a file table is implemented.
 *
 * Keep it in sync with OB_PARSER_FILE_TABLE_DESCRIBE_FLAG of
 * sql_parser_mysql_mode.y.
 */
static const int64_t OB_FILE_TABLE_DESCRIBE_FLAG = 0x100;

/**
 * @brief The names of the columns of the description of a file table.
 */
extern const char *const OB_FILE_TABLE_DESCRIBE_COLUMNS[6];

/**
 * @brief The length of a string column of a file table whose length is unknown.
 *
 * The length of a column is the length of its longest value, it is known when
 * the schema of the file is inferred. The length of the string columns of a
 * parquet file is not known, the widest string that a column of a table may
 * hold is used for them.
 */
static const int64_t OB_FILE_TABLE_DEFAULT_STRING_LENGTH = 16000;

/**
 * @brief The greatest number of the columns that a file table may have.
 *
 * The limit bounds the memory that the schema of a file may use.
 */
static const int64_t OB_FILE_TABLE_MAX_COLUMN_CNT = 4096;

/**
 * @brief The greatest size in bytes of one record of a file.
 *
 * The reader of a file streams it, it never holds a whole file in memory, but
 * it holds one record. The limit bounds the memory that one record may use.
 */
static const int64_t OB_FILE_TABLE_MAX_RECORD_SIZE = 16 * 1024 * 1024;

/**
 * @brief One column of a file table. The type is inferred from the file content.
 */
struct ObFileColumnDef
{
  common::ObString name_;
  common::ObObjType type_;
  bool nullable_;
  int64_t precision_;
  int64_t scale_;
  // the length in bytes of the longest value of the column, it is the length of
  // the column when the column holds a string, it is 0 when it is unknown
  int64_t max_len_;
  ObFileColumnDef()
    : type_(common::ObNullType), nullable_(true), precision_(0), scale_(0), max_len_(0)
  {}
  TO_STRING_KV(K_(name), K_(type), K_(nullable), K_(precision), K_(scale), K_(max_len));
};

/**
 * @brief The options of a file table function.
 *
 * The defaults follow the DuckDB file reader conventions: comma separated, double
 * quote enclosed, double quote escaped, empty unquoted field is NULL.
 */
struct ObFileScanOptions
{
  common::ObString path_;
  ObFileScanFormat format_;
  bool header_;                   // csv: the first row holds the column names
  int64_t sample_size_;           // only -1 (scan the whole file) is supported
  common::ObString delimiter_;    // csv field terminator
  common::ObString quote_;        // csv field enclosure
  common::ObString escape_;       // csv escape character
  common::ObString null_str_;     // csv: fields equal to this text are NULL
  common::ObString json_format_;  // json: "newline_delimited" or "array"
  bool union_by_name_;            // json: union the fields of every record
  int64_t file_size_;             // size of the file when the schema was inferred
  int64_t file_mtime_;            // mtime of the file when the schema was inferred
  ObFileScanOptions()
    : format_(ObFileScanFormat::CSV),
      header_(true),
      sample_size_(-1),
      delimiter_(1, ","),
      quote_(1, "\""),
      escape_(1, "\""),
      null_str_(),
      json_format_(17, "newline_delimited"),
      union_by_name_(true),
      file_size_(-1),
      file_mtime_(-1)
  {}
  TO_STRING_KV(K_(path), K_(format), K_(header), K_(sample_size), K_(delimiter),
               K_(quote), K_(escape), K_(null_str), K_(json_format),
               K_(union_by_name), K_(file_size), K_(file_mtime));
};

/**
 * @brief The definition of a file table, i.e. the metadata of a file that is
 *        queried as if it were a base table.
 *
 * The definition is created by the resolver: the path and the options come from
 * the table function call, the columns are inferred from the content of the file.
 */
struct ObFileTableDef
{
  ObFileScanOptions options_;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> columns_;
  int64_t row_cnt_;
  // the table describes the columns of the file instead of holding its rows
  bool describe_;
  ObFileTableDef() : row_cnt_(0), describe_(false) {}
  virtual ~ObFileTableDef() {}
  virtual TO_STRING_KV(K_(options), K_(columns), K_(row_cnt), K_(describe));
  /**
   * @brief Copy the definition into the memory of another allocator.
   *
   * The definition holds no expression, only the texts of the path, of the
   * options and of the names of the columns, they are deep copied.
   */
  int deep_copy(const ObFileTableDef &other, common::ObIAllocator *allocator);
};

/**
 * @brief Whether the value of the format option agrees with the format of the
 *        file table function that reads the file.
 *
 * The csv, the newline delimited json and the parquet files are the formats
 * that the file table functions read, the value of the option must name one
 * of them.
 */
inline bool ob_file_is_supported_format(const ObFileScanFormat format,
                                        const common::ObString &value)
{
  bool ret = false;
  switch (format) {
    case ObFileScanFormat::CSV: {
      ret = (0 == value.case_compare("csv"));
      break;
    }
    case ObFileScanFormat::JSON: {
      ret = (0 == value.case_compare("newline_delimited"));
      break;
    }
    case ObFileScanFormat::PARQUET: {
      ret = (0 == value.case_compare("parquet"));
      break;
    }
    default: {
      // the glob function does not read a file, it has no format
      break;
    }
  }
  return ret;
}

/**
 * @brief Copy a text into the memory of an allocator.
 */
inline int ob_file_copy_string(const common::ObString &src,
                               common::ObIAllocator &allocator,
                               common::ObString &dst)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (src.empty()) {
    dst = common::ObString();
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(src.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, src.ptr(), src.length());
    buf[src.length()] = '\0';
    dst.assign_ptr(buf, src.length());
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
