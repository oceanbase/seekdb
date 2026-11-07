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
#include "lib/alloc/ob_iallocator.h"
#include "lib/container/ob_array.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief A reader that streams the rows of a parquet file.
 *
 * The reader is self contained: the thrift compact protocol decoder, the
 * parquet metadata model, the page decoder and the snappy decompressor are
 * implemented in ob_parquet_reader.cpp, no third party library is needed.
 *
 * The reader never materializes the whole file, it decodes one page at a time
 * and produces one row per next_row() call. Only the projected columns are
 * decoded, the columns are matched with the leaves of the parquet schema by
 * name.
 *
 * The supported subset of the parquet format is:
 * - a flat schema, a nested schema is rejected,
 * - several row groups,
 * - the dictionary, the data page v1 and the data page v2 page types,
 * - the plain, the plain dictionary and the rle dictionary encodings,
 * - the uncompressed and the snappy codecs.
 */
class ObParquetFileScanReader : public ObFileScanReader
{
public:
  /**
   * @brief Create the reader of a parquet file.
   *
   * The file is not opened here, open() does the file work and close()
   * releases it.
   *
   * @param options  the reader options, the path must be set
   * @param columns  the projected columns, the reader produces one value per column
   */
  ObParquetFileScanReader(const ObFileScanOptions &options,
                          const common::ObIArray<ObFileColumnDef> &columns);
  virtual ~ObParquetFileScanReader();
  /**
   * @brief Open the file, read its footer and prepare the streaming.
   */
  virtual int open() override;
  /**
   * @brief Produce the next row into the row values.
   *
   * @return OB_SUCCESS when a row was produced, OB_ITER_END at the end of the file
   */
  virtual int next_row() override;
  /**
   * @brief Release the file resources.
   */
  virtual int close() override;
  /**
   * @brief Read the footer of the file and infer the columns of its schema.
   *
   * One column is produced per leaf of the parquet schema, in the order of the
   * file. The name of the column is deep copied into the given allocator.
   *
   * @param allocator the allocator used for the column names
   * @param columns   the inferred columns, on success
   */
  virtual int infer_schema(common::ObIAllocator &allocator,
                            common::ObIArray<ObFileColumnDef> &columns) override;

private:
  class Impl;
  Impl *impl_;
  DISALLOW_COPY_AND_ASSIGN(ObParquetFileScanReader);
};

} // end namespace sql
} // end namespace oceanbase
