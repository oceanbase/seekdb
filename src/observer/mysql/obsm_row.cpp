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

#include "obsm_row.h"

#include "observer/mysql/obsm_utils.h"

using namespace oceanbase::share::schema;
using namespace oceanbase::common;
using namespace oceanbase::obmysql;

ObSMRow::ObSMRow(MYSQL_PROTOCOL_TYPE type,
                 const ObNewRow &obrow,
                 const ObDataTypeCastParams &dtc_params,
                 const sql::ObSQLSessionInfo &session,
                 const common::ColumnsFieldIArray *fields,
                 ObSchemaGetterGuard *schema_guard)
    : ObMySQLRow(type),
      obrow_(obrow),
      dtc_params_(dtc_params),
      session_(session),
      fields_(fields),
      schema_guard_(schema_guard)
{
}

int ObSMRow::build_cell_value(int64_t idx, ObIAllocator &scratch_allocator,
                              ObMySQLCellValue &out) const {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_packed_)) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(idx < 0 || idx >= get_cells_cnt())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t cell_idx =
        NULL == obrow_.projector_ ? idx : obrow_.projector_[idx];
    const ObObj &cell = obrow_.cells_[cell_idx];
    const ObField *field = NULL == fields_ ? NULL : &fields_->at(idx);
    ret = ObSMUtils::build_cell_value(cell, type_, scratch_allocator, out,
                                      dtc_params_, field, session_,
                                      schema_guard_);
  }
  return ret;
}

int ObSMRow::get_packed_row_blob(const char *&data, int64_t &len) const {
  int ret = OB_SUCCESS;
  data = NULL;
  len = 0;
  if (OB_UNLIKELY(!is_packed_)) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(1 != get_cells_cnt())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(ERROR, "packed row must contain exactly one cell", K(ret),
                K(get_cells_cnt()));
  } else {
    const int64_t cell_idx =
        NULL == obrow_.projector_ ? 0 : obrow_.projector_[0];
    const ObObj &cell = obrow_.cells_[cell_idx];
    len = cell.get_string_len();
    data = cell.get_string_ptr();
    if (OB_UNLIKELY(len < 0 || (len > 0 && NULL == data))) {
      ret = OB_ERR_UNEXPECTED;
      data = NULL;
      len = 0;
      SQL_ENG_LOG(ERROR, "invalid packed row blob", K(ret), K(common::lbt()));
    }
  }
  return ret;
}
