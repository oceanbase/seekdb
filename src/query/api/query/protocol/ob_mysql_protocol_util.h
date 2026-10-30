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

#ifndef OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_PROTOCOL_UTIL_H_
#define OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_PROTOCOL_UTIL_H_

#include "common/mysqlclient/ob_mysql_global.h"
#include "common/ob_accuracy.h"
#include "common/datum/datum_param_store.h"
#include "common/object/ob_type_metadata.h"
#include "common/timezone/ob_timezone_info.h"
#include "lib/string/ob_string.h"
#include "rpc/obmysql/ob_mysql_row.h"
#include "rpc/obmysql/ob_mysql_util.h"
#include <inttypes.h>
#include <stdint.h>

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;

int store_params_value_to_str(common::ObIAllocator &allocator,
                              ObSQLSessionInfo &session,
                              common::ParamStore *params,
                              char *&params_value,
                              int64_t &params_value_len);
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace common
{
class ObDataTypeCastParams;
class ObField;
class ObIAllocator;
struct ObDatum;
struct DatumValue;
class ObSMUtils {
public:
  // Scalar numeric, temporal, BIT, NULL and CHAR/VARCHAR/hex values.
  // The caller retains the Datum payload through synchronous encoding and
  // handles casts; string conversion uses the packet scratch allocator.
  static int build_datum_cell_value(const ObDatum &datum, const SqlTypeMeta &meta,
                   obmysql::MYSQL_PROTOCOL_TYPE type, ObIAllocator &scratch_allocator,
                   obmysql::ObMySQLCellValue &out, const ObField *field,
                   const ObDataTypeCastParams &dtc_params);
  static int build_converted_string_cell_value(const ObDatum &datum, const SqlTypeMeta &meta,
                   ObCharsetType result_charset, ObIAllocator &scratch_allocator,
                   obmysql::ObMySQLCellValue &out);
  static int build_cell_value(const DatumValue &value, obmysql::MYSQL_PROTOCOL_TYPE type,
                   ObIAllocator &scratch_allocator, obmysql::ObMySQLCellValue &out,
                   const ObDataTypeCastParams &dtc_params, const ObField *field,
                   const sql::ObSQLSessionInfo &session,
                   share::schema::ObSchemaGetterGuard *schema_guard = nullptr);

  static bool update_from_bitmap(const char *bitmap, int64_t field_index);

  static int get_type_length(SqlValueType ob_type, int64_t &length);

  static int get_mysql_type(SqlValueType ob_type, obmysql::EMySQLFieldType &mysql_type,
                            uint16_t &flags, ObScale &num_decimals);

  static int get_ob_type(SqlValueType &ob_type, obmysql::EMySQLFieldType mysql_type,
                         const bool is_unsigned = false);
};

} // end of namespace common

namespace query
{

} // end of namespace query
} // end of namespace oceanbase

#endif // OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_PROTOCOL_UTIL_H_
