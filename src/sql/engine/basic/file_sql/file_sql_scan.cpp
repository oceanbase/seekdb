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
#include "file_sql_scan.h"

#include "oblib/common/timezone/ob_time_convert.h"
#include "share/ob_time_utility2.h"

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

int build_column_data_type(ScalarType type, ObDataType &data_type)
{
  int ret = OB_SUCCESS;
  data_type.reset();
  data_type.set_charset_type(CHARSET_UTF8MB4);
  data_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  data_type.set_collation_level(CS_LEVEL_IMPLICIT);
  switch (type) {
  case ScalarType::BIGINT:
    data_type.set_obj_type(ObIntType);
    data_type.set_precision(10);
    data_type.set_scale(0);
    break;
  case ScalarType::DOUBLE:
    data_type.set_obj_type(ObDoubleType);
    break;
  case ScalarType::BOOLEAN:
    // The value of a boolean column is 0 or 1, the type name that describe
    // shows comes from the inferred schema and not from this column type.
    data_type.set_obj_type(ObTinyIntType);
    data_type.set_precision(1);
    data_type.set_scale(0);
    break;
  case ScalarType::VARCHAR:
    data_type.set_obj_type(ObVarcharType);
    data_type.set_length(FILE_SQL_MAX_VARCHAR_LENGTH);
    data_type.set_length_semantics(LS_BYTE);
    break;
  case ScalarType::DATE:
    data_type.set_obj_type(ObDateType);
    break;
  case ScalarType::DATETIME:
    data_type.set_obj_type(ObDateTimeType);
    data_type.set_scale(0);
    break;
  case ScalarType::NULL_VALUE:
    // A column that is NULL in every row keeps the null type of the column
    // but has to be able to hold a value once the file changes, a varchar
    // column is used for that.
    data_type.set_obj_type(ObVarcharType);
    data_type.set_length(FILE_SQL_MAX_VARCHAR_LENGTH);
    data_type.set_length_semantics(LS_BYTE);
    break;
  default:
    ret = OB_ERR_UNEXPECTED;
    break;
  }
  return ret;
}

int set_datum_from_scalar(ObDatum &datum,
                          const ObDataType &data_type,
                          const ScalarValue &value,
                          ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (value.is_null) {
    datum.set_null();
  } else {
    switch (data_type.get_obj_type()) {
    case ObIntType:
    case ObTinyIntType:
      datum.set_int(value.integer);
      break;
    case ObDoubleType:
      datum.set_double(value.real);
      break;
    case ObDateType: {
      int32_t days = 0;
      if (OB_FAIL(ObTimeConverter::str_to_date(ObString(value.text.length(), value.text.c_str()), days))) {
        ret = OB_INVALID_DATA;
      } else {
        datum.set_int32(days);
      }
      break;
    }
    case ObDateTimeType: {
      int64_t usec = 0;
      ObTimeConvertCtx ctx(nullptr, false);
      if (OB_FAIL(ObTimeConverter::str_to_datetime(ObString(value.text.length(), value.text.c_str()),
                                                   ctx,
                                                   usec))) {
        ret = OB_INVALID_DATA;
      } else {
        datum.set_int(usec);
      }
      break;
    }
    default: {
      // The text of a string column is copied, the value of the scanner is
      // only valid until the next batch.
      char *buffer = static_cast<char *>(allocator.alloc(value.text.length() + 1));
      if (OB_ISNULL(buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (!value.text.empty()) {
          MEMCPY(buffer, value.text.c_str(), value.text.length());
        }
        datum.set_string(buffer, static_cast<uint32_t>(value.text.length()));
      }
      break;
    }
    }
  }
  return ret;
}

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
