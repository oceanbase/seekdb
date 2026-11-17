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

// The seekdb side of the file_sql framework. It converts the values of a
// format module into the columns of a table item and into the datums of a
// scan operator. The format modules themselves stay free of seekdb headers so
// that they can be tested on their own.

#include "sql/engine/basic/file_sql/file_sql_types.h"

#include "oblib/common/datum/ob_datum.h"
#include "oblib/common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

// The longest text a varchar column of a file table can hold.
const int64_t FILE_SQL_MAX_VARCHAR_LENGTH = 65535;

// Build the seekdb column type of an inferred column.
int build_column_data_type(ScalarType type, ObDataType &data_type);

// Write one scanned value into the datum of a column. The text of a string
// column is copied into `allocator`, the caller keeps the allocator alive
// until the batch has been consumed. A NULL value produces a NULL datum.
int set_datum_from_scalar(ObDatum &datum,
                          const ObDataType &data_type,
                          const ScalarValue &value,
                          ObIAllocator &allocator);

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
