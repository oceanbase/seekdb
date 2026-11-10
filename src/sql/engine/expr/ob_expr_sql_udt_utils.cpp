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
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/ob_spi.h"
#include "sql/pl/ob_pl_resolver.h"
#include "common/datum/datum_value.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

int ObSqlUdtUtils::convert_result_for_client(DatumValue &value, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *owner = nullptr;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(owner))) {
  } else if (owner == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = convert_result_for_client(value, owner, &result.get_session(),
                                    &result.get_exec_context(), result.is_ps_protocol());
  }
  return ret;
}

int ObSqlUdtUtils::convert_result_for_client(DatumValue &value, ObIAllocator *owner,
    ObSQLSessionInfo *session, ObExecContext *exec_ctx, bool is_ps_protocol,
    const ColumnsFieldIArray *fields, share::schema::ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  UNUSED(is_ps_protocol);
  if (session == nullptr || owner == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!value.is_collection_sql_type() && !value.is_geometry()) {
    ret = OB_NOT_SUPPORTED;
  } else if (value.is_geometry()) {
  } else if (exec_ctx == nullptr) {
    ret = OB_BAD_NULL_ERROR;
  } else {
    ObExecContext &ctx = *exec_ctx;
    if (ctx.get_physical_plan_ctx() == nullptr || !ctx.get_physical_plan_ctx()->is_subschema_ctx_inited()) {
      if (fields == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (ctx.get_physical_plan_ctx() == nullptr && OB_FAIL(ctx.create_physical_plan_ctx())) {
      } else {
        ret = ctx.get_physical_plan_ctx()->build_subschema_by_fields(fields, schema_guard);
      }
    }
    ObSubSchemaValue metadata;
    if (OB_SUCC(ret) && OB_FAIL(ctx.get_sqludt_meta_by_subschema_id(value.get_subschema_id(), metadata))) {
    } else if (OB_SUCC(ret) && metadata.type_ != ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
      // Both historical SQL-UDT-to-string and SQL-record-to-PL-record
      // converters in this build return this error unconditionally.
      ret = OB_NOT_SUPPORTED;
    } else if (OB_SUCC(ret)) {
      const auto *collection = reinterpret_cast<const ObSqlCollectionInfo *>(metadata.value_);
      ObString bytes = value.get_string();
      ObIArrayType *array = nullptr;
      ObStringBuffer output(owner);
      ObArenaAllocator scratch(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE);
      auto *array_type = static_cast<ObCollectionArrayType *>(collection->collection_meta_);
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx, &scratch,
              ObLongTextType, CS_TYPE_BINARY, true, bytes))) {
      } else if (OB_FAIL(ObArrayTypeObjFactory::construct(*owner, *array_type, array, true))) {
      } else if (OB_FAIL(array->init(bytes))) {
      } else if (OB_FAIL(array->print(output))) {
      } else {
        ObString retained;
        if (OB_FAIL(output.get_result_string(retained))) {
        } else {
          value.datum_.set_string(retained);
        }
      }
    }
  }
  return ret;
}

bool ObSqlUdtNullBitMap::is_valid()
{
  return (OB_NOT_NULL(bitmap_) && bitmap_len_ > 0);
}

int ObSqlUdtNullBitMap::check_bitmap_pos(uint32_t pos, bool &is_set)
{
  int ret = OB_SUCCESS;
  is_set = false;
  uint32 index = pos / 8;
  uint32 byte_index = pos % 8;
  if (index >= bitmap_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_set = bitmap_[index] & (1 << byte_index);
  }
  return ret;
}


int ObSqlUdtNullBitMap::set_bitmap_pos(uint32_t pos)
{
  int ret = OB_SUCCESS;
  uint32 index = pos / 8;
  uint32 byte_index = pos % 8;
  if (index >= bitmap_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bitmap_[index] |= (1 << byte_index);
  }
  return ret;
}

int ObSqlUdtNullBitMap::set_current_bitmap_pos()
{
  return set_bitmap_pos(pos_);
}

int ObSqlUdtNullBitMap::reset_bitmap_pos(uint32_t pos)
{
  int ret = OB_SUCCESS;
  uint32 index = pos / 8;
  uint32 byte_index = pos % 8;
  if (index >= bitmap_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bitmap_[index] &= ~(1 << byte_index);
  }
  return ret;
}


int ObSqlUdtNullBitMap::assign(ObSqlUdtNullBitMap &src, uint32_t pos, uint32_t bit_len)
{
  int ret = OB_SUCCESS;
  uint32_t src_pos = src.get_pos();
  for (int i = 0; i < bit_len && OB_SUCC(ret); i++) {
    bool is_set = false;
    if (OB_FAIL(src.check_bitmap_pos(pos + i, is_set))) {
    } else if (is_set && OB_FAIL(set_current_bitmap_pos())) {
    } else {
      get_pos()++;
    }
  }
  src.set_bitmap_pos(src_pos);
  return ret;
}



int ObSqlUdtMetaUtils::generate_udt_meta_from_schema(ObSchemaGetterGuard *schema_guard,
                                                     ObSubSchemaCtx *subschema_ctx,
                                                     common::ObIAllocator &allocator,
                                                     uint64_t udt_id,
                                                     ObSqlUDTMeta &udt_meta)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

}
}
