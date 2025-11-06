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
#include "src/pl/ob_pl_resolver.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
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
    LOG_WARN("check udt bitmap overflow", K(ret), K(*this), K(index), K(byte_index), K(pos));
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
    LOG_WARN("set udt bitmap overflow", K(ret), K(*this), K(index), K(byte_index), K(pos));
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
    LOG_WARN("reset udt bitmap overflow", K(ret), K(*this), K(index), K(byte_index), K(pos));
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
      LOG_WARN("failed to check nested udt bitmap", K(ret));
    } else if (is_set && OB_FAIL(set_current_bitmap_pos())) {
      LOG_WARN("failed to set nested udt bitmap", K(ret));
    } else {
      get_pos()++;
    }
  }
  src.set_bitmap_pos(src_pos);
  return ret;
}



int ObSqlUdtUtils::ob_udt_flattern_pl_extend(const ObObj **flattern_objs,
                                             const int32_t &flattern_object_count,
                                             int32_t &pos,
                                             ObSqlUdtNullBitMap &nested_udt_bitmap,
                                             const ObObj *obj,
                                             ObExecContext &exec_context,
                                             ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

int ObSqlUdtUtils::ob_udt_reordering_leaf_objects(const ObObj **flattern_objs,
                                                  const ObObj **sorted_objs,
                                                  const int32_t &flattern_object_count,
                                                  ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < flattern_object_count; i++) {
    int64_t index = sql_udt.get_udt_meta().leaf_attrs_meta_[i].order_;
    sorted_objs[i] = flattern_objs[index];
  }
  return ret;
}

int ObSqlUdtUtils::ob_udt_calc_sql_varray_length(const ObObj *cur_obj,
                                                 int64_t &sql_udt_total_len,
                                                 bool with_lob_header)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

int ObSqlUdtUtils::ob_udt_calc_total_len(const ObObj **sorted_objs,
                                         const int32_t &flattern_object_count,
                                         int64_t &sql_udt_total_len,
                                         ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

int ObSqlUdtUtils::ob_udt_convert_pl_varray_to_sql_varray(const ObObj *cur_obj,
                                                          char *buf,
                                                          const int64_t buf_len,
                                                          int64_t &pos,
                                                          bool with_lob_header)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

// convert pl extend to sql udt by type
int ObSqlUdtUtils::ob_udt_convert_sorted_objs_array_to_udf_format(const ObObj **sorted_objs,
                                                                  char *buf,
                                                                  const int64_t buf_len,
                                                                  int64_t &pos,
                                                                  ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

// convert sql_udt to string by type



int ObSqlUdtUtils::convert_sql_udt_to_string(ObObj &sql_udt_obj,
                                             common::ObIAllocator *allocator,
                                             sql::ObExecContext *exec_context,
                                             ObSqlUDT &sql_udt,
                                             ObString &res_str)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

int ObSqlUdtUtils::convert_collection_to_string(ObObj &coll_obj, const ObSqlCollectionInfo &coll_meta,
                                                common::ObIAllocator *allocator, ObString &res_str)
{
  int ret = OB_SUCCESS;
  ObIArrayType *arr_obj = NULL;
  ObString coll_data = coll_obj.get_string();
  ObStringBuffer buf(allocator);
  ObArenaAllocator lob_allocator(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID());
  ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_meta.collection_meta_);
  if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&lob_allocator,
                                                        ObLongTextType,
                                                        CS_TYPE_BINARY,
                                                        true, coll_data))) {
    LOG_WARN("fail to get real string data", K(ret), K(coll_data));
  } else if (OB_FAIL(ObArrayTypeObjFactory::construct(*allocator, *arr_type, arr_obj, true))) {
    LOG_WARN("construct array obj failed", K(ret),  K(coll_meta));
  } else {
    if (OB_FAIL(arr_obj->init(coll_data))) {
      LOG_WARN("failed to init array", K(ret));
    } else if (OB_FAIL(arr_obj->print(buf))) {
      LOG_WARN("failed to format array", K(ret));
    } else {
      res_str.assign_ptr(buf.ptr(), buf.length());
    }
  }

  return ret;
}






int ObSqlUdtUtils::cast_sql_record_to_pl_record(sql::ObExecContext *exec_ctx,
                                                ObObj &result,
                                                ObString &udt_data,
                                                ObSqlUDTMeta &udt_meta)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}


int ObSqlUdtMetaUtils::generate_udt_meta_from_schema(ObSchemaGetterGuard *schema_guard,
                                                     ObSubSchemaCtx *subschema_ctx,
                                                     common::ObIAllocator &allocator,
                                                     uint64_t tenant_id,
                                                     uint64_t udt_id,
                                                     ObSqlUDTMeta &udt_meta)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  return ret;
}

}
}
