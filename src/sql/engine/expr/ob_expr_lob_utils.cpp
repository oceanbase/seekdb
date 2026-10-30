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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

int ObTextStringHelper::build_text_iter(
    ObTextStringIter &text_iter,
    ObExecContext &exec_ctx,
    ObIAllocator *res_allocator,
    ObIAllocator *tmp_allocator)
{
  int ret = OB_SUCCESS;
  const common::ObLobReadOptions *options = nullptr;
  if (OB_FAIL(exec_ctx.get_lob_read_options(options))) {
  } else {
    ret = build_text_iter(
        text_iter, *options, res_allocator, tmp_allocator);
  }
  return ret;
}

int ObTextStringHelper::read_real_string_data(
    const common::ObLobReadOptions &options,
    ObIAllocator *allocator,
    SqlValueType type,
    ObCollationType cs_type,
    bool has_lob_header,
    ObString &str)
{
  return common::lob_helper::read_real_string_data(
      allocator, type, cs_type, has_lob_header, str, &options);
}

int ObTextStringHelper::read_real_string_data(
    ObExecContext &exec_ctx,
    ObIAllocator *allocator,
    SqlValueType type,
    ObCollationType cs_type,
    bool has_lob_header,
    ObString &str)
{
  int ret = OB_SUCCESS;
  const common::ObLobReadOptions *options = nullptr;
  if (OB_FAIL(exec_ctx.get_lob_read_options(options))) {
  } else {
    ret = read_real_string_data(
        *options, allocator, type, cs_type, has_lob_header, str);
  }
  return ret;
}

int ObTextStringHelper::read_real_string_data(
    ObExecContext &exec_ctx,
    ObIAllocator *allocator,
    const common::DatumParameter &value,
    ObString &str)
{
  int ret = OB_SUCCESS;
  str = value.get_string();
  if (value.is_null()) {
    str.reset();
  } else if (!value.get_meta().is_lob_storage() || !value.has_lob_header()) {
    // Plain character data and headerless LOBs already expose their bytes.
  } else if (value.datum_.len_ != 0 && !value.datum_.get_lob_data().is_mem_loc_
             && value.datum_.get_lob_data().in_row_) {
    const ObLobCommon &lob = value.datum_.get_lob_data();
    str.assign_ptr(lob.get_inrow_data_ptr(), static_cast<int32_t>(lob.get_byte_size(value.datum_.len_)));
  } else {
    ret = read_real_string_data(exec_ctx, allocator, value.get_type(),
                               value.get_collation_type(), value.has_lob_header(), str);
  }
  return ret;
}

int ObTextStringHelper::build_text_iter(
    ObTextStringIter &text_iter,
    const common::ObLobReadOptions &options,
    ObIAllocator *res_allocator,
    ObIAllocator *tmp_allocator)
{
  int ret = text_iter.init(
      0 /*buffer_len*/, &options, res_allocator, tmp_allocator);
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int ob_adjust_lob_datum(ObExecContext &exec_ctx,
                        const ObDatum &origin_datum,
                        const common::SqlTypeMeta &origin_meta,
                        const common::SqlTypeMeta &result_meta,
                        ObIAllocator &allocator,
                        ObDatum &out_datum)
{
  int ret = OB_SUCCESS;
  if (origin_datum.is_null() || origin_datum.is_nop() || !is_lob_storage(origin_meta.get_type())) {
  } else if (origin_meta.has_lob_header() != result_meta.has_lob_header()) {
    if (!origin_meta.has_lob_header()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObString full_data = origin_datum.get_string();
      ObLobLocatorV2 locator(full_data, true);
      OB_ASSERT(!locator.is_persist_lob());
      const ObLobCommon *lob_common = reinterpret_cast<const ObLobCommon *>(full_data.ptr());
      if (full_data.length() >= sizeof(ObLobCommon) && !lob_common->is_mem_loc_ && lob_common->in_row_) {
        // Preserve the mapped-cell fast path: ordinary in-row locators need
        // no request LOB service or allocator just to remove their header.
        full_data.assign_ptr(lob_common->get_inrow_data_ptr(),
            static_cast<int32_t>(lob_common->get_byte_size(full_data.length())));
      } else {
        ret = ObTextStringHelper::read_real_string_data(exec_ctx, &allocator,
            origin_meta.get_type(), origin_meta.get_collation_type(), true, full_data);
      }
      if (OB_SUCC(ret)) { out_datum.set_string(full_data); }
    }
  }
  return ret;
}

/// only called in ObExprValuesOp::calc_next_row
int ob_adjust_lob_datum(ObExecContext &exec_ctx,
                        ObDatum &datum,
                        const common::SqlTypeMeta &in_obj_meta,
                        const common::SqlTypeMeta &out_obj_meta,
                        ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (!is_lob_storage(in_obj_meta.get_type())) { // null & nop is not lob
  } else if (in_obj_meta.get_type_class() != out_obj_meta.get_type_class()) {
    // lob casted to other type do nothing
  } else if (in_obj_meta.has_lob_header() != out_obj_meta.has_lob_header()) {
    if (in_obj_meta.has_lob_header()) { // obj_meta does not have lob header, get data only
      // The mapped-cell path strips an ordinary in-row header directly,
      // before requesting runtime LOB services for other locator forms.
      ret = ob_adjust_lob_datum(exec_ctx, static_cast<const ObDatum &>(datum),
          in_obj_meta, out_obj_meta, allocator, datum);
    } else { // origin obj does not have lob header, but meta has, build temp lob header
      if (OB_FAIL(ObTextStringResult::ob_convert_datum_temporay_lob(datum,
                                                                    in_obj_meta,
                                                                    out_obj_meta,
                                                                    allocator))) {
      }
    }
  }
  return ret;
}

int ob_adjust_in_memory_lob_datum(const DatumValue &source,
                                  const common::SqlTypeMeta &result_meta,
                                  ObIAllocator &allocator,
                                  ObDatum *out_datum)
{
  int ret = OB_SUCCESS;
  if (out_datum == nullptr) { return OB_INVALID_ARGUMENT; }
  if (source.is_null() || !is_lob_storage(source.get_type())
      || source.has_lob_header() == result_meta.has_lob_header()) {
    return OB_SUCCESS;
  }
  if (source.has_lob_header()) {
    ObLobLocatorV2 locator(source.get_string(), true);
    ObString data;
    if (!locator.is_valid() || locator.is_persist_lob() || !locator.has_inrow_data()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(locator.get_inrow_data(data))) {
    } else { out_datum->set_string(data); }
  } else {
    ret = ObTextStringResult::ob_convert_datum_temporay_lob(
        *out_datum, source.meta_, result_meta, allocator);
  }
  return ret;
}

}
}
