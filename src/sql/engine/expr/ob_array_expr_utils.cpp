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
#include "common/object/ob_obj_compare.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/ob_lob_access_utils.h"
#include "share/object/ob_array_cast.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_add.h"
#include "sql/engine/expr/ob_expr_minus.h"
#include <map>

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

const char* ObArrayExprUtils::DEFAULT_CAST_TYPE_NAME = "ARRAY(FLOAT)";
const ObString ObArrayExprUtils::DEFAULT_CAST_TYPE_STR = ObString::make_string(DEFAULT_CAST_TYPE_NAME);

int ObArrayExprUtils::get_type_vector(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    ObIAllocator &allocator,
    ObIArrayType *&result,
    bool &is_null)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = NULL;
  if (OB_FAIL(expr.eval(ctx, datum))) {
    LOG_WARN("eval failed", K(ret));
  } else if (OB_UNLIKELY(datum->is_null())) {
    is_null = true;
  } else if (OB_FAIL(get_type_vector(expr, *datum, ctx, allocator, result))) {
    LOG_WARN("failed to get vector", K(ret));
  }
  return ret;
}

// get vector or array(float)
int ObArrayExprUtils::get_type_vector(
    const ObExpr &expr,
    const ObDatum &datum,
    ObEvalCtx &ctx,
    ObIAllocator &allocator,
    ObIArrayType *&result)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue value;
  uint16_t subschema_id = expr.obj_meta_.get_subschema_id();
  if (!expr.obj_meta_.is_collection_sql_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support", K(ret), K(expr.obj_meta_));
  } else if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
    LOG_WARN("failed to get subschema ctx", K(ret));
  } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid subschema type", K(ret), K(value));
  } else {
    ObString blob_data = datum.get_string();
    const ObSqlCollectionInfo *coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_);
    ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, &allocator,
                                                          ObLongTextType,
                                                          CS_TYPE_BINARY,
                                                          true,
                                                          blob_data))) {
      LOG_WARN("fail to get real data.", K(ret), K(blob_data));
    } else if (OB_FAIL(ObArrayTypeObjFactory::construct(allocator, *arr_type, result, true))) {
      LOG_WARN("construct array obj failed", K(ret), K(*coll_info));
    } else if (OB_FAIL(result->init(blob_data))) {
      LOG_WARN("failed to init array", K(ret));
    }
  }
  return ret;
}

int ObArrayExprUtils::vector_datum_add(ObExecContext &exec_ctx,
                                       ObDatum &res,
                                       const ObDatum &data,
                                       ObIAllocator &allocator,
                                       ObDatum *tmp_res,
                                       bool negative)
{
  int ret = OB_SUCCESS;
  ObString blob_res = res.get_string();
  ObString blob_data = data.get_string();
  ObLobLocatorV2 locator(blob_res, true/*has_lob_header*/);
  bool is_outrow = !locator.has_inrow_data();
  if (OB_FAIL(ObTextStringHelper::read_real_string_data(exec_ctx, &allocator,
                                                        ObLongTextType,
                                                        CS_TYPE_BINARY,
                                                        true,
                                                        blob_data))) {
    LOG_WARN("fail to get real data.", K(ret), K(blob_data));
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(exec_ctx, &allocator,
                                                        ObLongTextType,
                                                        CS_TYPE_BINARY,
                                                        true,
                                                        blob_res))) {
    LOG_WARN("fail to get real data.", K(ret), K(blob_data));
  } else {
    int64_t length = blob_data.length() / sizeof(float);
    float *float_data = reinterpret_cast<float *>(blob_data.ptr());
    float *float_res = reinterpret_cast<float *>(blob_res.ptr());
    for (int64_t i = 0; OB_SUCC(ret) && i < length; ++i) {
      negative ? float_res[i] -= float_data[i] : float_res[i] += float_data[i];
      if (std::isinf(float_res[i]) != 0) {
        ret = OB_OPERATE_OVERFLOW;
        SQL_LOG(WARN, "value overflow", K(ret), K(i), K(float_data[i]), K(float_res[i]));
      }
    }
    if (OB_SUCC(ret) && is_outrow) {
      ObString res_str;
      if (OB_FAIL(ObArrayExprUtils::set_array_res(nullptr, blob_res.length(), allocator, res_str, blob_res.ptr()))) {
        SQL_LOG(WARN, "failed to set array res", K(ret));
      } else if (OB_NOT_NULL(tmp_res)) {
        tmp_res->set_string(res_str);
      } else {
        res.set_string(res_str);
      }
    }
  }
  return ret;
}

// cast any array and varchar to array(float)
int ObArrayExprUtils::calc_cast_type(
    const ObExprOperatorType &expr_type,
    ObExprResType &type,
    common::ObExprTypeCtx &type_ctx,
    const bool only_vector)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session());
  ObExecContext *exec_ctx = OB_ISNULL(session) ? NULL : session->get_cur_exec_ctx();
  uint16_t dst_subschema_id = 0;
  bool need_cast = false;
  if (!type.is_collection_sql_type() && !type.is_string_type() && !type.is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(type));
  } else if (type.is_collection_sql_type()) {
    ObSubSchemaValue value;
    uint16_t src_subschema_id = type.get_subschema_id();
    if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(src_subschema_id, value))) {
      LOG_WARN("failed to get subschema ctx", K(ret));
    } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid subschema type", K(ret), K(value));
    } else {
      const ObSqlCollectionInfo *coll_info = NULL;
      coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_);
      if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_VECTOR_TYPE) {
        // do nothing   
      } else if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_ARRAY_TYPE) {
        ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
        if (only_vector) {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
          LOG_WARN("only support vector type", K(ret));
        } else if (arr_type->element_type_->type_id_ != ObNestedType::OB_BASIC_TYPE) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("nested array is not support", K(ret));
        } else {
          ObCollectionBasicType *elem_type = static_cast<ObCollectionBasicType *>(arr_type->element_type_);
          if (ObFloatType != elem_type->basic_meta_.get_obj_type()) {
            need_cast = true;
          }
        }
      } else if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_SPARSE_VECTOR_TYPE) {
        if (!is_sparse_vector_supported(expr_type)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(type));
        }    
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret), K(type));
      }
      // vector and array(float) don't need to cast
      if (OB_SUCC(ret) && !need_cast) {
        type.set_calc_type(ObCollectionSQLType);
        type.set_calc_subschema_id(src_subschema_id); // avoid cast by set the same subschema_id
      }
    }
  } else if (type.is_string_type()) {
    need_cast = true;
  }
  if (OB_FAIL(ret)) {
  } else if (need_cast) {
    if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(DEFAULT_CAST_TYPE_STR, dst_subschema_id))) {
      LOG_WARN("failed to get subschema id by type string", K(ret), K(DEFAULT_CAST_TYPE_STR));
    } else {
      type.set_calc_type(ObCollectionSQLType);
      type.set_calc_subschema_id(dst_subschema_id);
    }
  }
   
  return ret;
}

int ObArrayExprUtils::collect_vector_cast_info(ObExprResType &type, ObExecContext &exec_ctx, ObVectorCastInfo &info)
{
  int ret = OB_SUCCESS;
  if (type.is_collection_sql_type()) {
    ObSubSchemaValue value;
    info.subschema_id_ = type.get_subschema_id();
    if (OB_FAIL(exec_ctx.get_sqludt_meta_by_subschema_id(info.subschema_id_, value))) {
      LOG_WARN("failed to get subschema ctx", K(ret));
    } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid subschema type", K(ret), K(value));
    } else {
      const ObSqlCollectionInfo *coll_info = NULL;
      coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_);
      if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_VECTOR_TYPE) {
        ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_); 
        info.is_vector_ = true;
        info.dim_cnt_ = arr_type->dim_cnt_;
      } else if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_ARRAY_TYPE) {
        ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
        ObCollectionBasicType *elem_type = static_cast<ObCollectionBasicType *>(arr_type->element_type_);
        if (ObFloatType != elem_type->basic_meta_.get_obj_type()) {
          info.need_cast_ = true;
        }
      } else if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_SPARSE_VECTOR_TYPE) {
        info.is_sparse_vector_ = true;
      } else if (coll_info->collection_meta_->type_id_ == ObNestedType::OB_MAP_TYPE) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("invalid type for op", K(ret), K(coll_info->collection_meta_->type_id_));
      }
    }
  } else if (type.is_string_type()) {
    info.need_cast_ = true;
  } else if (!type.is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(type));
  }
  return ret;
}

int ObArrayExprUtils::calc_cast_type2(
    const ObExprOperatorType &expr_type,
    ObExprResType &type1,
    ObExprResType &type2,
    common::ObExprTypeCtx &type_ctx,
    uint16_t &res_subschema_id,
    const bool only_vector)
{
  int ret = OB_SUCCESS;
  res_subschema_id = UINT16_MAX;
  ObSQLSessionInfo *session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session());
  ObExecContext *exec_ctx = OB_ISNULL(session) ? NULL : session->get_cur_exec_ctx();
  ObString default_dst_type("ARRAY(FLOAT)");
  uint16_t default_dst_subschema_id = UINT16_MAX;

  ObVectorCastInfo info1;
  ObVectorCastInfo info2;
  if (OB_ISNULL(exec_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec ctx is null", K(ret));
  } else if (OB_FAIL(collect_vector_cast_info(type1, *exec_ctx, info1))) {
    LOG_WARN("failed to collect vector cast info", K(ret));
  } else if (OB_FAIL(collect_vector_cast_info(type2, *exec_ctx, info2))) {
    LOG_WARN("failed to collect vector cast info", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (info1.is_sparse_vector_ && info2.is_sparse_vector_) {
    if (!is_sparse_vector_supported(expr_type)) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("failed to calc cast type", K(ret), K(expr_type));
    }
    res_subschema_id = info1.subschema_id_;
    type_ctx.set_cast_mode(type_ctx.get_cast_mode() & (~CM_WARN_ON_FAIL));
  } else if (info1.is_sparse_vector_ ) {
    if (!is_sparse_vector_supported(expr_type) || (!type2.is_string_type() && !type2.is_null())) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("failed to calc cast type", K(ret));
    } else if (!type2.is_null()) {
      type2.set_calc_type(ObCollectionSQLType);
      type2.set_calc_subschema_id(info1.subschema_id_);
    }
    res_subschema_id = info1.subschema_id_;
    type_ctx.set_cast_mode(type_ctx.get_cast_mode() & (~CM_WARN_ON_FAIL));
  } else if (info2.is_sparse_vector_) {
    if (!is_sparse_vector_supported(expr_type) || (!type1.is_string_type() && !type1.is_null())) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("failed to calc cast type", K(ret));
    } else if (!type1.is_null()) {
      type1.set_calc_type(ObCollectionSQLType);
      type1.set_calc_subschema_id(info2.subschema_id_);
    }
    res_subschema_id = info2.subschema_id_;
    type_ctx.set_cast_mode(type_ctx.get_cast_mode() & (~CM_WARN_ON_FAIL));
  } else if (info1.is_vector_ && info2.is_vector_) {
    if (info1.dim_cnt_ != info2.dim_cnt_) {
      ret = OB_ERR_INVALID_VECTOR_DIM;
      LOG_WARN("check array validty failed", K(ret), K(info1.dim_cnt_), K(info2.dim_cnt_));
    }
  } else if (info1.is_vector_) {
    if (!type2.is_null()) {
      type2.set_calc_type(ObCollectionSQLType);
      type2.set_calc_subschema_id(info1.subschema_id_);
      info2.need_cast_ = true;
    }
    res_subschema_id = info1.subschema_id_;
  } else if (info2.is_vector_) {
    if (!type1.is_null()) {
      type1.set_calc_type(ObCollectionSQLType);
      type1.set_calc_subschema_id(info2.subschema_id_);
      info1.need_cast_ = true;
    }
    res_subschema_id = info2.subschema_id_;
  } else if (only_vector) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("no vector in the expr", K(ret));
  } else if (info1.need_cast_ || info2.need_cast_) {
    if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(default_dst_type, default_dst_subschema_id))) {
      LOG_WARN("failed to get subschema id by type string", K(ret), K(default_dst_type));
    } else {
      if (info1.need_cast_) {
        type1.set_calc_type(ObCollectionSQLType);
        type1.set_calc_subschema_id(default_dst_subschema_id);
      }
      if (info2.need_cast_) {
        type2.set_calc_type(ObCollectionSQLType);
        type2.set_calc_subschema_id(default_dst_subschema_id);
      }
      res_subschema_id = default_dst_subschema_id;
    }
  }
  if (OB_SUCC(ret)) {
    if (type1.is_collection_sql_type() && !info1.need_cast_) {
      type1.set_calc_type(ObCollectionSQLType);
      type1.set_calc_subschema_id(type1.get_subschema_id()); // avoid cast by set the same subschema_id
      res_subschema_id = type1.get_subschema_id();
    }
    if (type2.is_collection_sql_type() && !info2.need_cast_) {
      type2.set_calc_type(ObCollectionSQLType);
      type2.set_calc_subschema_id(type2.get_subschema_id()); // avoid cast by set the same subschema_id
      res_subschema_id = type2.get_subschema_id();
    }
  }
  return ret;
}

int ObArrayExprUtils::set_array_res(ObIArrayType *arr_obj, const int32_t res_size, const ObExpr &expr, ObEvalCtx &ctx, ObString &res, const char *data)
{
  int ret = OB_SUCCESS;
  char *res_buf = nullptr;
  int64_t res_buf_len = 0;
  ObDatum tmp_res;
  ObTextStringDatumResult str_result(expr.datum_meta_.type_, &expr, &ctx, &tmp_res);
  if (OB_FAIL(str_result.init(res_size, nullptr))) {
    LOG_WARN("fail to init result", K(ret), K(res_size));
  } else if (OB_FAIL(str_result.get_reserved_buffer(res_buf, res_buf_len))) {
    LOG_WARN("fail to get reserver buffer", K(ret));
  } else if (res_buf_len < res_size) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid res buf len", K(ret), K(res_buf_len), K(res_size));
  } else if (nullptr != data) {
    MEMCPY(res_buf, data, res_size);
  } else if (nullptr != arr_obj && OB_FAIL(arr_obj->get_raw_binary(res_buf, res_buf_len))) {
    LOG_WARN("get array raw binary failed", K(ret), K(res_buf_len), K(res_size));
  }
  if (FAILEDx(str_result.lseek(res_size, 0))) {
    LOG_WARN("failed to lseek res.", K(ret), K(str_result), K(res_size));
  } else {
    str_result.get_result_buffer(res);
  }
  return ret;
}

int ObArrayExprUtils::set_array_res(ObIArrayType *arr_obj, const int32_t res_size, ObIAllocator &allocator, ObString &res, const char *data)
{
  int ret = OB_SUCCESS;
  const bool has_lob_header = true;
  char *res_buf = nullptr;
  int64_t res_buf_len = 0;
  ObDatum tmp_res;
  ObTextStringDatumResult str_result(ObCollectionSQLType, has_lob_header, &tmp_res);
  if (OB_FAIL(str_result.init(res_size, &allocator))) {
    LOG_WARN("fail to init result", K(ret), K(res_size));
  } else if (OB_FAIL(str_result.get_reserved_buffer(res_buf, res_buf_len))) {
    LOG_WARN("fail to get reserver buffer", K(ret));
  } else if (res_buf_len < res_size) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid res buf len", K(ret), K(res_buf_len), K(res_size));
  } else if (nullptr != data) {
    MEMCPY(res_buf, data, res_size);
  } else if (nullptr != arr_obj && OB_FAIL(arr_obj->get_raw_binary(res_buf, res_buf_len))) {
    LOG_WARN("get array raw binary failed", K(ret), K(res_buf_len), K(res_size));
  } 
  if (FAILEDx(str_result.lseek(res_size, 0))) {
    LOG_WARN("failed to lseek res.", K(ret), K(str_result), K(res_size));
  } else {
    str_result.get_result_buffer(res);
  }
  return ret;
}

int ObArrayExprUtils::check_array_type_compatibility(ObExecContext *exec_ctx, uint16_t l_subid, uint16_t r_subid, bool &is_compatiable)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue l_meta;
  ObSubSchemaValue r_meta;
  if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(l_subid, l_meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(l_subid));
  } else if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(r_subid, r_meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(l_subid));
  } else if (l_meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE
             || r_meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid subschema type", K(ret), K(l_meta.type_), K(r_meta.type_));
  } else if (OB_ISNULL(l_meta.value_) || OB_ISNULL(r_meta.value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type info is null", K(ret), K(l_meta.value_), K(r_meta.value_));
  } else {
    is_compatiable =
      reinterpret_cast<const ObSqlCollectionInfo *>(l_meta.value_)->has_same_super_type(*reinterpret_cast<const ObSqlCollectionInfo *>(r_meta.value_));
  }
  return ret;
}

int ObArrayExprUtils::get_coll_info_by_subschema_id(ObExecContext *exec_ctx, uint16_t subid, const ObSqlCollectionInfo *&coll_info)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue meta;
  if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(subid, meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(subid));
  } else if (meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid subschema type", K(ret), K(meta.type_));
  } else if (OB_ISNULL(meta.value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type info is null", K(ret));
  } else {
    coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(meta.value_);
  }
  return ret;
}

int ObArrayExprUtils::get_array_element_type(ObExecContext *exec_ctx, uint16_t subid, ObDataType &elem_type,
                                             uint32_t &depth, bool &is_vec)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue meta;
  if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(subid, meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(subid));
  } else if (meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid subschema type", K(ret), K(meta.type_));
  } else if (OB_ISNULL(meta.value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type info is null", K(ret));
  } else {
    const ObSqlCollectionInfo * coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(meta.value_);
    elem_type = coll_info->get_basic_meta(depth);
    is_vec = coll_info->collection_meta_->type_id_ == ObNestedType::OB_VECTOR_TYPE;
  }
  return ret;
}

int ObArrayExprUtils::get_array_element_type(ObExecContext *exec_ctx, uint16_t subid, ObObjType &obj_type,
                                             uint32_t &depth, bool &is_vec)
{
  int ret = OB_SUCCESS;
  ObDataType elem_type;
  if (OB_FAIL(get_array_element_type(exec_ctx, subid, elem_type, depth, is_vec))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(subid));
  } else {
    obj_type = elem_type.get_obj_type();
  }
  return ret;
}

int ObArrayExprUtils::deduce_array_element_type(ObExecContext *exec_ctx, ObExprResType* types_stack, int64_t param_num, ObDataType &elem_type)
{
  int ret = OB_SUCCESS;
  uint16_t last_subschema_id = ObInvalidSqlType;
  ObExprResType coll_calc_type;
  elem_type.meta_.set_utinyint(); // default type
  bool is_first_elem = true;
  // calculate array element type
  for (int64_t i = 0; i < param_num && OB_SUCC(ret); i++) {
    if (types_stack[i].is_null()) {
    } else if (ob_is_collection_sql_type(types_stack[i].get_type())) {
      // check subschmea id
      ObCollectionTypeBase *coll_type = NULL;
      if (OB_FAIL(ObArrayExprUtils::get_coll_type_by_subschema_id(exec_ctx, types_stack[i].get_subschema_id(), coll_type))) {
        LOG_WARN("failed to get array type by subschema id", K(ret), K(types_stack[i].get_subschema_id()));
      } else if (coll_type->type_id_ != ObNestedType::OB_ARRAY_TYPE && coll_type->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("invalid collection type", K(ret), K(coll_type->type_id_));
      } else if (is_first_elem) {
        is_first_elem = false;
        coll_calc_type = types_stack[i];
        last_subschema_id = types_stack[i].get_subschema_id();
        elem_type.meta_.set_collection(last_subschema_id);
      } else if (last_subschema_id == ObInvalidSqlType) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("array element type dismatch", K(ret));
      } else if (last_subschema_id != types_stack[i].get_subschema_id()) {
        ObExprResType tmp_calc_type;
        if (OB_FAIL(ObExprResultTypeUtil::get_array_calc_type(exec_ctx, coll_calc_type, types_stack[i], tmp_calc_type))) {
          LOG_WARN("failed to check array compatibilty", K(ret));
        } else {
          last_subschema_id = tmp_calc_type.get_subschema_id();
          coll_calc_type = tmp_calc_type;
          elem_type.meta_.set_collection(last_subschema_id);
        }
      }
    } else if (last_subschema_id != ObInvalidSqlType) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("array element type dismatch", K(ret));
    } else if (!ob_is_array_supported_type(types_stack[i].get_type())) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("unsupported element type", K(ret), K(types_stack[i].get_type()));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "array element type");
    } else if (ob_is_varbinary_or_binary(types_stack[i].get_type(), types_stack[i].get_collation_type())) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("array element in binary type isn't supported", K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "array element in binary type");
    } else if (OB_FAIL(ObExprResultTypeUtil::get_deduce_element_type(types_stack[i], elem_type))) {
      LOG_WARN("get deduce type failed", K(ret), K(types_stack[i].get_type()), K(elem_type.get_obj_type()), K(i));
    } else {
      is_first_elem = false;
    }
  }
  
  // set params calculate type
  if (last_subschema_id == ObInvalidSqlType) {
    for (int64_t i = 0; i < param_num && OB_SUCC(ret); i++) {
      if (types_stack[i].is_null()) {
      } else if (types_stack[i].get_type() != elem_type.get_obj_type()) {
        types_stack[i].set_calc_meta(elem_type.get_meta_type());
        types_stack[i].set_calc_accuracy(elem_type.get_accuracy());
      }
    }
  }
  return ret;
}

int ObArrayExprUtils::deduce_nested_array_subschema_id(ObExecContext *exec_ctx,  ObDataType &elem_type, uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  uint16_t elem_subid = elem_type.meta_.get_subschema_id();
  ObSubSchemaValue elem_meta;
  if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(elem_subid, elem_meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(elem_subid));
  } else if (elem_meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid subschema type", K(ret), K(elem_meta.type_));
  } else {
    const int MAX_LEN = 256;
    int64_t pos = 0;
    char tmp[MAX_LEN] = {0};
    ObString type_info;
    const ObSqlCollectionInfo *coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(elem_meta.value_);
    if (OB_FAIL(databuff_printf(tmp, MAX_LEN, pos, "ARRAY("))) {
      LOG_WARN("failed to convert len to string", K(ret));
    } else if (FALSE_IT(STRNCPY(tmp + pos, coll_info->name_def_, coll_info->name_len_))) {
    } else if (FALSE_IT(pos += coll_info->name_len_)) {
    } else if (OB_FAIL(databuff_printf(tmp, MAX_LEN, pos, ")"))) {
      LOG_WARN("failed to add ) to string", K(ret));
    } else if (FALSE_IT(type_info.assign_ptr(tmp, static_cast<int32_t>(pos)))) {
    } else if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(type_info, subschema_id))) {
      LOG_WARN("failed get subschema id", K(ret), K(type_info));
    }
  }
  return ret;
}

int ObArrayExprUtils::deduce_map_subschema_id(ObExecContext *exec_ctx, uint16_t key_subid, uint16_t value_subid, uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  const int MAX_LEN = 256;
  int64_t pos = 0;
  char type_str[MAX_LEN] = {0};
  ObString type_info;
  ObSubSchemaValue key_meta;
  ObSubSchemaValue value_meta;
  const ObSqlCollectionInfo *key_coll_info;
  const ObSqlCollectionInfo *value_coll_info;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(key_subid, key_meta))) {
    LOG_WARN("failed to get key meta.", K(ret), K(key_subid));
  } else if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(value_subid, value_meta))) {
    LOG_WARN("failed to get value meta.", K(ret), K(value_subid));
  } else if (OB_ISNULL(key_coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(key_meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret), K(key_coll_info));
  } else if (OB_ISNULL(value_coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value_meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret), K(value_coll_info));
  } else if (OB_FAIL(databuff_printf(type_str, MAX_LEN, pos, "MAP("))) {
    LOG_WARN("failed to print MAP( to string", K(ret));
  } else if (key_coll_info->name_len_ < 7 ) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid collection name define", K(ret), K(key_coll_info->name_len_), K(key_coll_info->name_def_));
  } else if (FALSE_IT(STRNCPY(type_str + pos, key_coll_info->name_def_ + 6, key_coll_info->name_len_ - 7))) {
    // remove "ARRAY(" and ")", e.g ARRAY(INT) -> INT
  } else if (FALSE_IT(pos += key_coll_info->name_len_ - 7)) {
  } else if (OB_FAIL(databuff_printf(type_str, MAX_LEN, pos, ","))) {
    LOG_WARN("failed to print comma to string", K(ret));
  } else if (value_coll_info->name_len_ < 7 ) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid collection name define", K(ret), K(value_coll_info->name_len_), K(value_coll_info->name_def_));
  } else if (FALSE_IT(STRNCPY(type_str + pos, value_coll_info->name_def_ + 6, value_coll_info->name_len_ - 7))) {
  } else if (FALSE_IT(pos += value_coll_info->name_len_ - 7)) {
  } else if (OB_FAIL(databuff_printf(type_str, MAX_LEN, pos, ")"))) {
    LOG_WARN("failed to print ) to string", K(ret));
  } else if (FALSE_IT(type_info.assign_ptr(type_str, static_cast<int32_t>(pos)))) {
  } else if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(type_info, subschema_id))) {
    LOG_WARN("failed get subschema id", K(ret), K(type_info));
  }
  return ret;
}

int ObVectorVectorArithFunc::operator()(ObDatum &res, const ObDatum &l, const ObDatum &r, const ObExpr &expr, ObEvalCtx &ctx, ArithType type) const
{
  int ret = OB_SUCCESS;
  const ObExpr &left_expr = *expr.args_[0];
  const ObExpr &right_expr = *expr.args_[1];
  ObIArrayType *arr_l = NULL;
  ObIArrayType *arr_r = NULL;
  ObIArrayType *arr_res = NULL;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  ObSubSchemaValue value;
  uint16_t subschema_id = expr.obj_meta_.get_subschema_id();
  const ObSqlCollectionInfo *coll_info = NULL;
  ObCollectionArrayType *arr_type = NULL;
  if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
    LOG_WARN("failed to get subschema ctx", K(ret));
  } else if (OB_FAIL(ObArrayExprUtils::get_type_vector(left_expr, l, ctx, tmp_allocator, arr_l))) {
    LOG_WARN("failed to get vector", K(ret));
  } else if (OB_FAIL(ObArrayExprUtils::get_type_vector(right_expr, r, ctx, tmp_allocator, arr_r))) {
    LOG_WARN("failed to get vector", K(ret));
  } else if (OB_ISNULL(arr_l) || OB_ISNULL(arr_r)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret), K(arr_l), K(arr_r));
  } else if (OB_UNLIKELY(arr_l->size() != arr_r->size())) {
    ret = OB_ERR_INVALID_VECTOR_DIM;
    LOG_WARN("check array validty failed", K(ret), K(arr_l->size()), K(arr_r->size()));
  } else if (arr_l->contain_null() || arr_r->contain_null()) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("array with null can't add", K(ret));
  } else if (FALSE_IT(coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_))) {
  } else if (OB_ISNULL(coll_info)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("collect info is null", K(ret), K(subschema_id));
  } else if (OB_ISNULL(arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_))) {
    ret = OB_ERR_NULL_VALUE;
     LOG_WARN("array type is null", K(ret), K(subschema_id));
  } else if (OB_FAIL(ObArrayTypeObjFactory::construct(tmp_allocator, *arr_type, arr_res))) {
    LOG_WARN("construct array obj failed", K(ret), K(subschema_id), K(coll_info));
  } else {
    const float *data_l = reinterpret_cast<const float*>(arr_l->get_data());
    const float *data_r = reinterpret_cast<const float*>(arr_r->get_data());
    const uint32_t size = arr_l->size();
    ObArrayFixedSize<float> *float_array = static_cast<ObArrayFixedSize<float> *>(arr_res);
    for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
      const float float_res = type == ADD ? data_l[i] + data_r[i] :
                              type == MUL ? data_l[i] * data_r[i] :
                              data_l[i] - data_r[i];
      if (std::isinf(float_res) != 0) {
        ret = OB_OPERATE_OVERFLOW;
        LOG_WARN("value overflow", K(ret), K(i), K(data_l[i]), K(data_r[i]));
      } else if (OB_FAIL(float_array->push_back(float_res))) {
        LOG_WARN("failed to push back value", K(ret), K(float_res));
      }
    }
    ObString res_str;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObArrayExprUtils::set_array_res(arr_res,
                                                       arr_res->get_raw_binary_len(),
                                                       ctx.get_expr_res_alloc(),
                                                       res_str))) {
      LOG_WARN("get array binary string failed", K(ret), K(*coll_info));
    //   FIXME huhaosheng.hhs: maybe set batch_idx_ before in order to use frame res_buf
    // } else if (OB_FAIL(ObArrayExprUtils::set_array_res(arr_res, expr, ctx, res_str))) { 
    
    //   LOG_WARN("get array binary string failed", K(ret), K(*coll_info));
    } else {
      res.set_string(res_str);
    }
  }
  return ret;
}

int ObVectorElemArithFunc::operator()(ObDatum &res, const ObDatum &l, const ObDatum &r, const ObExpr &expr, ObEvalCtx &ctx, ArithType type) const
{
  UNUSED(type);
  int ret = OB_SUCCESS;
  const ObExpr &left_expr = *expr.args_[0];
  const ObExpr &right_expr = *expr.args_[1];
  ObIArrayType *arr_l = NULL;
  float data_r = r.get_float();
  ObIArrayType *arr_res = NULL;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  ObSubSchemaValue value;
  uint16_t subschema_id = expr.obj_meta_.get_subschema_id();
  const ObSqlCollectionInfo *coll_info = NULL;
  ObCollectionArrayType *arr_type = NULL;
  if (0 == data_r) {
    res.set_null();
  } else if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
    LOG_WARN("failed to get subschema ctx", K(ret));
  } else if (OB_FAIL(ObArrayExprUtils::get_type_vector(left_expr, l, ctx, tmp_allocator, arr_l))) {
    LOG_WARN("failed to get vector", K(ret));
  } else if (OB_ISNULL(arr_l)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret), K(arr_l));
  } else if (arr_l->contain_null()) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("array with null can't add", K(ret));
  } else if (FALSE_IT(coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_))) {
  } else if (OB_ISNULL(coll_info)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("collect info is null", K(ret), K(subschema_id));
  } else if (OB_ISNULL(arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_))) {
    ret = OB_ERR_NULL_VALUE;
     LOG_WARN("array type is null", K(ret), K(subschema_id));
  } else if (OB_FAIL(ObArrayTypeObjFactory::construct(tmp_allocator, *arr_type, arr_res))) {
    LOG_WARN("construct array obj failed", K(ret), K(subschema_id), K(coll_info));
  } else if (arr_type->element_type_->type_id_ != ObNestedType::OB_BASIC_TYPE) {
    ret = OB_NOT_SUPPORTED;
    OB_LOG(WARN, "not supported vector element type", K(ret), K(arr_type->element_type_->type_id_));
  } else {
    ObCollectionBasicType *elem_type = static_cast<ObCollectionBasicType *>(arr_type->element_type_);
    ObObjType obj_type = elem_type->basic_meta_.get_obj_type();
    if (obj_type == ObFloatType) {
      const float *data_l = reinterpret_cast<const float*>(arr_l->get_data());
      const uint32_t size = arr_l->size();
      ObVectorF32Data *float_array = static_cast<ObVectorF32Data *>(arr_res);
      for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
        const float float_res = data_l[i] / data_r; // only support div now
        if (std::isinf(float_res) != 0) {
          ret = OB_OPERATE_OVERFLOW;
          LOG_WARN("value overflow", K(ret), K(i), K(data_l[i]), K(data_r));
        } else if (OB_FAIL(float_array->push_back(float_res))) {
          LOG_WARN("failed to push back value", K(ret), K(float_res));
        }
      }
    } else if (obj_type == ObUTinyIntType) {
      const uint8_t *data_l = reinterpret_cast<const uint8_t*>(arr_l->get_data());
      const uint32_t size = arr_l->size();
      ObVectorU8Data *uint8_array = static_cast<ObVectorU8Data *>(arr_res);
      for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
        const uint8_t uint8_res = data_l[i] / data_r; // only support div now
        if (std::isinf(static_cast<float>(uint8_res)) != 0) {
          ret = OB_OPERATE_OVERFLOW;
          LOG_WARN("value overflow", K(ret), K(i), K(data_l[i]), K(data_r));
        } else if (OB_FAIL(uint8_array->push_back(uint8_res))) {
          LOG_WARN("failed to push back value", K(ret), K(uint8_res));
        }
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported vector element type", K(ret), K(obj_type), K(subschema_id), K(coll_info));
    }
    ObString res_str;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObArrayExprUtils::set_array_res(arr_res,
                                                       arr_res->get_raw_binary_len(),
                                                       ctx.get_expr_res_alloc(),
                                                       res_str))) {
      LOG_WARN("get array binary string failed", K(ret), K(*coll_info));
    } else {
      res.set_string(res_str);
    }
  }
  return ret;
}


int ObArrayExprUtils::get_array_type_by_subschema_id(ObEvalCtx &ctx, const uint16_t subschema_id, ObCollectionArrayType *&arr_type)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue meta;
  const ObSqlCollectionInfo *coll_info = NULL;
  if (OB_NOT_NULL(arr_type)) {
    // do nothing
  } else if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, meta))) {
    LOG_WARN("failed to get subschema value", K(ret), K(subschema_id));
  } else if (OB_ISNULL(coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source array collection info is null", K(ret));
  } else if (OB_ISNULL(arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source array collection array type is null", K(ret), K(*coll_info));
  }
  return ret;
}

int ObArrayExprUtils::get_coll_type_by_subschema_id(ObExecContext *exec_ctx, const uint16_t subschema_id, ObCollectionTypeBase *&coll_type)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue meta;
  const ObSqlCollectionInfo *coll_info = NULL;
  if (OB_NOT_NULL(coll_type)) {
    // do nothing
  } else if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(subschema_id, meta))) {
    LOG_WARN("failed to get subschema value", K(ret), K(subschema_id));
  } else if (OB_ISNULL(coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source collection info is null", K(ret));
  } else if (OB_ISNULL(coll_type = (coll_info->collection_meta_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source collection meta type is null", K(ret), K(*coll_info));
  }
  return ret;
}

int ObArrayExprUtils::construct_array_obj(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t subschema_id, ObIArrayType *&res, bool read_only)
{
  int ret = OB_SUCCESS;
  ObCollectionTypeBase *coll_type = NULL;
  if (OB_FAIL(get_coll_type_by_subschema_id(&ctx.exec_ctx_, subschema_id, coll_type))) {
    LOG_WARN("failed to get array type by subschema id", K(ret), K(subschema_id));
  } else if (OB_FAIL(ObArrayTypeObjFactory::construct(alloc, *coll_type, res, read_only))) {
    LOG_WARN("construct array obj failed", K(ret));
  }
  return ret;
}

int ObArrayExprUtils::get_array_obj(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t subschema_id, const ObString &raw_data, ObIArrayType *&res)
{
  int ret = OB_SUCCESS;
  ObString data_str = raw_data;
  if (res == NULL && OB_FAIL(construct_array_obj(alloc, ctx, subschema_id, res))) {
    LOG_WARN("construct array obj failed", K(ret));
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, &alloc,
                                                              ObLongTextType,
                                                              CS_TYPE_BINARY,
                                                              true,
                                                              data_str))) {
    LOG_WARN("fail to get real data.", K(ret), K(data_str));
  } else if (OB_FAIL(res->init(data_str))) {
    LOG_WARN("failed to init array", K(ret));
  }
  return ret;
}

int ObArrayExprUtils::add_elem_to_array(const ObExpr &expr, ObEvalCtx &ctx, ObIAllocator &alloc,
                                        ObCollectionArrayType *arr_type,  ObIArrayType *arr_obj, int args_idx)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = NULL;
  if (OB_FAIL(expr.args_[args_idx]->eval(ctx, datum))) {
  LOG_WARN("failed to eval args", K(ret), K(args_idx));
  } else if (arr_type->element_type_->type_id_ == ObNestedType::OB_BASIC_TYPE) {
    ObCollectionBasicType *value_elem = NULL;
    if (OB_ISNULL(value_elem = dynamic_cast<ObCollectionBasicType *>(arr_type->element_type_))) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("value_elem_type is null", K(ret), K(arr_type));
    } else if (OB_FAIL(ObArrayUtil::append(*arr_obj, value_elem->basic_meta_.get_obj_type(), datum))) {
      LOG_WARN("failed to append array value", K(ret));
    }
  } else if (arr_type->element_type_->type_id_ == ObNestedType::OB_ARRAY_TYPE ||
             arr_type->element_type_->type_id_ == ObNestedType::OB_VECTOR_TYPE) {
    ObString raw_bin;
    ObArrayNested *nest_array = NULL;
    uint16_t subschema_id = expr.args_[args_idx]->obj_meta_.get_subschema_id();
    if (OB_ISNULL(nest_array = static_cast<ObArrayNested *>(arr_obj))) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("nest_array is null", K(ret), K(arr_type));
    } else if (datum->is_null()) {
      if (OB_FAIL(nest_array->push_null())) {
        LOG_WARN("failed to push back null value", K(ret), K(args_idx));
      }
    } else if (OB_FAIL(add_elem_to_nested_array(alloc, ctx, subschema_id, *datum, nest_array))) {
      LOG_WARN("failed to add elem to nested array", K(ret), K(args_idx));
    }
  } else if (arr_type->type_id_ == ObNestedType::OB_MAP_TYPE ||
             arr_type->type_id_ == ObNestedType::OB_SPARSE_VECTOR_TYPE) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("nested map is not supported", K(ret));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid element type", K(ret), K(args_idx), K(arr_type->type_id_));
  }
  return ret;
}

int ObArrayExprUtils::add_elem_to_nested_array(ObIAllocator &tmp_allocator, ObEvalCtx &ctx, uint16_t subschema_id,
                                               const ObDatum &datum, ObArrayNested *nest_array)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue value;
  if (datum.is_null()) {
    if (OB_FAIL(nest_array->push_null())) {
      LOG_WARN("failed to push back null value", K(ret));
    }
  } else if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
    LOG_WARN("failed to get subschema ctx", K(ret));
  } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid subschema type", K(ret), K(value));
  } else {
    ObIArrayType *arr_obj = NULL;
    ObString raw_bin;
    const ObSqlCollectionInfo *coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_);
    ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
    if (OB_ISNULL(coll_info)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("collect info is null", K(ret), K(subschema_id));
    } else if (OB_FAIL(ObArrayTypeObjFactory::construct(tmp_allocator, *arr_type, arr_obj))) {
      LOG_WARN("construct array obj failed", K(ret), K(subschema_id), K(coll_info));
    } else if (FALSE_IT(raw_bin = datum.get_string())) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, &tmp_allocator,
                                                          ObCollectionSQLType,
                                                          CS_TYPE_BINARY,
                                                          true,
                                                          raw_bin))) {
      LOG_WARN("fail to get real data.", K(ret), K(raw_bin));
    } else if (OB_FAIL(arr_obj->init(raw_bin))) {
      LOG_WARN("failed to init array", K(ret));
    } else if (OB_FAIL(nest_array->push_back(*arr_obj))) {
      LOG_WARN("failed to push back array", K(ret));
    } 
  }
  return ret;
}

int ObArrayExprUtils::deduce_array_type(ObExecContext *exec_ctx, ObExprResType &type1,
                                        ObExprResType &type2,uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue arr_meta;
  const ObSqlCollectionInfo *coll_info = NULL;
  if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(type1.get_subschema_id(), arr_meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(type1.get_subschema_id()));
  } else if (arr_meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid subschema type", K(ret), K(arr_meta.type_));
  } else if (OB_ISNULL(coll_info = static_cast<const ObSqlCollectionInfo *>(arr_meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("coll info is null", K(ret));
  } else if (coll_info->collection_meta_->type_id_ != ObNestedType::OB_ARRAY_TYPE
             && coll_info->collection_meta_->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid collection type", K(ret), K(coll_info->collection_meta_->type_id_));
  } else if (type2.is_null()) {
    // do nothing
  } else if (!ob_is_collection_sql_type(type2.get_type())) {
    ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
    ObCollectionTypeBase *elem_type = arr_type->element_type_;
    if (!ob_is_array_supported_type(type2.get_type())) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("unexpected type for operation", K(ret), K(type2.get_type()));
    } else if (ob_is_varbinary_or_binary(type2.get_type(), type2.get_collation_type())) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("array element in binary type isn't supported", K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "array element in binary type");
    } else if (elem_type->type_id_ == ObNestedType::OB_BASIC_TYPE) {
      if (type2.get_type() != static_cast<ObCollectionBasicType *>(elem_type)->basic_meta_.get_obj_type()) {
        ObObjMeta calc_meta = type2.get_obj_meta();
        if (type2.get_type() == ObDecimalIntType || type2.get_type() == ObNumberType || type2.get_type() == ObUNumberType) {
          calc_meta.set_type(ObDoubleType);
          if (get_decimalint_type(type2.get_precision()) == DECIMAL_INT_32) {
            calc_meta.set_type(ObFloatType);
          }
        }
        if (calc_meta.get_type() == static_cast<ObCollectionBasicType *>(elem_type)->basic_meta_.get_obj_type()) {
          type2.set_calc_meta(calc_meta);
        } else {
          uint32_t depth = 0;
          ObDataType coll_elem1_type;
          ObDataType coll_calc_type;
          ObExprResType deduce_type;
          coll_calc_type.set_meta_type(calc_meta);
          coll_calc_type.set_accuracy(type2.get_accuracy());
          bool is_vec = false;
          ObCollationType calc_collection_type = CS_TYPE_INVALID;
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(ObArrayExprUtils::get_array_element_type(exec_ctx, type1.get_subschema_id(), coll_elem1_type, depth, is_vec))) {
            LOG_WARN("failed to get array element type", K(ret));
          } else if (OB_FAIL(ObExprResultTypeUtil::get_array_calc_type(exec_ctx, coll_elem1_type, coll_calc_type,
                                                                       depth, deduce_type, calc_meta))) {
            LOG_WARN("failed to get array calc type", K(ret));
          } else {
            type1.set_calc_meta(deduce_type);
            type2.set_calc_meta(calc_meta);
            subschema_id = deduce_type.get_subschema_id();
          }
        }
      }
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("invalid obj type", K(ret), K(*coll_info), K(type2.get_type()));
    }
  } else {
    // type2.is array
    ObCollectionTypeBase *type2_coll_type = NULL;
    ObString child_def;
    uint16_t child_subschema_id;
    ObExprResType child_type;
    ObExprResType coll_calc_type;
    if (OB_FAIL(ObArrayExprUtils::get_coll_type_by_subschema_id(exec_ctx, type2.get_subschema_id(), type2_coll_type))) {
      LOG_WARN("failed to get array type by subschema id", K(ret), K(type1.get_subschema_id()));
    } else if (type2_coll_type->type_id_ != ObNestedType::OB_ARRAY_TYPE && type2_coll_type->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("invalid collection type", K(ret), K(type2_coll_type->type_id_));
    } else if (OB_FAIL(coll_info->get_child_def_string(child_def))) {
      LOG_WARN("failed to get type1 child define", K(ret), K(*coll_info));
    } else if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(child_def, child_subschema_id))) {
      LOG_WARN("failed to get type1 child subschema id", K(ret), K(*coll_info), K(child_def));
    } else if (child_subschema_id == type2.get_subschema_id()) {
      // do nothing
    } else if (FALSE_IT(child_type.set_collection(child_subschema_id))) {
    } else if (OB_FAIL(ObExprResultTypeUtil::get_array_calc_type(exec_ctx, child_type, type2, coll_calc_type))) {
      LOG_WARN("failed to check array compatibilty", K(ret));
    } else {
      if (type2.get_subschema_id() != coll_calc_type.get_subschema_id()) {
        type2.set_calc_meta(coll_calc_type);
      }
      if (child_type.get_subschema_id() != coll_calc_type.get_subschema_id()) {
        ObDataType child_calc_type;
        uint16_t type1_calc_id;
        child_calc_type.meta_.set_collection(coll_calc_type.get_subschema_id());
        if (OB_FAIL(ObArrayExprUtils::deduce_nested_array_subschema_id(exec_ctx, child_calc_type, type1_calc_id))) {
          LOG_WARN("failed to deduce nested array subschema id", K(ret));
        } else {
          coll_calc_type.set_collection(type1_calc_id);
          type1.set_calc_meta(coll_calc_type);
          subschema_id = coll_calc_type.get_subschema_id();
        }
      }
    }
  }
  return ret;
}

int ObArrayExprUtils::get_child_subschema_id(ObExecContext *exec_ctx, uint16_t subid, uint16_t &child_subid)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue arr_meta;
  ObString child_def;
  const ObSqlCollectionInfo *coll_info = NULL;
  if (OB_FAIL(exec_ctx->get_sqludt_meta_by_subschema_id(subid, arr_meta))) {
    LOG_WARN("failed to get elem meta.", K(ret), K(subid));
  } else if (arr_meta.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid subschema type", K(ret), K(arr_meta.type_));
  } else if (OB_ISNULL(coll_info = static_cast<const ObSqlCollectionInfo *>(arr_meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("coll info is null", K(ret),  K(*coll_info));
  } else if (coll_info->collection_meta_->type_id_ != ObNestedType::OB_ARRAY_TYPE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("It's not nested array", K(ret));
  } else if (OB_FAIL(coll_info->get_child_def_string(child_def))) {
    LOG_WARN("failed to get type1 child define", K(ret), K(*coll_info));
  } else if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(child_def, child_subid))) {
    LOG_WARN("failed to get type1 child subschema id", K(ret), K(*coll_info), K(child_def));
  }
  return ret;
}

int ObNestedVectorFunc::construct_param(
    ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t meta_id, ObString &str_data, ObIArrayType *&param_obj)
{
  return ObArrayExprUtils::get_array_obj(alloc, ctx, meta_id, str_data, param_obj);
}



int ObArrayExprUtils::get_basic_elem(ObIArrayType *src, uint32_t idx, ObObj &elem_obj, bool &is_null)
{
  int ret = OB_SUCCESS;
  if (src->get_format() == Nested_Array) {
    ObArrayNested *arr_nested = static_cast<ObArrayNested *>(src);
    if (OB_FAIL(get_basic_elem(arr_nested->get_child_array(), idx, elem_obj, is_null))) {
      LOG_WARN("failed to cast get element", K(ret));
    }
  } else {
    if (src->is_null(idx)) {
      is_null = true;
    } else if (OB_FAIL(src->elem_at(idx, elem_obj))) {
      LOG_WARN("get elem obj failed", K(ret));
    }
  }
  return ret;
}

template int ObArrayExprUtils::calc_array_sum(unsigned int, unsigned char*, const char*,
                                              unsigned int, ObCollectionArrayType*, double&);
template int ObArrayExprUtils::calc_array_sum(unsigned int, unsigned char*, const char*,
                                              unsigned int, ObCollectionArrayType*, int64_t&);
template int ObArrayExprUtils::calc_array_sum(unsigned int, unsigned char*, const char*,
                                              unsigned int, ObCollectionArrayType*, uint64_t&);

int ObArrayExprUtils::get_array_data(ObString &data_str,
                        ObCollectionArrayType *arr_type, 
                        uint32_t &len,
                        uint8_t *&null_bitmaps,
                        const char *&data, 
                        uint32_t &data_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  char *raw_str = nullptr;
  len = 0, data_len = 0;
  null_bitmaps = nullptr, data = nullptr;

  if (arr_type->type_id_ == ObNestedType::OB_ARRAY_TYPE) {
    raw_str = data_str.ptr();
    len = *reinterpret_cast<uint32_t *>(raw_str);
    pos += sizeof(len);
    null_bitmaps = reinterpret_cast<uint8_t *>(raw_str + pos);
    pos += sizeof(uint8_t) * len;
  } else if (arr_type->type_id_ == ObNestedType::OB_VECTOR_TYPE) {
    raw_str = data_str.ptr();
    len = data_str.length() / sizeof(float);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected array type", K(ret));
  }
  if (pos > data_str.length()) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "raw data len is invalid", K(ret), K(pos), K(len), K(data_str.length()));
  } else {
    data = raw_str + pos;
    data_len = data_str.length() - pos;
  }

  return ret;
}


template<typename T>
int ObArrayExprUtils::raw_check_add(const T &res, const T &l, const T &r) {
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support array check add", K(res), K(l), K(r));
  return ret;
}

template<>
int ObArrayExprUtils::raw_check_add<int64_t>(const int64_t &res, const int64_t &l, const int64_t &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprAdd::is_int_int_out_of_range(l, r, res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "BIGINT");
  }
  return ret;
}
template<>
int ObArrayExprUtils::raw_check_add<uint64_t>(const uint64_t &res, const uint64_t &l, const uint64_t &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprAdd::is_uint_uint_out_of_range(l, r, res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "BIGINT UNSIGNED");
  }
  return ret;
}
template<>
int ObArrayExprUtils::raw_check_add<float>(const float &res, const float &l, const float &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprAdd::is_float_out_of_range(res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "FLOAT");
  }
  return ret;
}
template<>
int ObArrayExprUtils::raw_check_add<double>(const double &res, const double &l, const double &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprAdd::is_double_out_of_range(res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "DOUBLE");
  }
  return ret;
}

template int ObArrayExprUtils::raw_check_add<int8_t>(const int8_t &res, const int8_t &l, const int8_t &r);
template int ObArrayExprUtils::raw_check_add<int16_t>(const int16_t &res, const int16_t &l, const int16_t &r);
template int ObArrayExprUtils::raw_check_add<int32_t>(const int32_t &res, const int32_t &l, const int32_t &r);

template<typename T>
int ObArrayExprUtils::raw_check_minus(const T &res, const T &l, const T &r) {
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support array check", K(res), K(l), K(r));
  return ret;
}

template<>
int ObArrayExprUtils::raw_check_minus<int64_t>(const int64_t &res, const int64_t &l, const int64_t &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprMinus::is_int_int_out_of_range(l, r, res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "BIGINT");
  }
  return ret;
}
template<>
int ObArrayExprUtils::raw_check_minus<uint64_t>(const uint64_t &res, const uint64_t &l, const uint64_t &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprMinus::is_uint_uint_out_of_range(l, r, res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "BIGINT UNSIGNED");
  }
  return ret;
}
template<>
int ObArrayExprUtils::raw_check_minus<float>(const float &res, const float &l, const float &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprMinus::is_float_out_of_range(res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "FLOAT");
  }
  return ret;
}
template<>
int ObArrayExprUtils::raw_check_minus<double>(const double &res, const double &l, const double &r) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObExprMinus::is_double_out_of_range(res))) {
    ret = OB_OPERATE_OVERFLOW;
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Array", "DOUBLE");
  }
  return ret;
}

template int ObArrayExprUtils::raw_check_minus<int8_t>(const int8_t &res, const int8_t &l, const int8_t &r);
template int ObArrayExprUtils::raw_check_minus<int16_t>(const int16_t &res, const int16_t &l, const int16_t &r);
template int ObArrayExprUtils::raw_check_minus<int32_t>(const int32_t &res, const int32_t &l, const int32_t &r);

template <typename T>
int ObArrayExprUtils::calc_fixed_size_key_index(ObIArrayType *src_key_arr, uint32_t *idx_arr, uint32_t &idx_count)
{
  int ret = OB_SUCCESS;
  std::map<T, uint32_t> key_idx;
  ObArrayFixedSize<T> *key_arr = static_cast<ObArrayFixedSize<T> *>(src_key_arr);
  for (uint32_t i = 0; i < src_key_arr->size(); i++) {
    if (key_arr->is_null(i)) {
      idx_arr[0] = i;
      idx_count = 1;
    } else {
      key_idx[(*key_arr)[i]] = i;
    }
  }
  typename std::map<T, uint32_t>::iterator it = key_idx.begin();
  for (; it != key_idx.end() && OB_SUCC(ret); ++it) {
    idx_arr[idx_count++] = it->second;
  }
  return ret;
}

int ObArrayExprUtils::get_collection_raw_data(
    ObIAllocator &allocator,
    const ObObjMeta &meta,
    const void *data,
    ObLength len,
    ObString &bin_str,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  ObTextStringIter str_iter(ObCollectionSQLType, CS_TYPE_BINARY,
                            ObString(len, reinterpret_cast<const char *>(data)),
                            meta.has_lob_header());
  if (OB_ISNULL(access_ctx) || OB_ISNULL(access_ctx->lob_read_options_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("datum access context is not initialized", K(ret));
  } else if (OB_FAIL(str_iter.init(
                 0, access_ctx->lob_read_options_, &allocator))) {
    LOG_WARN("Lob: str iter init failed", K(ret));
  } else if (OB_FAIL(str_iter.get_full_data(bin_str))) {
    LOG_WARN("Lob: str iter get full data failed", K(ret));
  }
  return ret;
}

int ObArrayExprUtils::convert_to_string(common::ObIAllocator &allocator, ObEvalCtx &ctx, const uint16_t subschema_id, const common::ObString &data, ObString &res_str)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_allocator;
  ObIArrayType *arr_obj = NULL;
  ObStringBuffer buf(&allocator);
  if (OB_FAIL(ObArrayExprUtils::get_array_obj(tmp_allocator, ctx, subschema_id, data, arr_obj))) {
    LOG_WARN("get array failed", K(ret));
  } else if (OB_FAIL(arr_obj->print(buf))) {
    LOG_WARN("failed to format array", K(ret));
  } else {
    res_str.assign_ptr(buf.ptr(), buf.length());
  }
  return ret;
}

int ObArrayExprUtils::calc_collection_hash_val(
    const ObObjMeta &meta, const void *data, ObLength len,
    hash_algo hash_func, uint64_t seed, uint64_t &hash_val,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  ObString bin_str;
  common::ObArenaAllocator allocator(ObModIds::OB_LOB_READER, OB_MALLOC_NORMAL_BLOCK_SIZE);
  if (OB_FAIL(get_collection_raw_data(
          allocator, meta, data, len, bin_str, access_ctx))) {
    LOG_WARN("get collection raw data failed", K(ret));
  } else {
     hash_val = seed;
    if (bin_str.length() > 0) {
      hash_val = ObCharset::hash(CS_TYPE_BINARY, bin_str.ptr(), bin_str.length(), seed, false, hash_func);
    }
  }
  return ret;
}

int ObArrayExprUtils::collection_compare(const ObObjMeta &l_meta, const ObObjMeta &r_meta,
                                         const void *l_v, const ObLength l_len,
                                         const void *r_v, const ObLength r_len,
                                         int &cmp_ret,
                                         const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  ObString l_data;
  ObString r_data;
  common::ObArenaAllocator allocator(ObModIds::OB_LOB_READER, OB_MALLOC_NORMAL_BLOCK_SIZE);
  if (OB_FAIL(get_collection_raw_data(
          allocator, l_meta, l_v, l_len, l_data, access_ctx))) {
    LOG_WARN("get collection raw data failed", K(ret));
  } else if (OB_FAIL(get_collection_raw_data(
                 allocator, r_meta, r_v, r_len, r_data, access_ctx))) {
    LOG_WARN("get collection raw data failed", K(ret));
  } else {
    cmp_ret = ObCharset::strcmpsp(CS_TYPE_BINARY, l_data.ptr(), l_data.length(), r_data.ptr(),
                                    r_data.length(), false);
    cmp_ret = (cmp_ret > 0 ? 1 : (cmp_ret < 0 ? -1 : 0));
  }
  return ret;
}


// collection object is read only
int ObArrayExprUtils::get_collection_obj(ObEvalCtx &ctx, const uint16_t subschema_id, ObIArrayType *&res)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue meta;
  ObSqlCollectionInfo *coll_info = NULL;
  if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, meta))) {
    LOG_WARN("failed to get subschema value", K(ret), K(subschema_id));
  } else if (OB_ISNULL(coll_info = reinterpret_cast<ObSqlCollectionInfo *>(meta.value_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source array collection info is null", K(ret));
  } else if (FALSE_IT(res = coll_info->get_collection_obj())) {
  } else if (res != NULL) {
    res->clear();
  } else {
    if (OB_FAIL(ObArrayTypeObjFactory::construct(coll_info->allocator_, *coll_info->collection_meta_, res))) {
      LOG_WARN("construct array obj failed", K(ret));
    } else {
      coll_info->set_collection_obj(res);
    }
  }
  return ret;
}


int ObArrayExprUtils::calc_string_key_index(ObIArrayType *src_key_arr, uint32_t *idx_arr, uint32_t &idx_count)
{
  int ret = OB_SUCCESS;
  std::map<ObString, uint32_t> key_idx;
  ObArrayBinary *key_arr = static_cast<ObArrayBinary *>(src_key_arr);
  for (uint32_t i = 0; i < src_key_arr->size() && OB_SUCC(ret); i++) {
    if (key_arr->is_null(i)) {
      idx_arr[0] = i;
      idx_count = 1;
    } else {
      key_idx[(*key_arr)[i]] = i;
    }
  }
  std::map<ObString, uint32_t>::iterator it = key_idx.begin();
  for (; it != key_idx.end() && OB_SUCC(ret); ++it) {
    idx_arr[idx_count++] = it->second;
  }
  return ret;
}


} // sql
} // oceanbase
