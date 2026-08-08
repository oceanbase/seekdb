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
#include "common/object/ob_object.h"
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
class ObSMUtils {
public:
  static int
  build_cell_value(const ObObj &obj, obmysql::MYSQL_PROTOCOL_TYPE type,
                   ObIAllocator &scratch_allocator,
                   obmysql::ObMySQLCellValue &out,
                   const ObDataTypeCastParams &dtc_params, const ObField *field,
                   const sql::ObSQLSessionInfo &session,
                   share::schema::ObSchemaGetterGuard *schema_guard = NULL);

  static bool update_from_bitmap(ObObj &param, const char *bitmap, int64_t field_index);

  static bool update_from_bitmap(const char *bitmap, int64_t field_index);

  static int get_type_length(ObObjType ob_type, int64_t &length);

  static int get_mysql_type(ObObjType ob_type, obmysql::EMySQLFieldType &mysql_type,
                            uint16_t &flags, ObScale &num_decimals);

  static int get_ob_type(ObObjType &ob_type, obmysql::EMySQLFieldType mysql_type,
                         const bool is_unsigned = false);
};

} // end of namespace common

namespace query
{

// Decode the scalar subset of a COM_STMT_EXECUTE value. The server protocol
// adapter supplies the implementation; PL consumes this lower-layer seam
// without including Observer's request-handler class.
int decode_mysql_basic_param_value(
    common::ObIAllocator &allocator,
    uint32_t type,
    common::ObCharsetType charset,
    common::ObCharsetType ncharset,
    common::ObCollationType collation,
    const char *&data,
    const common::ObTimeZoneInfo *time_zone,
    common::ObObj &value,
    bool is_complex_element = false,
    bool is_unsigned = false);

} // end of namespace query
} // end of namespace oceanbase

#endif // OCEANBASE_QUERY_PROTOCOL_OB_MYSQL_PROTOCOL_UTIL_H_
