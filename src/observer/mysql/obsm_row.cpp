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

#include "query/protocol/ob_mysql_protocol_util.h"
#include "sql/executor/ob_execute_result.h"

using namespace oceanbase::share::schema;
using namespace oceanbase::common;
using namespace oceanbase::obmysql;

bool DatumMysqlRow::can_encode(const oceanbase::sql::ObExpr &expr, const ObField &field,
                               ObCharsetType result_charset, bool need_ps_cast, bool &convert_charset)
{
  convert_charset = false;
  const SqlTypeMeta &meta = expr.obj_meta_;
  bool direct = !need_ps_cast || meta.get_type() == field.type_meta_.get_type();
  if (direct && meta.is_integer_type()) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_8BYTE_DATA;
  } else if (direct && meta.get_type_class() == ObNumberTC) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_NUMBER;
  } else if (direct && meta.is_decimal_int()) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_DECIMALINT && meta.get_scale() >= 0;
  } else if (direct && (meta.get_type_class() == ObDateTimeTC
                        || meta.get_type() == ObMySQLDateTimeType)) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_8BYTE_DATA;
  } else if (direct && (meta.get_type() == ObVarcharType || meta.get_type() == ObCharType
                        || meta.get_type() == ObHexStringType)) {
    const ObCollationType from = meta.get_collation_type();
    const ObCollationType to = ObCharset::get_default_collation(result_charset);
    direct = expr.obj_datum_map_ == OBJ_DATUM_STRING && CS_TYPE_INVALID != from;
    // Charset conversion works on borrowed Datum bytes. Validate the same
    // charset descriptions as the old result adapter before taking this path.
    if (direct && (from == to || (ObCharset::is_valid_charset(result_charset)
                                   && CHARSET_BINARY != result_charset))) {
      const ObCharsetInfo *from_info = ObCharset::get_charset(from);
      const ObCharsetInfo *to_info = ObCharset::get_charset(to);
      direct = nullptr != from_info && nullptr != to_info && CS_TYPE_INVALID != to;
      convert_charset = direct && CS_TYPE_BINARY != from && CS_TYPE_BINARY != to
                        && 0 != strcmp(from_info->csname, to_info->csname);
    }
  } else if (direct && meta.is_null()) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_NULL;
  } else if (direct && (meta.get_type_class() == ObFloatTC || meta.is_date()
                        || meta.get_type() == ObMySQLDateType)) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_4BYTE_DATA;
  } else if (direct && meta.is_year()) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_1BYTE_DATA;
  } else if (direct && (meta.get_type_class() == ObDoubleTC || meta.is_time() || meta.is_bit())) {
    direct = expr.obj_datum_map_ == OBJ_DATUM_8BYTE_DATA;
  } else {
    direct = false;
  }
  return direct;
}

int64_t DatumMysqlRow::get_cells_cnt() const { return row_.count(); }

int DatumMysqlRow::build_cell_value(int64_t idx, ObIAllocator &scratch_allocator,
                                    ObMySQLCellValue &out) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= row_.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (-2 == obj_indexes_[idx]) {
    ret = ObSMUtils::build_converted_string_cell_value(
        row_.datum(idx), row_.expr(idx).obj_meta_, result_charset_, scratch_allocator, out);
  } else if (obj_indexes_[idx] < 0) {
    ret = ObSMUtils::build_datum_cell_value(row_.datum(idx), row_.expr(idx).obj_meta_,
                                           type_, scratch_allocator, out, &fields_.at(idx), dtc_params_);
  } else {
    ret = ObSMUtils::build_cell_value(objects_[obj_indexes_[idx]], type_,
                                     scratch_allocator, out, dtc_params_, &fields_.at(idx),
                                     session_, schema_guard_);
  }
  return ret;
}

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
    const DatumValue &cell = obrow_.cells_[cell_idx];
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
    const DatumValue &cell = obrow_.cells_[cell_idx];
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
