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

#ifndef _OBSM_UTILS_H_
#define _OBSM_UTILS_H_

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
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace common
{
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
  static const char* get_extend_type_name(int type);
};

} // end of namespace common
} // end of namespace oceanbase

#endif /* _OBSM_UTILS_H_ */
