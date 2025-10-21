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

#ifndef _OCEABASE_COMMON_OBSM_ROW_H_
#define _OCEABASE_COMMON_OBSM_ROW_H_

#include "lib/timezone/ob_time_convert.h"
#include "rpc/obmysql/ob_mysql_row.h"
#include "common/row/ob_row.h"
#include "common/ob_field.h"

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

class ObSMRow
    : public obmysql::ObMySQLRow
{
public:
  ObSMRow(obmysql::MYSQL_PROTOCOL_TYPE type,
          const ObNewRow &obrow,
          const ObDataTypeCastParams &dtc_params,
          const sql::ObSQLSessionInfo &session,
          const ColumnsFieldIArray *fields = NULL,
          share::schema::ObSchemaGetterGuard *schema_guard = NULL,
          uint64_t tenant = common::OB_INVALID_ID);

  virtual ~ObSMRow() {}

protected:
  virtual int64_t get_cells_cnt() const
  {
    return NULL == obrow_.projector_
        ? obrow_.count_
        : obrow_.projector_size_;
  }
  virtual int encode_cell(
      int64_t idx, char *buf,
      int64_t len, int64_t &pos, char *bitmap) const;

private:
  const ObNewRow &obrow_;
  const ObDataTypeCastParams dtc_params_;
  const sql::ObSQLSessionInfo &session_;
  const ColumnsFieldIArray *fields_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
  uint64_t tenant_id_;

  DISALLOW_COPY_AND_ASSIGN(ObSMRow);
}; // end of class OBMP

} // end of namespace common
} // end of namespace oceanbase

#endif /* _OCEABASE_COMMON_OBSM_ROW_H_ */
