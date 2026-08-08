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

#include "sql/engine/expr/ob_obj_cast_runtime.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/engine/ob_exec_context.h"
#include "share/object/ob_array_cast.h"

namespace oceanbase
{
namespace sql
{

ObSqlObjCastRuntime::ObSqlObjCastRuntime(ObExecContext *exec_ctx)
  : exec_ctx_(exec_ctx),
    user_logging_ctx_(
        nullptr == exec_ctx ? nullptr : exec_ctx->get_user_logging_ctx())
{
}

ObSqlObjCastRuntime::ObSqlObjCastRuntime(const ObUserLoggingCtx *user_logging_ctx)
  : exec_ctx_(nullptr),
    user_logging_ctx_(user_logging_ctx)
{
}

int ObSqlObjCastRuntime::get_enum_set_values(
    const uint16_t subschema_id,
    const common::ObIArray<common::ObString> *&values,
    common::ObCollationType &collation_type) const
{
  int ret = OB_SUCCESS;
  const common::ObEnumSetMeta *meta = nullptr;
  values = nullptr;
  collation_type = common::CS_TYPE_INVALID;
  if (OB_ISNULL(exec_ctx_)) {
    ret = OB_ERR_UNDEFINED;
    LOG_WARN("object cast runtime has no execution context", K(ret));
  } else if (OB_FAIL(exec_ctx_->get_enumset_meta_by_subschema_id(
                 subschema_id, false, meta))) {
    LOG_WARN("failed to get enum/set metadata", K(ret), K(subschema_id));
  } else if (OB_ISNULL(meta) || OB_ISNULL(meta->get_str_values())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid enum/set metadata", K(ret), K(subschema_id), KP(meta));
  } else {
    values = meta->get_str_values();
    collation_type = meta->get_collation_type();
  }
  return ret;
}

int ObSqlObjCastRuntime::cast_collection(
    common::ObObjCastParams &params,
    const common::ObObj &input,
    common::ObObj &output,
    const uint64_t cast_mode) const
{
  int ret = OB_SUCCESS;
  if (input.is_null()) {
    output.set_null();
  } else if (OB_ISNULL(exec_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("object cast runtime has no execution context", K(ret));
  } else if (OB_ISNULL(params.allocator_v2_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("object cast allocator is null", K(ret));
  } else {
    const uint16_t dst_subschema_id = output.get_meta().get_subschema_id();
    ObSubSchemaValue dst_meta;
    if (OB_FAIL(exec_ctx_->get_sqludt_meta_by_subschema_id(
            dst_subschema_id, dst_meta))) {
      LOG_WARN("failed to get collection metadata", K(ret), K(dst_subschema_id));
    } else {
      common::ObString input_string = input.get_string();
      const common::ObCollationType cs_type = input.get_collation_type();
      common::ObIAllocator &allocator = *params.allocator_v2_;
      common::ObIArrayType *array = nullptr;
      const ObSqlCollectionInfo *collection_info =
          reinterpret_cast<const ObSqlCollectionInfo *>(dst_meta.value_);
      if (OB_ISNULL(collection_info) || OB_ISNULL(collection_info->collection_meta_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid collection metadata", K(ret), K(dst_subschema_id));
      } else {
        common::ObCollectionTypeBase *collection_type =
            collection_info->collection_meta_;
        common::ObCollectionArrayType *array_type =
            static_cast<common::ObCollectionArrayType *>(collection_type);
        if (collection_type->type_id_ != common::ObNestedType::OB_VECTOR_TYPE
            && OB_FAIL(common::ObArrayTypeObjFactory::construct(
                allocator, *array_type, array))) {
          LOG_WARN("failed to construct collection", K(ret));
        } else if (collection_type->type_id_ == common::ObNestedType::OB_VECTOR_TYPE) {
          const bool is_binary = cs_type == common::CS_TYPE_BINARY;
          if (OB_FAIL(common::ObArrayTypeObjFactory::construct(
                  allocator, *array_type, array, is_binary))) {
            LOG_WARN("failed to construct vector", K(ret));
          } else if (OB_FAIL(ObArrayCastUtils::string_cast_vector(
                         allocator, input_string, array, array_type, is_binary))) {
            LOG_WARN("failed to cast vector elements", K(ret));
          }
        } else if (collection_type->type_id_ == common::ObNestedType::OB_ARRAY_TYPE) {
          if (cs_type != common::CS_TYPE_BINARY) {
            if (OB_FAIL(ObArrayCastUtils::string_cast(
                    allocator, input_string, array, array_type->element_type_))) {
              LOG_WARN("failed to cast array elements", K(ret));
            }
          } else if (OB_FAIL(ObArrayCastUtils::string_cast_array(
                         input_string, array, array_type->element_type_))) {
            LOG_WARN("failed to decode array", K(ret));
          }
        } else if (collection_type->type_id_ == common::ObNestedType::OB_MAP_TYPE
                   || collection_type->type_id_
                          == common::ObNestedType::OB_SPARSE_VECTOR_TYPE) {
          common::ObCollectionMapType *map_type =
              static_cast<common::ObCollectionMapType *>(collection_type);
          if (collection_type->type_id_
              == common::ObNestedType::OB_SPARSE_VECTOR_TYPE) {
            if (OB_FAIL(ObArrayCastUtils::string_cast_sparse_vector_fast(
                    allocator, input_string, array, map_type))) {
              LOG_WARN("failed to cast sparse vector", K(ret));
            }
          } else if (OB_FAIL(ObArrayCastUtils::string_cast_map(
                         allocator, input_string, array, map_type,
                         cast_mode, false))) {
            LOG_WARN("failed to cast map", K(ret));
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unsupported collection type", K(ret), K(collection_type->type_id_));
        }

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(array->check_validity(*array_type, *array))) {
          LOG_WARN("invalid collection value", K(ret));
          if (ret == OB_ERR_INVALID_VECTOR_DIM) {
            LOG_USER_ERROR(
                OB_ERR_INVALID_VECTOR_DIM,
                static_cast<uint32_t>(array_type->dim_cnt_),
                array->size());
          }
        } else if (OB_FAIL(ObArrayCastUtils::set_array_obj_res(
                       array, &params, &output))) {
          LOG_WARN("failed to encode collection result", K(ret));
        }
      }
    }
  }
  return ret;
}

void ObSqlObjCastRuntime::report_warning(
    const int64_t code,
    const common::ObString &type_name,
    const common::ObString &input,
    const uint64_t cast_mode) const
{
  ObDataTypeCastUtil::log_user_error_warning(
      user_logging_ctx_, code, type_name, input, cast_mode);
}

}  // namespace sql
}  // namespace oceanbase
