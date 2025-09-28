/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "common/cell/ob_cell_reader.h"

namespace oceanbase
{
namespace common
{
#define READ_NULL(object) \
  { \
    object.set_null(); \
  }
#define READ_COMMON(obj_set_fun, obj_set_type, medium_type, object) \
  { \
    const medium_type *value = 0; \
    if (OB_FAIL(read<medium_type>(value))) { \
      COMMON_LOG(WARN, "cell reader fail to read value.", K(ret)); \
    } else { \
      object.obj_set_fun(static_cast<obj_set_type>(*value)); \
    } \
  }
#define READ_INT(obj_set_fun, obj_set_type, object) \
  { \
    switch (meta->attr_) { \
      case 0: \
        READ_COMMON(obj_set_fun, obj_set_type, int8_t, object); \
        break; \
      case 1: \
        READ_COMMON(obj_set_fun, obj_set_type, int16_t, object); \
        break; \
      case 2: \
        READ_COMMON(obj_set_fun, obj_set_type, int32_t, object); \
        break; \
      case 3: \
        READ_COMMON(obj_set_fun, obj_set_type, int64_t, object); \
        break; \
      default: \
        ret = OB_NOT_SUPPORTED; \
        COMMON_LOG(WARN, "not supported type, ", K(ret), "attr", meta->attr_); \
    } \
  }
#define READ_NUMBER(obj_set_fun, obj_set_type, object) \
  { \
    number::ObNumber value; \
    if (OB_FAIL(value.decode(buf_, buf_size_, pos_))) { \
      COMMON_LOG(WARN, "decode number failed", K(ret)); \
    } else { \
      object.obj_set_fun(value); \
    } \
  }

#define READ_CHAR(obj_set_fun, object) \
  { \
    const uint32_t *len = NULL; \
    ObString value; \
    if (0 == meta->attr_) { \
      object.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI); \
    } else if (1 == meta->attr_) { \
      const uint8_t *collation_type = NULL; \
      if (OB_FAIL(read<uint8_t>(collation_type))) { \
        COMMON_LOG(WARN, "row reader fail to read collation_type.", K(ret)); \
      } else { \
        object.set_collation_type(static_cast<ObCollationType>(*collation_type)); \
      } \
    } \
    if (OB_SUCC(ret)) { \
      if (OB_FAIL(read<uint32_t>(len))) { \
        COMMON_LOG(WARN, "row reader fail to read value.", K(ret)); \
      } else if (pos_ + *len > buf_size_) { \
        ret = OB_BUF_NOT_ENOUGH; \
        COMMON_LOG(WARN, "buffer not enough, ", K(ret), K_(pos), K_(buf_size), "length", *len); \
      } else { \
        value.assign_ptr((char*)(buf_ + pos_), *len); \
        pos_ += *len; \
        object.obj_set_fun(value); \
      } \
    } \
  }

int ObCellReader::read_decimal_int(ObObj &obj)
{
  int ret = OB_SUCCESS;
  const uint32_t *len = NULL;
  const int16_t *scale = NULL;
  if (OB_FAIL(read<int16_t>(scale))) {
    COMMON_LOG(WARN, "failed to read uint32_t", K(ret));
  } else if (OB_FAIL(read<uint32_t>(len))) {
    COMMON_LOG(WARN, "failed to read int16_t", K(ret));
  } else if (OB_UNLIKELY(pos_ + *len > buf_size_)) {
    ret = OB_BUF_NOT_ENOUGH;
    COMMON_LOG(WARN, "buffer not enough", K(ret), K(pos_), K(*len), K(buf_size_));
  } else {
    obj.set_decimal_int(*len, *scale, (ObDecimalInt *)(buf_ + pos_));
    pos_ += *len;
  }
  return ret;
}

#define READ_TEXT(obj_type, object) \
  { \
    const uint32_t *len = NULL; \
    ObString value; \
    if (!meta->need_collation()) { \
      object.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI); \
    } else { \
      const uint8_t *collation_type = NULL; \
      if (OB_FAIL(read<uint8_t>(collation_type))) { \
        COMMON_LOG(WARN, "row reader fail to read collation_type.", K(ret)); \
      } else { \
        object.set_collation_type(static_cast<ObCollationType>(*collation_type)); \
      } \
    } \
    bool has_header = false; \
    if (OB_SUCC(ret) && !meta->is_varchar_text()) {\
      const uint8_t *scale = NULL;\
      const uint8_t *version = NULL;\
      if (OB_FAIL(read<uint8_t>(scale))) { \
        COMMON_LOG(WARN, "row reader fail to read scale.", K(ret)); \
      } else if (OB_FAIL(read<uint8_t>(version))) { \
        COMMON_LOG(WARN, "row reader fail to read scale.", K(ret)); \
      } else { \
        object.set_scale(static_cast<ObScale>(*scale));\
        ObLobScale lob_scale(*scale);\
        if (!lob_scale.is_in_row() || !lob_scale.has_lob_header()) { \
          COMMON_LOG(WARN, "Unexpected lob scale", K(*version), K(*scale), K(ret)); \
        }\
        if (lob_scale.has_lob_header()) { \
          has_header = true; \
        } \
        if (TEXT_CELL_META_VERSION != *version) { \
          COMMON_LOG(WARN, "Unexpected lob version", K(*version), K(*scale), K(ret)); \
        } \
      } \
    } \
    if (OB_SUCC(ret)) { \
      if (OB_FAIL(read<uint32_t>(len))) { \
        COMMON_LOG(WARN, "row reader fail to read value.", K(ret)); \
      } else if (pos_ + *len > buf_size_) { \
        ret = OB_BUF_NOT_ENOUGH; \
        COMMON_LOG(WARN, "buffer not enough, ", K(ret), K_(pos), K_(buf_size), "length", *len); \
      } else { \
        object.set_lob_value(obj_type, (char*)(buf_ + pos_), *len);\
        if (has_header) { \
          object.set_has_lob_header(); \
        } \
        pos_ += *len; \
      } \
    } \
  }

#define READ_BINARY(obj_set_fun, object) \
  { \
    const int32_t *len = NULL; \
    ObString value; \
    if (OB_FAIL(read<int32_t>(len))) { \
      COMMON_LOG(WARN, "row reader fail to read value.", K(ret)); \
    } else if (pos_ + *len > buf_size_) { \
      ret = OB_BUF_NOT_ENOUGH; \
      COMMON_LOG(WARN, "buffer not enough, ", K(ret), K_(pos), K_(buf_size), "length", *len); \
    } else { \
      value.assign_ptr((char*)(buf_ + pos_), *len); \
      pos_ += *len; \
      object.obj_set_fun(value); \
    } \
  }
#define READ_EXTEND(object) \
  { \
    if (ObCellWriter::EA_END_FLAG == meta->attr_) { \
      object.set_ext(ObActionFlag::OP_END_FLAG); \
    } else if (ObCellWriter::EA_OTHER == meta->attr_) { \
      const int8_t *value = NULL; \
      if (OB_FAIL(read<int8_t>(value))) { \
        COMMON_LOG(WARN, "cell reader fail to read extend value.", K(ret)); \
      } else { \
        switch (*value) { \
          case ObCellWriter::EV_NOP_ROW: \
            object.set_ext(ObActionFlag::OP_NOP); \
            break; \
          case ObCellWriter::EV_DEL_ROW: \
            object.set_ext(ObActionFlag::OP_DEL_ROW); \
            break; \
          case ObCellWriter::EV_LOCK_ROW: \
            obj_.set_ext(ObActionFlag::OP_LOCK_ROW); \
            break; \
          case ObCellWriter::EV_NOT_EXIST_ROW: \
            object.set_ext(ObActionFlag::OP_ROW_DOES_NOT_EXIST); \
            break; \
          case ObCellWriter::EV_MIN_CELL: \
            object.set_min_value(); \
            break; \
          case ObCellWriter::EV_MAX_CELL: \
            object.set_max_value(); \
            break; \
          default: \
            ret = OB_NOT_SUPPORTED; \
            COMMON_LOG(WARN, "invalid extend value.", K(ret), K(value)); \
        } \
      } \
    } else { \
      ret = OB_NOT_SUPPORTED; \
      COMMON_LOG(WARN, "invalid extend attr.", K(ret), "attr", meta->attr_); \
    } \
  }

ObCellReader::ObCellReader()
    :buf_(NULL),
     buf_size_(0),
     pos_(0),
     column_id_(0),
     obj_(),
     store_type_(INVALID_COMPACT_STORE_TYPE),
     row_start_(0),
     is_inited_(false)
{
}
int ObCellReader::init(const char *buf, int64_t buf_size, ObCompactStoreType store_type)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "ObCellReader has inited, ", K(ret));
  } else if (NULL == buf || buf_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid cell reader input argeument.", K(ret), KP(buf), K(buf_size));
  } else if (SPARSE != store_type && DENSE != store_type) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid cell reader input argument.", K(ret), K(store_type));
  } else {
    buf_ = buf;
    buf_size_ = buf_size;
    pos_ = 0;
    column_id_ = 0;
    //obj_
    store_type_ = store_type;
    row_start_ = 0;
    is_inited_ = true;
  }
  return ret;
}
int ObCellReader::next_cell()
{
  int ret = OB_SUCCESS;
  int64_t old_pos = pos_;
  column_id_ = OB_INVALID_ID;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (SPARSE == store_type_) {
    if (OB_FAIL(parse(&column_id_))) {
      COMMON_LOG(WARN, "cell reader fail to parse.", K(ret));
    }
  } else if (DENSE == store_type_) {
    if (OB_FAIL(parse(NULL))) {
      COMMON_LOG(WARN, "cell reader fail to parse.", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    COMMON_LOG(WARN, "invalid store type.", K(ret), K_(store_type));
  }
  if (OB_SUCCESS != ret && OB_ITER_END != ret) {
    pos_ = old_pos;
  }
  return ret;
}
int ObCellReader::get_cell(uint64_t &column_id, ObObj &obj, bool *is_row_finished, ObString *row)
{
  int ret = OB_SUCCESS;
  const ObObj *cell = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
     COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (OB_FAIL(get_cell(column_id, cell, is_row_finished, row))) {
    COMMON_LOG(WARN, "get cell fail", K(ret));
  } else {
    obj = *cell;
  }
  return ret;
}
int ObCellReader::get_cell(const ObObj *&obj, bool *is_row_finished, ObString *row)
{
  int ret = OB_SUCCESS;
  bool is_end_obj = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (OB_FAIL(is_es_end_object(obj_, is_end_obj))) {
    COMMON_LOG(WARN, "fail to get is_end_obj, ", K(ret), K_(obj));
  } else {
    obj = &obj_;
    if (NULL != is_row_finished) {
      *is_row_finished = is_end_obj;
    }
    if (NULL != row && is_end_obj) {
      row->assign_ptr(
          const_cast<char *>(buf_ + row_start_), static_cast<int32_t>(pos_ - row_start_));
      row_start_ = pos_;
    }
  }
  return ret;
}
int ObCellReader::get_cell(uint64_t &column_id,
                           const ObObj *&obj,
                           bool *is_row_finished,
                           ObString *row)
{
  int ret = OB_SUCCESS;
  bool is_end_obj = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (OB_FAIL(is_es_end_object(obj_, is_end_obj))) {
    COMMON_LOG(WARN, "fail to get is_end_obj, ", K(ret), K_(obj));
  } else {
    obj = &obj_;
    column_id = column_id_;
    if (NULL != is_row_finished) {
      *is_row_finished = is_end_obj;
    }
    if (NULL != row && is_end_obj) {
      row->assign_ptr(
          const_cast<char *>(buf_ + row_start_), static_cast<int32_t>(pos_ - row_start_));
      row_start_ = pos_;
    }
  }
  return ret;
}

int ObCellReader::parse(uint64_t *column_id)
{
  int ret = OB_SUCCESS;
  const ObCellWriter::CellMeta *meta = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (OB_FAIL(read<ObCellWriter::CellMeta>(meta))) {
    COMMON_LOG(WARN, "cell reader fail to read meta, ", K(ret));
  } else {
    switch (meta->type_) {
      case ObNullType:
        READ_NULL(obj_);
        break;
      case ObTinyIntType:
        READ_INT(set_tinyint, int8_t, obj_);
        break;
      case ObSmallIntType:
        READ_INT(set_smallint, int16_t, obj_);
        break;
      case ObMediumIntType:
        READ_INT(set_mediumint, int32_t, obj_);
        break;
      case ObInt32Type:
        READ_INT(set_int32, int32_t, obj_);
        break;
      case ObIntType:
        READ_INT(set_int, int64_t, obj_);
        break;
      case ObUTinyIntType:
        READ_INT(set_utinyint, uint8_t, obj_);
        break;
      case ObUSmallIntType:
        READ_INT(set_usmallint, uint16_t, obj_);
        break;
      case ObUMediumIntType:
        READ_INT(set_umediumint, uint32_t, obj_);
        break;
      case ObUInt32Type:
        READ_INT(set_uint32, uint32_t, obj_);
        break;
      case ObUInt64Type:
        READ_INT(set_uint64, uint64_t, obj_);
        break;
      case ObFloatType:
        READ_COMMON(set_float, float, float, obj_);
        break;
      case ObUFloatType:
        READ_COMMON(set_ufloat, float, float, obj_);
        break;
      case ObDoubleType:
        READ_COMMON(set_double, double, double, obj_);
        break;
      case ObUDoubleType:
        READ_COMMON(set_udouble, double, double, obj_);
        break;
      case ObNumberType:
        READ_NUMBER(set_number, ObNumber, obj_);
        break;
      case ObUNumberType:
        READ_NUMBER(set_unumber, ObNumber, obj_);
        break;
      case ObDateTimeType:
        READ_COMMON(set_datetime, int64_t, int64_t, obj_);
        break;
      case ObTimestampType:
        READ_COMMON(set_timestamp, int64_t, int64_t, obj_);
        break;
      case ObDateType:
        READ_COMMON(set_date, int32_t, int32_t, obj_);
        break;
      case ObTimeType:
        READ_COMMON(set_time, int64_t, int64_t, obj_);
        break;
      case ObYearType:
        READ_COMMON(set_year, uint8_t, uint8_t, obj_);
        break;
      case ObVarcharType:
        READ_CHAR(set_varchar, obj_);
        break;
      case ObCharType:
        READ_CHAR(set_char, obj_);
        break;
      case ObHexStringType:
        READ_BINARY(set_hex_string, obj_);
        break;
      case ObExtendType:
        READ_EXTEND(obj_);
        break;
      case ObTinyTextType:
      case ObTextType:
      case ObMediumTextType:
      case ObLongTextType:
      case ObJsonType:
      case ObGeometryType:
      case ObCollectionSQLType:
        READ_TEXT(static_cast<ObObjType>(meta->type_), obj_);
        break;
      case ObBitType:
        READ_INT(set_bit, uint64_t, obj_);
        break;
      case ObEnumType:
        READ_INT(set_enum, uint64_t, obj_);
        break;
      case ObSetType:
        READ_INT(set_set, uint64_t, obj_);
        break;
      case ObDecimalIntType: {
        ret = read_decimal_int(obj_);
        break;
      }
      case ObMySQLDateType:
        READ_COMMON(set_mysql_date, int32_t, int32_t, obj_);
        break;
      case ObMySQLDateTimeType:
        READ_COMMON(set_mysql_datetime, int64_t, int64_t, obj_);
        break;
      default:
        ret = OB_NOT_SUPPORTED;
        COMMON_LOG(WARN, "not supported type.", K(ret), "type", meta->type_);
    }
  }
  if (OB_SUCCESS == ret && NULL != column_id) {
    if (ObExtendType != meta->type_ || obj_.is_min_value() || obj_.is_max_value()) {
      const uint32_t *tmp_column_id = NULL;
      if (OB_FAIL(read<uint32_t>(tmp_column_id))) {
        COMMON_LOG(WARN, "cell reader fail to read column id.", K(ret));
      } else {
        *column_id = *tmp_column_id;
      }
    } else {
      *column_id = OB_INVALID_ID;
    }
  }
  return ret;
}

template<class T>
int ObCellReader::read(const T *&ptr)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (pos_ + (static_cast<int64_t>(sizeof(T))) > buf_size_) {
    ret = OB_BUF_NOT_ENOUGH;
    COMMON_LOG(WARN, "buffer not enough, ", K(ret));
  } else {
    ptr = reinterpret_cast<const T *>(buf_ + pos_);
    pos_ += sizeof(T);
  }
  return ret;
}

inline int ObCellReader::is_es_end_object(const common::ObObj &obj, bool &is_end_obj)
{
  int ret = OB_SUCCESS;
  is_end_obj = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObCellReader should be inited first, ", K(ret));
  } else if (!obj.is_valid_type()) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid argument, ", K(ret), "type", obj.get_type());
  } else if (common::ObExtendType == obj.get_type()
             && common::ObActionFlag::OP_END_FLAG == obj.get_ext()) {
    is_end_obj = true;
  }

  return ret;
}


}//end namespace common
}//end namespace oceanbase
