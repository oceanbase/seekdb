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

#include "sql/engine/expr/ob_expr_des_hex_str.h"
#include "common/datum/datum_value.h"
#include "common/number/ob_number_v2.h"
#include "common/ob_action_flag.h"
#include "lib/utility/serialization.h"
#include <type_traits>

namespace oceanbase
{
  using namespace oceanbase::common;
namespace sql
{
namespace {
using common::DatumValue;
using common::SqlTypeMeta;
using common::SqlValueType;
using common::ObIAllocator;

template <typename T>
int decode_scalar(const char *buf, int64_t len, int64_t &pos,
                  SqlValueType type, const SqlTypeMeta &meta,
                  ObIAllocator &allocator, DatumValue &value)
{
  int ret = OB_SUCCESS;
  T decoded{};
  if (OB_FAIL(serialization::decode(buf, len, pos, decoded))) {
  } else if constexpr (std::is_same<T, float>::value) {
    ret = value.set_float(decoded, allocator);
  } else if constexpr (std::is_same<T, double>::value) {
    ret = value.set_double(type, static_cast<double>(decoded), allocator);
  } else if (type == ObDateType) {
    ret = value.set_date(static_cast<int32_t>(decoded), allocator);
  } else if (type == ObMySQLDateType) {
    ret = value.set_mysql_date(static_cast<int32_t>(decoded), allocator);
  } else if (type == ObMySQLDateTimeType) {
    ret = value.set_mysql_datetime(static_cast<int64_t>(decoded), allocator);
  } else if (type == ObDateTimeType) {
    ret = value.set_datetime(static_cast<int64_t>(decoded), allocator);
  } else if (type == ObTimestampType) {
    ret = value.set_timestamp(static_cast<int64_t>(decoded), allocator);
  } else if (type == ObTimeType) {
    ret = value.set_time(static_cast<int64_t>(decoded), allocator);
  } else if (type == ObYearType) {
    ret = value.set_year(static_cast<uint8_t>(decoded), allocator);
  } else if constexpr (std::is_unsigned<T>::value) {
    ret = value.set_uint(type, static_cast<uint64_t>(decoded), allocator);
  } else {
    ret = value.set_int(type, static_cast<int64_t>(decoded), allocator);
  }
  if (OB_SUCC(ret)) { value.set_meta_type(meta); }
  return ret;
}

int decode_legacy_value(const char *buf, int64_t len, int64_t &pos,
                        ObIAllocator &allocator, DatumValue &value)
{
  int ret = OB_SUCCESS;
  SqlTypeMeta meta;
  if (OB_FAIL(serialization::decode(buf, len, pos, meta))) {
  } else if (meta.is_invalid()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    value.reset();
    switch (meta.get_type()) {
      case ObNullType:
        value.set_null();
        value.set_meta_type(meta);
        break;
      case ObTinyIntType: ret = decode_scalar<int8_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObSmallIntType: ret = decode_scalar<int16_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObMediumIntType:
      case ObInt32Type: ret = decode_scalar<int32_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObIntType: ret = decode_scalar<int64_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObUTinyIntType: ret = decode_scalar<uint8_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObUSmallIntType: ret = decode_scalar<uint16_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObUMediumIntType:
      case ObUInt32Type: ret = decode_scalar<uint32_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObUInt64Type: ret = decode_scalar<uint64_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObFloatType:
      case ObUFloatType: ret = decode_scalar<float>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObDoubleType:
      case ObUDoubleType: ret = decode_scalar<double>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObDateTimeType:
      case ObTimestampType:
      case ObTimeType: ret = decode_scalar<int64_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObDateType: ret = decode_scalar<int32_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObYearType: ret = decode_scalar<uint8_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObMySQLDateType: ret = decode_scalar<int32_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObMySQLDateTimeType: ret = decode_scalar<int64_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObEnumType:
      case ObSetType:
      case ObBitType: ret = decode_scalar<uint64_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObUnknownType: ret = decode_scalar<int64_t>(buf, len, pos, meta.get_type(), meta, allocator, value); break;
      case ObNumberType:
      case ObUNumberType: {
        ObNumberDesc desc{};
        uint32_t *digits = nullptr;
        if (OB_FAIL(serialization::decode_number_type(buf, len, pos, desc, digits))) {
        } else {
          desc.reserved_ = 0;
          desc.flag_ = 0;
          const int64_t bytes_len = sizeof(desc) + sizeof(uint32_t) * desc.len_;
          char *bytes = static_cast<char *>(allocator.alloc(bytes_len));
          if (OB_ISNULL(bytes)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            MEMCPY(bytes, &desc, sizeof(desc));
            if (desc.len_ > 0) { MEMCPY(bytes + sizeof(desc), digits, sizeof(uint32_t) * desc.len_); }
            value.datum_.set_string(bytes, bytes_len);
            value.set_meta_type(meta);
          }
        }
        break;
      }
      case ObVarcharType:
      case ObCharType:
      case ObHexStringType:
      case ObTinyTextType:
      case ObTextType:
      case ObMediumTextType:
      case ObLongTextType:
      case ObEnumInnerType:
      case ObSetInnerType:
      case ObJsonType:
      case ObGeometryType: {
        ObString string;
        if (OB_FAIL(serialization::decode(buf, len, pos, string))) {
        } else if (OB_FAIL(value.set_string(meta.get_type(), string, allocator))) {
        } else { value.set_meta_type(meta); }
        break;
      }
      case ObUserDefinedSQLType:
      case ObCollectionSQLType: {
        uint16_t subschema_id = 0;
        uint8_t flags = 0;
        ObString string;
        if (OB_FAIL(serialization::decode(buf, len, pos, subschema_id))) {
        } else if (OB_FAIL(serialization::decode(buf, len, pos, flags))) {
        } else if (OB_FAIL(serialization::decode(buf, len, pos, string))) {
        } else if (OB_FAIL(value.set_string(meta.get_type(), string, allocator))) {
        } else {
          meta.set_subschema_id(subschema_id);
          meta.set_udt_flags(flags);
          value.set_meta_type(meta);
        }
        break;
      }
      case ObDecimalIntType: {
        int32_t bytes_len = 0;
        ObScale scale = meta.get_scale();
        if (OB_FAIL(serialization::decode(buf, len, pos, bytes_len))) {
        } else if (OB_FAIL(serialization::decode(buf, len, pos, scale))) {
        } else if (bytes_len < 0 || bytes_len > len - pos) {
          ret = OB_DESERIALIZE_ERROR;
        } else {
          const ObDecimalInt *decint = bytes_len > 0
              ? reinterpret_cast<const ObDecimalInt *>(buf + pos) : nullptr;
          ret = value.set_decimal_int(bytes_len, scale, decint, allocator);
          if (OB_SUCC(ret)) {
            pos += bytes_len;
            meta.set_decimal_int(scale);
            value.set_meta_type(meta);
          }
        }
        break;
      }
      case ObExtendType: {
        int64_t ext = 0;
        if (OB_FAIL(serialization::decode(buf, len, pos, ext))) {
        } else if (meta.is_ext_sql_array()) {
          int64_t body_len = 0;
          if (ext != 0) {
            ret = OB_NOT_SUPPORTED;
          } else if (OB_FAIL(serialization::decode(buf, len, pos, body_len))) {
          } else if (body_len <= 0 || body_len > INT32_MAX || body_len > len - pos) {
            ret = OB_DESERIALIZE_ERROR;
          } else {
            // The legacy Ext literal printer rejects SQL-array values. Keep
            // that result while validating/skipping its declared payload.
            pos += body_len;
            ret = OB_INVALID_ARGUMENT;
          }
        } else if (!((ObActionFlag::OP_MIN_OBJ <= ext && ext <= ObActionFlag::OP_LOCK_ROW)
                     || ext == static_cast<int64_t>(UINT64_MAX - 2)
                     || ext == static_cast<int64_t>(UINT64_MAX - 1)) && ext != 0) {
          ret = OB_NOT_SUPPORTED;
        } else {
          if (OB_FAIL(value.set_extend(ext, meta.get_extend_type(), allocator))) {
          } else { value.set_meta_type(meta); }
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        break;
    }
  }
  return ret;
}
} // namespace

ObExprDesHexStr::ObExprDesHexStr(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_DES_HEX_STR, N_DES_HEX_STR, 1, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

int ObExprDesHexStr::calc_result_type1(ObExprResType &type, ObExprResType &type1,
                                      common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = common::OB_SUCCESS;

  if (!type1.is_varchar()) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    type.set_varchar();
    type.set_collation_level(type1.get_collation_level());
    type.set_collation_type(type1.get_collation_type());
    type.set_scale(type1.get_scale());
  }

  return ret;
}

int ObExprDesHexStr::deserialize_hex_cstr(const char *buf,
                                          int64_t buf_len,
                                          common::ObIAllocator &allocator,
                                          common::DatumValue &value)
{
  int ret = common::OB_SUCCESS;

  int64_t pos = 0;
  int64_t ret_len = 0;
  char *res_buf = NULL;
  if (OB_ISNULL(buf) || buf_len < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (NULL == (res_buf = static_cast<char*>(allocator.alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to allocate memory for res_buf.", K(ret), K(buf_len));
  } else if (buf_len != (ret_len = common::str_to_hex(buf,
                                                      static_cast<int32_t>(buf_len),
                                                      res_buf,
                                                      static_cast<int32_t>(buf_len)))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(decode_legacy_value(res_buf, ret_len / 2, pos, allocator, value))) {
  }

  return ret;
}

int ObExprDesHexStr::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &expr) const
{
  int ret = OB_SUCCESS;
  CK(1 == expr.arg_cnt_);
  expr.eval_func_ = eval_des_hex_str;
  return ret;
}

int ObExprDesHexStr::eval_des_hex_str(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *in= NULL;
  ObEvalCtx::TempAllocGuard alloc_guard(ctx);
  ObIAllocator &alloc = alloc_guard.get_allocator();
  DatumValue value;
  if (OB_FAIL(expr.eval_param_value(ctx, in))) {
  } else if (in->is_null()) {
    expr_datum.set_null();
  } else if (OB_FAIL(deserialize_hex_cstr(in->ptr_, in->len_, alloc, value))) {
  } else {
    const int64_t len = std::max(128, in->len_ * 2);
    char *buf = expr.get_str_res_mem(ctx, len);
    int64_t pos = 0;
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(value.print_plain_str_literal(buf, len, pos))) {
    } else {
      expr_datum.set_string(buf, pos);
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
