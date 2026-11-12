/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include "file_sql_types.h"

#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <vector>

namespace oceanbase { namespace sql { namespace file_sql {

// The physical Parquet types that the reader supports.
enum class ParquetType { BOOLEAN, INT32, INT64, INT96, FLOAT, DOUBLE, BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY };

struct ParquetColumn
{
  std::string name;
  ParquetType type = ParquetType::BYTE_ARRAY;
  int type_length = 0;      // only for FIXED_LEN_BYTE_ARRAY
  int max_definition_level = 0;
  bool optional = false;
};

// The metadata of one column chunk inside a row group.
struct ParquetColumnChunk
{
  int64_t codec = 0;             // 0 UNCOMPRESSED, 1 SNAPPY
  int64_t num_values = 0;
  int64_t data_page_offset = 0;
  int64_t total_compressed_size = 0;
};

struct ParquetRowGroup
{
  int64_t num_rows = 0;
  std::vector<ParquetColumnChunk> columns;
};

struct ParquetOptions
{
  // Only the listed columns are read. An empty list means every column.
  std::vector<std::string> projection;
  size_t max_row_groups = 4096;
  size_t max_rows = 64 * 1024 * 1024;
  size_t max_batch_bytes = 32 * 1024 * 1024;
};

// Reads a Parquet file and produces rows typed with the shared ScalarValue.
// The reader never opens a path: the caller must authorize it first.
class ParquetReader
{
public:
  ParquetReader() = default;
  ParquetReader(const ParquetReader &) = delete;
  ParquetReader &operator=(const ParquetReader &) = delete;
  FileStatus init(FILE *input, const ParquetOptions &options = {});
  FileStatus next_batch(size_t limit, std::vector<ScalarRow> &rows);
  FileStatus rescan();
  FileStatus check_unchanged() const;
  const std::vector<ColumnSchema> &columns() const { return columns_; }
  uint64_t row_count() const { return row_count_; }
private:
  FileStatus fail(FileCode code, size_t column, const char *message);
  FileStatus read_footer();
  FileStatus build_schema();
  FileStatus read_row_group(uint64_t group_index);
  FileStatus read_column_chunk(uint64_t group_index, size_t column_index);
  bool is_projected(size_t column_index) const;
  FILE *input_ = nullptr;
  ParquetOptions options_;
  struct stat snapshot_ {};
  off_t start_ = 0;
  std::vector<ColumnSchema> columns_;
  std::vector<ParquetColumn> parquet_columns_;
  std::vector<ParquetRowGroup> row_groups_;
  // Values of the current row group, one vector per column.
  std::vector<std::vector<ScalarValue>> group_values_;
  uint64_t row_count_ = 0;
  uint64_t group_rows_ = 0;
  uint64_t group_index_ = 0;
  uint64_t row_in_group_ = 0;
  bool footer_read_ = false;
  FileStatus failure_;
};

}}} // namespace oceanbase::sql::file_sql
