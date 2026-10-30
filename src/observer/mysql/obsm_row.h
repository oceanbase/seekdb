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

#include "common/timezone/ob_time_convert.h"
#include "rpc/obmysql/ob_mysql_row.h"
#include "common/row/ob_row.h"
#include "common/ob_field.h"

namespace oceanbase
{
namespace sql
{
class ResultRowView;
struct ObExpr;
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

class ObSMRow final
    : public obmysql::ObMySQLRow
{
public:
  ObSMRow(obmysql::MYSQL_PROTOCOL_TYPE type,
          const ObNewRow &obrow,
          const ObDataTypeCastParams &dtc_params,
          const sql::ObSQLSessionInfo &session,
          const ColumnsFieldIArray *fields = NULL,
          share::schema::ObSchemaGetterGuard *schema_guard = NULL);

  virtual ~ObSMRow() {}

  int build_cell_value(int64_t idx, ObIAllocator &scratch_allocator,
                       obmysql::ObMySQLCellValue &out) const override;
  int get_packed_row_blob(const char *&data, int64_t &len) const override;

protected:
  int64_t get_cells_cnt() const override
  {
    return NULL == obrow_.projector_
        ? obrow_.count_
        : obrow_.projector_size_;
  }

private:
  const ObNewRow &obrow_;
  const ObDataTypeCastParams &dtc_params_;
  const sql::ObSQLSessionInfo &session_;
  const ColumnsFieldIArray *fields_;
  share::schema::ObSchemaGetterGuard *schema_guard_;

  DISALLOW_COPY_AND_ASSIGN(ObSMRow);
}; // end of class OBMP

// Protocol adapter for borrowed native execution values. Converted columns
// contain DatumValue views; other values remain in their expression frame.
class DatumMysqlRow final : public obmysql::ObMySQLRow
{
public:
  DatumMysqlRow(obmysql::MYSQL_PROTOCOL_TYPE type, const sql::ResultRowView &row,
                const ObIArray<int64_t> &obj_indexes, const ObIArray<DatumValue> &objects,
                const ObDataTypeCastParams &dtc_params,
                const sql::ObSQLSessionInfo &session, const ColumnsFieldIArray &fields,
                ObCharsetType result_charset,
                share::schema::ObSchemaGetterGuard *schema_guard)
      : ObMySQLRow(type), row_(row), obj_indexes_(obj_indexes.empty() ? nullptr : &obj_indexes.at(0)),
        objects_(objects.empty() ? nullptr : &objects.at(0)),
        dtc_params_(dtc_params), session_(session), fields_(fields),
        result_charset_(result_charset), schema_guard_(schema_guard) {}
  static bool can_encode(const sql::ObExpr &expr, const ObField &field,
                          ObCharsetType result_charset, bool need_ps_cast,
                          bool &convert_charset);
  int build_cell_value(int64_t idx, ObIAllocator &scratch_allocator,
                       obmysql::ObMySQLCellValue &out) const override;
  int get_packed_row_blob(const char *&data, int64_t &len) const override
  { data = nullptr; len = 0; return OB_STATE_NOT_MATCH; }
protected:
  int64_t get_cells_cnt() const override;
private:
  const sql::ResultRowView &row_;
  const int64_t *obj_indexes_;
  const DatumValue *objects_;
  const ObDataTypeCastParams &dtc_params_;
  const sql::ObSQLSessionInfo &session_;
  const ColumnsFieldIArray &fields_;
  ObCharsetType result_charset_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
};

} // end of namespace common
} // end of namespace oceanbase

#endif /* _OCEABASE_COMMON_OBSM_ROW_H_ */
