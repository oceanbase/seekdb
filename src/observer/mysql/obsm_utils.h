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

#include <inttypes.h>
#include <stdint.h>
#include "lib/string/ob_string.h"
#include "lib/timezone/ob_timezone_info.h"
#include "rpc/obmysql/ob_mysql_global.h"
#include "rpc/obmysql/ob_mysql_util.h"
#include "common/object/ob_object.h"
#include "common/ob_accuracy.h"

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
class ObSMUtils {
public:
  /**
   * Serialize a cell to the position of buf + pos.
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position in the buffer to write
   * @param [in] cell index for binary protocol
   *
   * @return Returns OB_SUCCESS on success, or an oceanbase error code on failure
   */
  static int cell_str(
      char *buf, const int64_t len,
      const ObObj &obj,
      obmysql::MYSQL_PROTOCOL_TYPE type, int64_t &pos,
      int64_t cell_idx, char *bitmap,
      const ObDataTypeCastParams &dtc_params,
      const ObField *field,
      const sql::ObSQLSessionInfo &session,
      share::schema::ObSchemaGetterGuard *schema_guard = NULL,
      uint64_t tenant_id = common::OB_INVALID_ID);

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
