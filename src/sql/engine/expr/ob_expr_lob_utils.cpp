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
#include "share/rc/ob_module_provider.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{

namespace common
{
int ob_obj_read_lob_data(
    ObIAllocator &allocator,
    const common::ObObj &obj,
    ObString &data)
{
  int ret = OB_SUCCESS;
  if (share::g_mp->lob_manager() == nullptr) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("lob manager is null", K(ret), K(obj), K(lbt()));
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(&allocator, obj, data, nullptr))) {
    LOG_WARN("read_real_string_data fail", K(ret), K(obj), K(lbt()));
  }
  return ret;
}
}

namespace sql
{



int ObTextStringHelper::build_text_iter(
    ObTextStringIter &text_iter,
    ObExecContext *exec_ctx,
    const sql::ObBasicSessionInfo *session,
    ObIAllocator *res_allocator,
    ObIAllocator *tmp_allocator)
{
  int ret = OB_SUCCESS;
  ObLobAccessCtx *lob_access_ctx = nullptr;
  if (OB_NOT_NULL(exec_ctx) && OB_FAIL(exec_ctx->get_lob_access_ctx(lob_access_ctx))) {
    LOG_WARN("get_lob_access_ctx fail", K(ret));
  } else if (OB_FAIL(text_iter.init(0/*buffer_len*/, session, res_allocator, tmp_allocator, lob_access_ctx))) {
    LOG_WARN("init lob str iter fail", K(ret), K(text_iter));
  }
  return ret;
}

int ObTextStringHelper::read_real_string_data(
    ObIAllocator *allocator,
    ObObjType type,
    ObCollationType cs_type,
    bool has_lob_header,
    ObString &str,
    sql::ObExecContext *exec_ctx)
{
  UNUSED(exec_ctx);
  return common::lob_helper::read_real_string_data(allocator, type, cs_type, has_lob_header, str);
}

int ObTextStringHelper::read_real_string_data(
    ObIAllocator *allocator,
    const common::ObObj &obj,
    ObString &str,
    sql::ObExecContext *exec_ctx)
{
  int ret = OB_SUCCESS;
  const ObObjMeta& meta = obj.get_meta();
  str = obj.get_string();
  if (meta.is_null()) {
    str.reset();
  } else if (! obj.is_lob_storage()) {
  } else if (obj.has_lob_header() && obj.get_string_len() != 0 &&
      ! obj.get_lob_value()->is_mem_loc_ && obj.get_lob_value()->in_row_) {
    const ObLobCommon* lob = obj.get_lob_value();
    str.assign_ptr(lob->get_inrow_data_ptr(), static_cast<int32_t>(lob->get_byte_size(obj.get_string_len())));
  } else if (OB_FAIL(read_real_string_data(
      allocator,
      meta.get_type(),
      meta.get_collation_type(),
      obj.has_lob_header(),
      str,
      exec_ctx))) {
    COMMON_LOG(WARN, "read_real_string_data fail", K(ret));
  }
  return ret;
}

int ob_adjust_lob_datum(const ObObj &origin_obj,
                        const common::ObObjMeta &obj_meta,
                        const ObObjDatumMapType &obj_datum_map_,
                        ObIAllocator &allocator,
                        ObDatum &out_datum)
{
  int ret = OB_SUCCESS;
  if (!is_lob_storage(origin_obj.get_type())) { // null & nop is not lob
  } else if (origin_obj.has_lob_header() != obj_meta.has_lob_header()) {
    if (origin_obj.has_lob_header()) { // obj_meta does not have lob header, get data only
      // can avoid allocator if no persist lobs call this function,
      OB_ASSERT(origin_obj.is_persist_lob() == false);

      ObString full_data;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(&allocator, origin_obj, full_data))) {
        LOG_WARN("Lob: failed to get full data", K(ret));
      } else {
        out_datum.set_string(full_data);
      }
    } else { // origin obj does not have lob header, but meta has, build temp lob header
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpect for input obj, input obj should has lob header", K(ret), K(origin_obj), K(obj_meta));
      // ObObj out_obj(origin_obj);
      // if (OB_FAIL(ObTextStringResult::ob_convert_obj_temporay_lob(out_obj, allocator))) {
      //   LOG_WARN("Lob: failed to convert plain lob data to temp lob", K(ret));
      // } else if (OB_FAIL(out_datum.from_obj(out_obj, obj_datum_map_))) {
      //   LOG_WARN("convert lob obj to datum failed", K(ret), K(out_obj));
      // }
    }
  }
  return ret;
}

int ob_adjust_lob_datum(const ObObj &origin_obj,
                        const common::ObObjMeta &obj_meta,
                        ObIAllocator &allocator,
                        ObDatum &out_datum)
{
  return ob_adjust_lob_datum(origin_obj, obj_meta, allocator, &out_datum);
}

int ob_adjust_lob_datum(const ObObj &origin_obj,
                        const common::ObObjMeta &obj_meta,
                        ObIAllocator &allocator,
                        ObDatum *out_datum)
{
  int ret = OB_SUCCESS;
  if (!is_lob_storage(origin_obj.get_type())) { // null & nop is not lob
  } else if (origin_obj.has_lob_header() != obj_meta.has_lob_header()) {
    if (origin_obj.has_lob_header()) { // obj_meta does not have lob header, get data only
      // can avoid allocator if no persist lobs call this function,
      OB_ASSERT(origin_obj.is_persist_lob() == false);

      ObString full_data;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(&allocator, origin_obj, full_data))) {
        LOG_WARN("Lob: failed to get full data", K(ret));
      } else {
        out_datum->set_string(full_data);
      }
    } else { // origin obj does not have lob header, but meta has, build temp lob header
      // use by not strict default value add lob header
      ObObj out_obj(origin_obj);
      if (OB_FAIL(ObTextStringResult::ob_convert_obj_temporay_lob(out_obj, allocator))) {
        LOG_WARN("Lob: failed to convert plain lob data to temp lob", K(ret));
      } else if (OB_FAIL(out_datum->from_obj(out_obj))) {
        LOG_WARN("convert lob obj to datum failed", K(ret), K(out_obj));
      }
    }
  }
  return ret;
}

/// only called in ObExprValuesOp::calc_next_row
int ob_adjust_lob_datum(ObDatum &datum,
                        const common::ObObjMeta &in_obj_meta,
                        const common::ObObjMeta &out_obj_meta,
                        ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (!is_lob_storage(in_obj_meta.get_type())) { // null & nop is not lob
  } else if (in_obj_meta.get_type_class() != out_obj_meta.get_type_class()) {
    // lob casted to other type do nothing
  } else if (in_obj_meta.has_lob_header() != out_obj_meta.has_lob_header()) {
    if (in_obj_meta.has_lob_header()) { // obj_meta does not have lob header, get data only
      // can avoid allocator if no persist lobs call this function ?
      ObString full_data = datum.get_string();
      ObLobLocatorV2 lob(full_data, in_obj_meta.has_lob_header());
      OB_ASSERT(lob.is_persist_lob() == false);
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(&allocator,
                                                            in_obj_meta.get_type(),
                                                            in_obj_meta.get_collation_type(),
                                                            in_obj_meta.has_lob_header(),
                                                            full_data))) {
        LOG_WARN("Lob: failed to get full data", K(ret));
      } else {
        datum.set_string(full_data);
      }
    } else { // origin obj does not have lob header, but meta has, build temp lob header
      if (OB_FAIL(ObTextStringResult::ob_convert_datum_temporay_lob(datum,
                                                                    in_obj_meta,
                                                                    out_obj_meta,
                                                                    allocator))) {
        LOG_WARN("Lob: failed to convert plain lob data to temp lob", K(ret));
      }
    }
  }
  return ret;
}

}
}
