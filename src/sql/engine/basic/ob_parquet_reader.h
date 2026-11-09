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

#include "sql/engine/basic/ob_file_sql_scan.h"

namespace oceanbase
{
namespace sql
{

// Minimal self-contained Apache Parquet reader for the file-as-database
// feature. Supports:
//  - footer schema parsing (thrift compact protocol, fields skipped by id)
//  - multiple row groups, streamed one row group at a time
//  - optional (nullable) columns via definition levels (max def level 1)
//  - physical types BOOLEAN / INT32 / INT64 / FLOAT / DOUBLE / BYTE_ARRAY
//  - encodings PLAIN / PLAIN_DICTIONARY / RLE_DICTIONARY (v1 and v2 pages)
//  - compression UNCOMPRESSED / SNAPPY
//  - column pruning (only requested columns are decoded)

// Infer the column schema from the footer of the first parquet file.
int infer_parquet_schema(common::ObIAllocator &alloc,
                         const common::ObString &path,
                         common::ObIArray<ReadFileColumn> &cols);

// Create a parquet row reader over the given files (same schema assumed).
int create_parquet_row_reader(common::ObIAllocator &alloc,
                              const common::ObIArray<common::ObString> &paths,
                              const common::ObIArray<ReadFileColumn> &cols,
                              const ObTimeZoneInfo *tz_info,
                              ObIFileRowReader *&reader);

}  // end namespace sql
}  // end namespace oceanbase
