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

#include "lib/file/ob_file.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

#include <vector>

namespace oceanbase
{
namespace sql
{

namespace ob_csv_reader_core
{
class ObCsvReaderTokenizer;
}

/**
 * @brief The reader of a csv file.
 *
 * The reader streams the file: the whole file is never materialized, one record
 * is parsed at a time. The values of the row produced by the last next_row()
 * call are valid until the next next_row() call.
 *
 * The rules of the csv format:
 *   - the first record holds the column names when options.header_ is true,
 *   - a field is enclosed when its first character is options.quote_,
 *   - inside an enclosure a doubled quote is a literal quote and the escape
 *     character escapes the next character,
 *   - an empty unquoted field is a NULL, a quoted empty field is an empty string,
 *   - a record is terminated by "\n" or by "\r\n".
 */
class ObCsvFileScanReader : public ObFileScanReader
{
public:
  ObCsvFileScanReader(const ObFileScanOptions &options,
                      const common::ObIArray<ObFileColumnDef> &columns);
  virtual ~ObCsvFileScanReader();
  virtual int open() override;
  virtual int next_row() override;
  virtual int close() override;
  virtual int infer_schema(common::ObIAllocator &allocator,
                            common::ObIArray<ObFileColumnDef> &columns) override;

private:
  int init_options_(const ObFileScanOptions &options);
  int init_columns_(const common::ObIArray<ObFileColumnDef> &columns);
  int next_record_();
  int next_data_record_();
  int build_row_values_();
  int infer_csv_schema_(common::ObIAllocator &allocator,
                        common::ObIArray<ObFileColumnDef> &columns);

private:
  ObFileScanOptions options_;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> columns_;
  common::ObArenaAllocator perm_alloc_;
  common::ObArenaAllocator row_alloc_;
  common::ObFileReader file_reader_;
  ob_csv_reader_core::ObCsvReaderTokenizer *tokenizer_;
  char *read_buf_;
  int64_t read_buf_size_;
  int64_t file_offset_;
  int64_t record_no_;
  bool is_opened_;
  bool is_header_read_;
  int init_ret_;
};

/**
 * @brief The reader of a file of newline delimited json objects.
 *
 * The reader streams the file: the whole file is never materialized, one record
 * is parsed at a time. The values of the row produced by the last next_row()
 * call are valid until the next next_row() call.
 *
 * The rules of the newline delimited json format:
 *   - one json object per line, the blank lines are skipped,
 *   - only flat objects are supported, a nested object or array is an error,
 *   - the columns are the union of the keys of all the records, a key that is
 *     not a column is ignored and a column that is not in a record is a NULL.
 */
class ObJsonlFileScanReader : public ObFileScanReader
{
public:
  ObJsonlFileScanReader(const ObFileScanOptions &options,
                        const common::ObIArray<ObFileColumnDef> &columns);
  virtual ~ObJsonlFileScanReader();
  virtual int open() override;
  virtual int next_row() override;
  virtual int close() override;
  virtual int infer_schema(common::ObIAllocator &allocator,
                            common::ObIArray<ObFileColumnDef> &columns) override;

private:
  int init_options_(const ObFileScanOptions &options);
  int init_columns_(const common::ObIArray<ObFileColumnDef> &columns);
  int next_line_();
  int find_line_(bool eof);
  void consume_line_();
  bool is_blank_line_() const;
  int build_row_values_();
  int infer_jsonl_schema_(common::ObIAllocator &allocator,
                          common::ObIArray<ObFileColumnDef> &columns);

private:
  ObFileScanOptions options_;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> columns_;
  common::ObArenaAllocator perm_alloc_;
  common::ObArenaAllocator row_alloc_;
  common::ObFileReader file_reader_;
  std::vector<char> line_buf_;
  char *read_buf_;
  int64_t read_buf_size_;
  int64_t file_offset_;
  int64_t line_no_;
  int64_t line_len_;
  int64_t line_consumed_;
  int64_t line_scan_pos_;
  bool is_opened_;
  int init_ret_;
};

} // end namespace sql
} // end namespace oceanbase
