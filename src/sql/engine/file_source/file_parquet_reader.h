/**
 * Copyright (c) 2026 OceanBase
 *
 * OceanBase is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * OceanBase is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OceanBase. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "sql/engine/file_source/file_csv_reader.h"
#include "sql/engine/file_source/file_parquet_format.h"
#include "sql/engine/file_source/file_source_def.h"

#include <string>
#include <vector>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Streaming reader for parquet files.
///
/// The reader implements the parts of the parquet format that are required by
/// the "file as database" task without any third party dependency:
///
///   * the file metadata is decoded with a self contained reader for the
///     thrift compact protocol,
///   * the `SNAPPY` and `UNCOMPRESSED` codecs are supported,
///   * the `PLAIN`, `PLAIN_DICTIONARY`, `RLE_DICTIONARY` and `RLE` encodings
///     are supported for the data and the definition levels,
///   * data pages of type `DATA_PAGE` and `DATA_PAGE_V2` are supported.
///
/// Only the pages of the requested row groups are read from the file, the
/// decoded values are kept per row group only.
class FileParquetReader
{
public:
  FileParquetReader();
  ~FileParquetReader();

public:
  FileSourceError open(const std::string &path, const FileReadOptions &options);
  FileSourceError infer_schema(FileSchema &schema);
  FileSourceError scan(const FileRowCallback &callback);
  /// Read the next row of the file.
  ///
  /// The method implements the pull model used by the SQL executor, the row
  /// groups are decoded one by one and only the rows of the current row group
  /// are kept in memory. It returns false when the end of the file is reached
  /// or when an error occurs, the error is reported through `error`.
  bool next_row(FileRow &row, FileSourceError &error);
  const FileSignature &signature() const { return signature_; }
  FileSourceError check_unchanged() const;

private:
  FileSourceError read_footer();
  FileSourceError decode_row_group(int64_t row_group_index, std::vector<std::vector<std::string>> &values,
                                   std::vector<std::vector<bool>> &nulls) const;

private:
  std::string path_;
  FileReadOptions options_;
  FileSignature signature_;
  FileSchema schema_;
  bool opened_ = false;
  /// Raw footer metadata, kept in memory for the whole scan.
  std::vector<unsigned char> footer_;
  /// Schema elements of the file, the first one is the root.
  std::vector<ParquetSchemaNode> schema_nodes_;
  /// Leaf columns in file order.
  std::vector<ParquetColumnDesc> columns_;
  int64_t num_rows_ = 0;
  /// Row group descriptors.
  std::vector<ParquetRowGroup> row_groups_;
  /// Index of the row group that is currently decoded.
  int64_t current_group_ = 0;
  /// Index of the row inside the current row group.
  int64_t current_row_ = 0;
  /// Number of rows of the current row group.
  int64_t current_row_count_ = 0;
  /// Decoded values of the current row group, one vector per column.
  std::vector<std::vector<std::string>> current_values_;
  /// Null flags of the current row group, one vector per column.
  std::vector<std::vector<bool>> current_nulls_;
};

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
