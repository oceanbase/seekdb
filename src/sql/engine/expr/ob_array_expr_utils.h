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

#ifndef OCEANBASE_SQL_OB_ARRAY_EXPR_UTILS_H_
#define OCEANBASE_SQL_OB_ARRAY_EXPR_UTILS_H_
#define USING_LOG_PREFIX SQL_ENG

#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"
#include "common/udt/ob_array_utils.h"
#include "sql/engine/expr/ob_expr.h" // for ObExpr
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_expr_array_map.h"
#include "sql/engine/vector/ob_i_vector.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;

struct ObVectorCastInfo
{
  ObVectorCastInfo()
    : is_vector_(false),
      is_sparse_vector_(false),
      need_cast_(false),
      subschema_id_(UINT16_MAX),
      dim_cnt_(0)
  {}
  bool is_vector_;
  bool is_sparse_vector_;
  bool need_cast_;
  uint16_t subschema_id_;
  uint16_t dim_cnt_;
};

class ObArrayExprUtils
{
public:
  ObArrayExprUtils();
  virtual ~ObArrayExprUtils() = default;
  static int set_array_res(ObIArrayType *arr_obj, const int32_t data_len, const ObExpr &expr, ObEvalCtx &ctx, common::ObString &res,
                           const char *data = nullptr);
  static int set_array_res(ObIArrayType *arr_obj, const int32_t data_len, ObIAllocator &allocator, common::ObString &res,
                           const char *data = nullptr);
  static int deduce_array_element_type(ObExecContext *exec_ctx, ObExprResType* types_stack, int64_t param_num, ObDataType &elem_type);
  static int deduce_nested_array_subschema_id(ObExecContext *exec_ctx,  ObDataType &elem_type, uint16_t &subschema_id);
  static int deduce_map_subschema_id(ObExecContext *exec_ctx, uint16_t key_subid, uint16_t value_subid, uint16_t &subschema_id);
  static int deduce_array_type(ObExecContext *exec_ctx, ObExprResType &type1, ObExprResType &type2,uint16_t &subschema_id);
  static int check_array_type_compatibility(ObExecContext *exec_ctx, uint16_t l_subid, uint16_t r_subid, bool &is_compatiable);
  static int get_coll_info_by_subschema_id(ObExecContext*exec_ctx, uint16_t subid, const ObSqlCollectionInfo *&coll_info);
  static int get_array_element_type(ObExecContext *exec_ctx, uint16_t subid, ObObjType &obj_type, uint32_t &depth, bool &is_vec);
  static int get_array_element_type(ObExecContext *exec_ctx, uint16_t subid, ObDataType &elem_type, uint32_t &depth, bool &is_vec);
  static int get_array_type_by_subschema_id(ObEvalCtx &ctx, const uint16_t subschema_id, ObCollectionArrayType *&arr_type);
  static int get_coll_type_by_subschema_id(ObExecContext *exec_ctx, const uint16_t subschema_id, ObCollectionTypeBase *&coll_type);
  static int construct_array_obj(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t subschema_id, ObIArrayType *&res, bool read_only = true);
  static int get_array_obj(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t subschema_id, const ObString &raw_data, ObIArrayType *&res);
  static int add_elem_to_array(const ObExpr &expr, ObEvalCtx &ctx, ObIAllocator &alloc,
                               ObCollectionArrayType *value_type,  ObIArrayType *value_arr, int args_idx);
  static int add_elem_to_nested_array(ObIAllocator &tmp_allocator, ObEvalCtx &ctx, uint16_t subschema_id,
                                      const ObDatum &datum, ObArrayNested *nest_array);
  static int get_child_subschema_id(ObExecContext *exec_ctx, uint16_t subid, uint16_t &child_subid);
  static int calc_collection_hash_val(const ObObjMeta &meta, const void *data, ObLength len, hash_algo hash_func, uint64_t seed, uint64_t &hash_val);
  static int collection_compare(const ObObjMeta &l_meta, const ObObjMeta &r_meta,
                                const void *l_v, const ObLength l_len,
                                const void *r_v, const ObLength r_len,
                                int &cmp_ret);
  // collection object is read only
  static int get_collection_obj(ObEvalCtx &ctx, const uint16_t subschema_id, ObIArrayType *&res);
  template <typename T1, typename T>
  static int calc_array_sum_by_type(uint32_t data_len, uint32_t len, const char *data_ptr,
                                    uint8_t *null_bitmaps, T &sum)
  {
    int ret = OB_SUCCESS;
    if (data_len / sizeof(T1) != len) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "unexpected array length", K(ret), K(len), K(data_len));
    } else {
      T1 *data = reinterpret_cast<T1 *>(const_cast<char *>(data_ptr));
      for (uint32_t i = 0; i < len; ++i) {
        if (null_bitmaps != nullptr && null_bitmaps[i] > 0) {
          /* do nothing */
        } else if (OB_FAIL(raw_check_add<T>(sum + data[i], static_cast<T>(data[i]), sum))) {
          LOG_WARN("array_sum overflow", K(ret), K(sum), K(data[i]));
          break;
        } else {
          sum += static_cast<T>(data[i]);
        }
      }
    }
    return ret;
  }

  template <typename T>
  static int calc_array_sum(uint32_t len, uint8_t *nullbitmaps, const char *data_ptr,
                            uint32_t data_len, ObCollectionArrayType *arr_type, T &sum)
  {
    int ret = OB_SUCCESS;

    ObCollectionBasicType *elem_type = NULL;
    if (OB_ISNULL(elem_type = static_cast<ObCollectionBasicType *>(arr_type->element_type_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("source array collection element type is null", K(ret));
    } else if (arr_type->element_type_->type_id_ != ObNestedType::OB_BASIC_TYPE) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported element type", K(ret), K(arr_type->element_type_->type_id_));
    } else {
      ObObjType obj_type = elem_type->basic_meta_.get_obj_type();
      switch (obj_type) {
      case ObTinyIntType: {
        ret = calc_array_sum_by_type<int8_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObSmallIntType: {
        ret = calc_array_sum_by_type<int16_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObInt32Type: {
        ret = calc_array_sum_by_type<int32_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObIntType: {
        ret = calc_array_sum_by_type<int64_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObUTinyIntType: {
        ret = calc_array_sum_by_type<uint8_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObUSmallIntType: {
        ret = calc_array_sum_by_type<uint16_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObUInt32Type: {
        ret = calc_array_sum_by_type<uint32_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObUInt64Type: {
        ret = calc_array_sum_by_type<uint64_t>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObUFloatType:
      case ObFloatType: {
        ret = calc_array_sum_by_type<float>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      case ObUDoubleType:
      case ObDoubleType: {
        ret = calc_array_sum_by_type<double>(data_len, len, data_ptr, nullbitmaps, sum);
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        OB_LOG(WARN, "not supported element type", K(ret), K(elem_type->basic_meta_.get_type_class()));
      }
      } // end switch
    }

    return ret;
  }

  static int get_array_data(ObString &data_str, ObCollectionArrayType *arr_type, uint32_t &len,
                            uint8_t *&null_bitmaps, const char *&data, uint32_t &data_len);
  static int get_array_data(ObIVector *len_vec, ObIVector *nullbitmap_vec, ObIVector *data_vec,
                            int64_t idx, ObCollectionArrayType *arr_type, uint32_t &len,
                            uint8_t *&null_bitmaps, const char *&data, uint32_t &data_len);

  static int convert_to_string(common::ObIAllocator &allocator, ObEvalCtx &ctx, const uint16_t subschema_id, const common::ObString &data, ObString &res_str);
  // for vector
  static int get_type_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             ObIAllocator &allocator,
                             ObIArrayType *&result,
                             bool &is_null);
  static int get_type_vector(const ObExpr &expr,
                             const ObDatum &datum,
                             ObEvalCtx &ctx,
                             ObIAllocator &allocator,
                             ObIArrayType *&result);
  static int calc_cast_type(const ObExprOperatorType &expr_type, ObExprResType &type, common::ObExprTypeCtx &type_ctx, const bool only_vector = false);
  static int calc_cast_type2(const ObExprOperatorType &expr_type, ObExprResType &type1, ObExprResType &type2, common::ObExprTypeCtx &type_ctx, uint16_t &res_subschema_id,
                             const bool only_vector = false);
  static int collect_vector_cast_info(ObExprResType &type, ObExecContext &exec_ctx, ObVectorCastInfo &info);
  static bool is_sparse_vector_supported(const ObExprOperatorType &type) {
    return type == T_FUN_SYS_INNER_PRODUCT ||
           type == T_FUN_SYS_NEGATIVE_INNER_PRODUCT ||
           type == T_FUN_SYS_VECTOR_DIMS;
  };

  // update inplace
  static int vector_datum_add(ObDatum &res, const ObDatum &data, ObIAllocator &allocator, ObDatum *tmp_res = nullptr, bool negative = false);
  static int get_basic_elem(ObIArrayType *src, uint32_t idx, ObObj &elem_obj, bool &is_null);
  // check
  template<typename T>
  static int raw_check_add(const T &res, const T &l, const T &r);

  template<typename T>
  static int raw_check_minus(const T &res, const T &l, const T &r);
  template <typename T>
  static int calc_fixed_size_key_index(ObIArrayType *src_key_arr, uint32_t *idx_arr, uint32_t &idx_count);
  static int calc_string_key_index(ObIArrayType *src_key_arr, uint32_t *idx_arr, uint32_t &idx_count);

private:
  static const char* DEFAULT_CAST_TYPE_NAME;
  static const ObString DEFAULT_CAST_TYPE_STR;
  static int get_collection_raw_data(ObIAllocator &allocator, const ObObjMeta &meta, const void *data, ObLength len, ObString &bin_str);
};

struct ObVectorArithFunc
{
  enum ArithType
  {
    ADD = 0,
    MINUS,
    MUL,
    DIV,
  };
};

struct ObVectorVectorArithFunc : public ObVectorArithFunc
{

  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r, const ObExpr &expr, ObEvalCtx &ctx, ArithType type) const;
};

struct ObVectorElemArithFunc : public ObVectorArithFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r, const ObExpr &expr, ObEvalCtx &ctx, ArithType type) const;
};

class ObNestedVectorFunc
{
public:
  static int construct_param(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t meta_id,
                              ObString &str_data, ObIArrayType *&param_obj);

};

class ObCollectionExprUtil
{
private:
  // using ATTR0_FMT = ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>;
public:
  OB_INLINE static bool is_compact_fmt_cell(const void *ptr)
  {
    OB_ASSERT(ptr != nullptr);
    // uniform cell is a lob data, first uint32_t is version of lob, must >= 1
    // for discrete/continous cell, we set first uint32_t to 0
    return (reinterpret_cast<const uint32_t *>(ptr))[0] >= 1;
  }

  OB_INLINE static bool is_vector_fmt_cell(const void *ptr)
  {
    OB_ASSERT(ptr != nullptr);
    return (reinterpret_cast<const uint32_t *>(ptr))[0] == 0;
  }

};


} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_ARRAY_EXPR_UTILS_H_
