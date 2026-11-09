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
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"
#include "sql/resolver/dml/ob_file_table_def.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief Helpers shared by the file table functions: schema inference, type
 *        inference and file change detection.
 */
class ObFileScanUtils
{
public:
  /**
   * @brief Infer the columns of a file from its content.
   *
   * @param options   the reader options, the path must be set
   * @param allocator the allocator used for the column names
   * @param columns   the inferred columns, on success
   * @param row_cnt   the number of the rows of the file, it may be NULL
   */
  static int infer_schema(const ObFileScanOptions &options,
                          common::ObIAllocator &allocator,
                          common::ObIArray<ObFileColumnDef> &columns,
                          int64_t *row_cnt = NULL);

  /**
   * @brief Infer the type of one scalar text value.
   *
   * @param data    the text of the value
   * @param len     the length of the text
   * @param is_null set to true when the text denotes a NULL value
   * @return the inferred type, common::ObNullType for NULL
   */
  static common::ObObjType infer_scalar_type(const char *data, const int64_t len, bool &is_null);

  /**
   * @brief Widen two types into the type able to hold both of them.
   */
  static common::ObObjType merge_type(const common::ObObjType left, const common::ObObjType right);

  /**
   * @brief Whether the text is a valid date, i.e. "YYYY-MM-DD".
   */
  static bool is_valid_date(const char *data, const int64_t len);

  /**
   * @brief Whether the text is a valid datetime, i.e. "YYYY-MM-DD[ T]HH:MM:SS[.fff]".
   */
  static bool is_valid_datetime(const char *data, const int64_t len);

  /**
   * @brief The number of the fractional digits of a datetime text.
   *
   * The scale of a datetime column is the greatest number of the fractional
   * digits of its values, a datetime without a fraction has the scale 0.
   */
  static int64_t datetime_scale(const char *data, const int64_t len);

  /**
   * @brief Read the size and the modification time of a file.
   */
  static int stat_file(const common::ObString &path, int64_t &file_size, int64_t &file_mtime);

  /**
   * @brief Whether the file changed since its size and mtime were recorded.
   */
  static bool is_file_changed(const common::ObString &path, const int64_t file_size, const int64_t file_mtime);

  /**
   * @brief Store a text value into an object of the given type.
   *
   * @param obj  the object to fill
   * @param type the target type
   * @param data the text of the value
   * @param len  the length of the text
   */
  static int set_obj_from_text(common::ObObj &obj,
                               const common::ObObjType type,
                               const char *data,
                               const int64_t len);

  /**
   * @brief The SQL type name of an inferred type, as used by DESCRIBE.
   */
  static const char *type_name(const common::ObObjType type);

  /**
   * @brief Whether the extension of the file is the one of the format that reads it.
   *
   * The csv reader reads the files whose extension is "csv", the json reader
   * reads the files whose extension is "json", "jsonl" or "ndjson" and the
   * parquet reader reads the files whose extension is "parquet". The glob
   * function does not read the files, it discovers them, so it accepts every
   * path.
   */
  static bool is_supported_extension(const ObFileScanFormat format, const common::ObString &path);

  /**
   * @brief Make the names of the columns unique.
   *
   * A duplicated name is suffixed with "__2", "__3" and so on, the way DuckDB
   * names the duplicated columns of a CSV file.
   */
  static int dedup_column_names(common::ObIAllocator &allocator,
                                common::ObIArray<ObFileColumnDef> &columns);
};

/**
 * @brief A reader that streams the rows of a file.
 *
 * The reader never materializes the whole file, it produces one row at a time.
 */
class ObFileScanReader
{
public:
  ObFileScanReader() : row_cnt_(0) {}
  virtual ~ObFileScanReader() {}
  /**
   * @brief Open the file and prepare for streaming.
   */
  virtual int open() = 0;
  /**
   * @brief Produce the next row into the row values.
   *
   * @return OB_SUCCESS when a row was produced, OB_ITER_END at the end of the file
   */
  virtual int next_row() = 0;
  /**
   * @brief Release the file resources.
   */
  virtual int close() = 0;
  /**
   * @brief The values of the row produced by the last next_row() call.
   */
  const common::ObIArray<common::ObObj> &get_row_values() const { return row_values_; }
  /**
   * @brief Scan the file and infer the schema of its columns.
   *
   * The base implementation does not support the inference, the readers of the
   * supported formats override it.
   *
   * @param allocator the allocator used for the column names
   * @param columns   the inferred columns, on success
   */
  virtual int infer_schema(common::ObIAllocator &allocator,
                            common::ObIArray<ObFileColumnDef> &columns);
  /**
   * @brief The number of the rows that were counted when the schema of the
   *        file was inferred.
   */
  int64_t get_row_cnt() const { return row_cnt_; }

protected:
  // ObIArray is an abstract interface, a concrete array is needed to hold the
  // values of the row produced by the reader
  common::ObArray<common::ObObj> row_values_;
  // the number of the rows that were counted during the inference
  int64_t row_cnt_;
};

/**
 * @brief Create the reader of a file table.
 *
 * @param options  the reader options
 * @param columns  the columns of the file table, the reader produces one value per column
 * @param allocator the allocator of the reader
 * @param reader   the created reader, the caller destroys it with destroy_file_scan_reader()
 */
int create_file_scan_reader(const ObFileScanOptions &options,
                            const common::ObIArray<ObFileColumnDef> &columns,
                            common::ObIAllocator &allocator,
                            ObFileScanReader *&reader);

/**
 * @brief Destroy a reader created by create_file_scan_reader().
 */
void destroy_file_scan_reader(ObFileScanReader *reader);

} // end namespace sql
} // end namespace oceanbase
