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

// ObMySQLProcStatement(stored procedure execution)has a deep dependency on share/schema(ObRoutineInfo)+pl(ObPLUserType),
// it belongs to the DB/PL layer, so it lives in src; class declaration remains in oblib ob_mysql_prepared_statement.h(already forward-declared)。
#define USING_LOG_PREFIX LIB_MYSQLC
#include <mysql.h>
#include <stddef.h>
#include <stdint.h>
#include <utility>

#include "common/mysqlclient/ob_mysql_prepared_statement.h"
#include "share/schema/ob_routine_info.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_iarray.h"
#include "lib/ob_check_macros.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/string/ob_string.h"
#include "lib/utility/utility.h"
#include "mariadb_com.h"
#include "mysql.h"
#include "mysqlclient/ob_mysql_connection.h"
#include "mysqlclient/ob_mysql_global.h"
#include "mysqlclient/ob_mysql_prepared_param.h"
#include "mysqlclient/ob_mysql_prepared_result.h"
#include "object/ob_obj_type.h"
#include "object/ob_object.h"

namespace oceanbase {
namespace common {
class ObTimeZoneInfo;
}  // namespace common
namespace pl {
class ObUserDefinedType;
}  // namespace pl
}  // namespace oceanbase

namespace oceanbase
{
namespace common
{
namespace sqlclient
{

// ObObjType -> MySQL field type mapping: this file was split from ob_mysql_prepared_statement(oblib->src),
// this array is a file-local static in prepared_statement.cpp and is not visible after the split; keep a local copy following the existing duplicate-static pattern。
static const obmysql::EMySQLFieldType ob_type_to_mysql_type[ObMaxType] =
{
  /* ObMinType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NULL,          /* ObNullType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TINY,          /* ObTinyIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_SHORT,         /* ObSmallIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_INT24,         /* ObMediumIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONG,          /* ObInt32Type */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONGLONG,      /* ObIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TINY,          /* ObUTinyIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_SHORT,         /* ObUSmallIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_INT24,         /* ObUMediumIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONG,          /* ObUInt32Type */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONGLONG,      /* ObUInt64Type */
  obmysql::EMySQLFieldType::MYSQL_TYPE_FLOAT,         /* ObFloatType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DOUBLE,        /* ObDoubleType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_FLOAT,         /* ObUFloatType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DOUBLE,        /* ObUDoubleType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,    /* ObNumberType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,    /* ObUNumberType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DATETIME,      /* ObDateTimeType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TIMESTAMP,     /* ObTimestampType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DATE,          /* ObDateType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TIME,          /* ObTimeType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_YEAR,          /* ObYearType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_VAR_STRING,    /* ObVarcharType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_STRING,        /* ObCharType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_VARCHAR,       /* ObHexStringType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,       /* ObExtendType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,   /* ObUnknownType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TINY_BLOB,     /* ObTinyTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_BLOB,          /* ObTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB,   /* ObMediumTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONG_BLOB,     /* ObLongTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_BIT,           /* ObBitType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_STRING,        /* ObEnumType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_STRING,        /* ObSetType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,   /* ObEnumInnerType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,   /* ObSetInnerType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_JSON,                              /* ObJsonType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_GEOMETRY,                          /* ObGeometryType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,                           /* ObUserDefinedSQLType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,                        /* ObDecimalIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,                           /* ObCollectionSQLType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_BLOB,                              /* ObRoaringBitmapType */
  /* ObMaxType */
};

int ObMySQLProcStatement::bind_param(const int64_t col_idx,
                                     const int64_t param_idx,
                                     const bool is_output_param,
                                     const ObTimeZoneInfo *tz_info,
                                     ObObj &param,
                                     const share::schema::ObRoutineInfo &routine_info,
                                     ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObBindParam *bind_param = nullptr;
  const ObObjType obj_type = param.get_type();
  const share::schema::ObRoutineParam *routine_param = NULL;
  if (param_idx >= routine_info.get_routine_params().count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("col_idx invalid", K(ret), K(param_idx));
  } else if (FALSE_IT(routine_param = routine_info.get_routine_params().at(param_idx))) {
  } else if (OB_ISNULL(routine_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("routine_param is NULL", K(ret), K(col_idx));
  } else {
    enum_field_types buffer_type = MAX_NO_FIELD_TYPES;
    if (param.is_null()) {
      buffer_type = static_cast<enum_field_types>(ob_type_to_mysql_type[routine_param->get_param_type().get_obj_type()]);
    } else {
      buffer_type = static_cast<enum_field_types>(ob_type_to_mysql_type[param.get_type()]);
    }
    if (OB_FAIL(get_bind_param_by_idx(col_idx, bind_param))) {
      LOG_WARN("fail to get bind param by idx", K(ret), K(col_idx));
    } else if (OB_ISNULL(bind_param)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get bind param by idx", K(ret), K(col_idx), K(stmt_param_count_));
    } else if (OB_FAIL(ObBindParamEncode::encode_map_[obj_type](col_idx,
                                                                is_output_param,
                                                                *tz_info,
                                                                param,
                                                                *bind_param,
                                                                allocator,
                                                                buffer_type))) {
      LOG_WARN("fail to encode param", K(ret));
    } else if (OB_FAIL(param_.bind_param(*bind_param))) {
      LOG_WARN("failed to bind param", K(ret), KPC(bind_param));
    }
  }
  return ret;
}

int ObMySQLProcStatement::bind_basic_type_by_pos(uint64_t position,
                                                 void *param_buffer,
                                                 int64_t param_size,
                                                 int32_t datatype,
                                                 int32_t &indicator,
                                                 bool is_out_param)
{
  int ret = OB_SUCCESS;
  ObBindParam *bind_param = NULL;
  if (OB_FAIL(get_bind_param_by_idx(position, bind_param))) {
    LOG_WARN("fail to get bind param by idx", K(ret), K(position));
  } else if (OB_ISNULL(bind_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get bind param by idx", K(ret), K(position), K(stmt_param_count_));
  } else {
    enum_field_types buffer_type = static_cast<enum_field_types>(ob_type_to_mysql_type[(datatype)]);
    bind_param->col_idx_ = position;
    bind_param->buffer_type_ = buffer_type;
    bind_param->buffer_ = param_buffer;
    bind_param->buffer_len_ = param_size;
    bind_param->is_null_ = 0;
    bind_param->length_ = param_size;
    if (OB_FAIL(in_out_map_.push_back(is_out_param))) {
      LOG_WARN("failed to push back", K(ret));
    } else if (OB_FAIL(param_.bind_param(*bind_param))) {
      LOG_WARN("failed tp bind param", K(ret), KPC(bind_param));
    } else if (stmt_param_count_ == position + 1) {
      if (OB_FAIL(param_.bind_param())) {
        LOG_WARN("failed to bind param", K(ret),
                 "info", mysql_stmt_error(stmt_), "info", mysql_error(conn_->get_handler()));
      }
    }
  }
  return ret;
}

int ObMySQLProcStatement::bind_array_type_by_pos(uint64_t position,
                                                 void *array,
                                                 int32_t *indicators,
                                                 int64_t ele_size,
                                                 int32_t ele_datatype,
                                                 uint64_t array_size,
                                                 uint32_t *out_valid_array_size)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObMySQLProcStatement::bind_proc_param(ObIAllocator &allocator,
                                          ParamStore &params,
                                          const share::schema::ObRoutineInfo &routine_info,
                                          const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                          common::ObIArray<std::pair<int64_t, int64_t>> &basic_out_param,
                                          const ObTimeZoneInfo *tz_info,
                                          ObObj *result,
                                          bool is_sql)
{
  int ret = OB_SUCCESS;
  bool has_complex_type = false;
  if (routine_info.is_function()) {
    const ObDataType *ret_type = routine_info.get_ret_type();
    if (OB_ISNULL(ret_type)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("return type is NULL", K(ret), K(routine_info));
    } else if (ob_is_extend(ret_type->get_obj_type())) {
      has_complex_type = true;
    }
  }
  int64_t start_idx = routine_info.get_param_start_idx();
  const share::schema::ObRoutineParam *r_param = NULL;
  for (int64_t param_idx = 0; OB_SUCC(ret) && !has_complex_type && param_idx < params.count(); ++param_idx) {
    if (OB_ISNULL(r_param = routine_info.get_routine_params().at(start_idx + param_idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("param is NULL", K(ret), K(param_idx), K(start_idx), K(routine_info));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (has_complex_type) {
    if (OB_FAIL(bind_proc_param_with_composite_type(allocator,
                                                    params,
                                                    routine_info,
                                                    udts,
                                                    tz_info,
                                                    result,
                                                    is_sql,
                                                    basic_out_param))) {
      LOG_WARN("bind parameters failed", K(ret));
    }
  } else {
    int64_t start_idx = routine_info.get_param_start_idx();
    if (routine_info.is_function() && !is_sql) {
      if (OB_ISNULL(result)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is NULL", K(ret));
      } else if (OB_FAIL(basic_out_param.push_back(std::make_pair(0, 0)))) {
        LOG_WARN("push back failed", K(ret));
      } else if (OB_FAIL(bind_param(0, 0, true, tz_info, *result, routine_info, allocator))) {
        LOG_WARN("failed to bind param", K(ret));
      }
    }
    int64_t start_pos = (routine_info.is_function() ? (is_sql ? 0 : 1) : 0);
    int64_t skip_cnt = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
      ObObjParam &param = params.at(i);
      const share::schema::ObRoutineParam *r_param = routine_info.get_routine_params().at(i + start_idx);
      bool is_output = false;
      if (OB_ISNULL(r_param)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param is null", K(ret), K(i));
      } else if (param.is_pl_mock_default_param()) {
        skip_cnt++;
      } else {
        is_output = r_param->is_out_sp_param() || r_param->is_inout_sp_param();
        if (is_output && OB_FAIL(basic_out_param.push_back(std::make_pair(i + start_pos - skip_cnt, i)))) {
          LOG_WARN("push back failed", K(ret), K(i));
        } else if (OB_FAIL(bind_param(i + start_pos - skip_cnt, i + (routine_info.is_function() ? 1 : 0), 
                                      is_output, tz_info, param, routine_info, allocator))) {
          LOG_WARN("failed to bind param", K(ret));
        }
      }
    }
    if (routine_info.is_function() && is_sql) {
      if (OB_ISNULL(result)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is NULL", K(ret));
      } else if (OB_FAIL(basic_out_param.push_back(std::make_pair(params.count() - skip_cnt, 0)))) {
        LOG_WARN("push back failed", K(ret));
      } else if (OB_FAIL(bind_param(params.count() - skip_cnt, 0, true, tz_info, *result, routine_info, allocator))) {
        LOG_WARN("failed to bind param", K(ret));
      }
    }
  }
  return ret;
}

int ObMySQLProcStatement::bind_proc_param_with_composite_type(
                                          ObIAllocator &allocator,
                                          ParamStore &params,
                                          const share::schema::ObRoutineInfo &routine_info,
                                          const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                          const ObTimeZoneInfo *tz_info,
                                          ObObj *result,
                                          bool is_sql,
                                          common::ObIArray<std::pair<int64_t, int64_t>> &basic_out_param)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObMySQLProcStatement::convert_proc_output_param_result(int64_t out_param_idx,
                                                           const ObTimeZoneInfo &tz_info,
                                                           const ObBindParam &bind_param,
                                                           ObObj *param,
                                                           const share::schema::ObRoutineInfo &routine_info,
                                                           ObIAllocator &allocator,
                                                           bool is_return_value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param is NULL", K(ret));
  } else {
    if (bind_param.is_null_) {
      param->set_null();
    } else {
      ObObjType obj_type = ObNullType;
      const share::schema::ObRoutineParam *routine_param = routine_info.get_routine_params().at(out_param_idx);
      if (OB_ISNULL(routine_param)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("routine_param is NULL", K(ret), K(out_param_idx));
      } else {
        const ObDataType &data_type = routine_param->get_param_type();
        if (param->is_null()) {
          param->set_meta_type(data_type.get_meta_type());
          if (!is_return_value) {
            ObObjParam *obj_param = static_cast<ObObjParam *>(param);
            if (OB_ISNULL(obj_param)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("obj_param is NULL", K(ret));
            } else {
              obj_param->set_param_meta();
              obj_param->set_accuracy(data_type.get_accuracy());
            }
          }
        }
        if (FAILEDx(get_ob_type(obj_type, static_cast<obmysql::EMySQLFieldType>(bind_param.buffer_type_)))) {
          LOG_WARN("fail to get ob type", K(ret), K(bind_param));
        } else if (OB_FAIL(ObBindParamDecode::decode_map_[obj_type](bind_param.buffer_type_,
                                                                    tz_info,
                                                                    bind_param,
                                                                    *param,
                                                                    allocator))) {
          LOG_WARN("failed to decode param", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObMySQLProcStatement::process_proc_output_params(ObIAllocator &allocator,
                                                     ParamStore &params,
                                                     const share::schema::ObRoutineInfo &routine_info,
                                                     const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                                     common::ObIArray<std::pair<int64_t, int64_t>> &basic_out_param,
                                                     const ObTimeZoneInfo *tz_info,
                                                     ObObj *result,
                                                     bool is_sql)
{
  int ret = OB_SUCCESS;
  const int64_t params_count = params.count();
  if (OB_FAIL(result_.init())) {
    LOG_WARN("failed to init result_", K(ret));
  } else if (FALSE_IT(result_column_count_ = result_.get_result_column_count())) {
  } else if (result_column_count_ > 0
              && OB_FAIL(alloc_bind_params(result_column_count_, result_params_))) {
    LOG_WARN("failed to alloc bind params", K(ret), K(result_column_count_));
  } else if (result_column_count_ > 0) {
    ObBindParam *in_param = NULL;
    ObBindParam *out_param = NULL;
    MYSQL_BIND *mysql_bind = result_.get_bind();
    int64_t out_idx = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < basic_out_param.count(); i++) {
      if (OB_FAIL(get_bind_param_by_idx(basic_out_param.at(i).first, in_param))) {
        LOG_WARN("failed to get param", K(ret), K(i));
      } else if (OB_ISNULL(in_param)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("in_param is NULL", K(ret), K(i));
      } else if (i >= result_column_count_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("out_idx is error", K(ret), K(out_idx), K(result_column_count_));
      } else if (OB_FAIL(get_bind_result_param_by_idx(i, out_param))) {
        LOG_WARN("fail to get bind result param by idx", K(ret), K(out_idx));
      } else if (OB_ISNULL(out_param) || OB_ISNULL(mysql_bind)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("out_param is NULL", K(ret), K(out_idx), K(mysql_bind));
      } else {
        out_param->assign(*in_param);
        mysql_bind[i].buffer_type = out_param->buffer_type_;
        mysql_bind[i].buffer = out_param->buffer_;
        mysql_bind[i].buffer_length = out_param->buffer_len_;
        mysql_bind[i].length = &out_param->length_;
        mysql_bind[i].error = &mysql_bind[i].error_value;
        mysql_bind[i].is_null = &out_param->is_null_;
        if (OB_ISNULL(mysql_bind[i].buffer)) {
          void *tmp_buf = NULL;
          int64_t tmp_buf_len = get_alloca_size_by_mysql_type(out_param->buffer_type_);
          if (tmp_buf_len > 0) {
            if (OB_ISNULL(tmp_buf = allocator.alloc(tmp_buf_len))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("failed to alloc memory", K(ret));
            } else {
              mysql_bind[i].buffer = tmp_buf;
              mysql_bind[i].buffer_length = tmp_buf_len;
              out_param->buffer_ = tmp_buf;
              out_param->buffer_len_ =tmp_buf_len;
            }
          }
        }
      }
    }
    // process compsite out param
    int64_t idx_in_result = basic_out_param.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < get_com_datas().count(); ++i) {
      ObCompositeData &com_data = get_com_datas().at(i);
      for (int64_t inner_idx = 0; OB_SUCC(ret) && inner_idx < com_data.get_data_array().count(); inner_idx++) {
        CK (OB_NOT_NULL(in_param = com_data.get_data_array().at(inner_idx)));
        OZ (get_bind_result_param_by_idx(idx_in_result, out_param));
        CK (OB_NOT_NULL(out_param));
        if (OB_SUCC(ret)) {
          out_param->assign(*in_param);
          mysql_bind[idx_in_result].buffer_type = out_param->buffer_type_;
          mysql_bind[idx_in_result].buffer = NULL;
          mysql_bind[idx_in_result].buffer_length = 0;
          mysql_bind[idx_in_result].length = &out_param->length_;
          mysql_bind[idx_in_result].error = &mysql_bind[i].error_value;
          mysql_bind[idx_in_result].is_null = &out_param->is_null_;
          if (OB_ISNULL(mysql_bind[idx_in_result].buffer)) {
            void *tmp_buf = NULL;
            int64_t tmp_buf_len = get_alloca_size_by_mysql_type(out_param->buffer_type_);
            if (tmp_buf_len > 0) {
              if (OB_ISNULL(tmp_buf = allocator.alloc(tmp_buf_len))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_WARN("failed to alloc memory", K(ret));
              } else {
                mysql_bind[idx_in_result].buffer = tmp_buf;
                mysql_bind[idx_in_result].buffer_length = tmp_buf_len;
                out_param->buffer_ = tmp_buf;
                out_param->buffer_len_ = tmp_buf_len;
              }
            }
          }
        }
        idx_in_result++;
      } // end for
    } // end for
    if (OB_SUCC(ret)) {
      int tmp_ret = 0;
      if (0 != (tmp_ret = mysql_stmt_bind_result(stmt_, mysql_bind))) {
        ret = -mysql_stmt_errno(stmt_);
        LOG_WARN("failed to bind out param", K(ret), "info", mysql_stmt_error(stmt_));
      } else {
        tmp_ret = mysql_stmt_fetch(stmt_);
        if (MYSQL_DATA_TRUNCATED == tmp_ret) {
          if (OB_FAIL(handle_data_truncated(allocator))) {
            LOG_WARN("failed to handler data", K(ret));
          }
        } else {
          ret = -mysql_stmt_errno(stmt_);
          LOG_WARN("failed to fetch", K(ret), K(tmp_ret),
                   "info", mysql_stmt_error(stmt_), "info", mysql_error(conn_->get_handler()));
        }
        if (OB_SUCC(ret)) {
          idx_in_result = basic_out_param.count();
          for (int64_t i = 0; OB_SUCC(ret) && i < get_com_datas().count(); ++i) {
            ObCompositeData &com_data = get_com_datas().at(i);
            for (int64_t inner_idx = 0; OB_SUCC(ret) && inner_idx < com_data.get_data_array().count(); inner_idx++) {
              CK (OB_NOT_NULL(in_param = com_data.get_data_array().at(inner_idx)));
              OX (in_param->assign(result_params_[idx_in_result]));
              idx_in_result++;
            } // end inner for
          } // end outer for
        }
        // process basic out param
        for (int64_t i = 0; OB_SUCC(ret) && i < basic_out_param.count(); i++) {
          const int64_t col_idx = basic_out_param.at(i).first;
          ObBindParam *bind_param = nullptr;
          if (OB_FAIL(get_bind_result_param_by_idx(i, bind_param))) {
            LOG_WARN("fail to get bind param by idx", K(ret), K(col_idx), K_(stmt_param_count));
          } else if (OB_ISNULL(bind_param)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("bind_param is NULL", K(ret));
          } else {
            ObObj *obj = NULL;
            bool is_return_value = false;
            if (routine_info.is_function()
                && ((is_sql && i == result_column_count_ - 1)
                    ||(!is_sql && (col_idx == get_basic_return_value_pos())))) {
              obj = result;
              is_return_value = true;
            } else {
              obj = &params.at(basic_out_param.at(i).second);
            }
            if (OB_ISNULL(obj)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("obj is NULL", K(ret));
            } else if (OB_FAIL(convert_proc_output_param_result(basic_out_param.at(i).second, *tz_info,
                                                                *bind_param, obj, 
                                                                routine_info, allocator, is_return_value))) {
              LOG_WARN("fail to convert proc output param result", K(ret));
            }
          }
        } // end for
        // process composite out param
        OZ (process_composite_out_param(allocator, params, result, basic_out_param.count(), 
                                    routine_info, udts, tz_info));
      }
    }
  }
  return ret;
}

int ObMySQLProcStatement::process_composite_out_param(ObIAllocator &allocator,
                                                      ParamStore &params,
                                                      ObObj *result,
                                                      int64_t start_idx_in_result,
                                                      const share::schema::ObRoutineInfo &routine_info,
                                                      const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                                      const ObTimeZoneInfo *tz_info)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObMySQLProcStatement::store_string_obj(ObObj &param,
                                           ObObjType obj_type,
                                           ObIAllocator &allocator,
                                           const int64_t length,
                                           char *buffer)
{
  int ret = OB_SUCCESS;
  ObString dst(length, buffer);
  if (OB_FAIL(ob_write_string(allocator, dst, dst))) {
    LOG_WARN("failed to write str", K(ret));
  } else {
    switch (obj_type) {
      case ObVarcharType:
        param.set_varchar(dst);
        break;
      case ObCharType:
        param.set_char(dst);
        break;
      case ObTinyTextType:
        param.set_lob_value(ObTinyTextType, dst.ptr(), dst.length());
        break;
      case ObTextType:
        param.set_lob_value(ObTextType, dst.ptr(), dst.length());
        break;
      case ObMediumTextType:
        param.set_lob_value(ObMediumTextType, dst.ptr(), dst.length());
        break;
      case ObLongTextType:
        param.set_lob_value(ObLongTextType, dst.ptr(), dst.length());
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unknown type", K(ret), K(obj_type));
        break;
    }
  }
  return ret;
}

int ObMySQLProcStatement::init(ObMySQLConnection &conn,
                               const ObString &sql,
                               int64_t param_count)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObMySQLProcStatement::execute_proc(ObIAllocator &allocator,
                                       ParamStore &params,
                                       const share::schema::ObRoutineInfo &routine_info,
                                       const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                       const ObTimeZoneInfo *tz_info,
                                       ObObj *result,
                                       bool is_sql,
                                       int64_t out_param_start_pos,
                                       int64_t basic_param_start_pos,
                                       int64_t basic_return_value_pos)
{
  int ret = OB_SUCCESS;
  // pair.first is: out param position in @this.bind_params_
  // pair.second is: out param position in @params, if the routine is a function, the values is -1
  common::ObSEArray<std::pair<int64_t, int64_t>, 8> basic_out_param;
  void* execute_extend_arg = NULL;
  out_param_start_pos_ = out_param_start_pos;
  out_param_cur_pos_ = out_param_start_pos;
  basic_param_start_pos_ = basic_param_start_pos;
  basic_return_value_pos_ = basic_return_value_pos;
  if (OB_ISNULL(tz_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tz info is null", K(ret));
  } else if (OB_FAIL(bind_proc_param(allocator, params, routine_info, udts, basic_out_param, tz_info, result, is_sql))) {
    LOG_WARN("failed to bind proc param", K(ret));
  } else if (OB_FAIL(param_.bind_param())) {
    LOG_WARN("failed to bind prepared input param", "info", mysql_error(conn_->get_handler()), K(ret));
  } else if (OB_FAIL(execute_stmt_v2_interface())) {
    LOG_WARN("failed to execute PL", K(ret));
  } else if (OB_FAIL(process_proc_output_params(allocator, params, routine_info, udts, basic_out_param,
                                                tz_info, result, is_sql))) {
    LOG_WARN("fail to process proc output params", K(ret));
  }
  return ret;
}

int ObMySQLProcStatement::execute_proc()
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObMySQLProcStatement::close()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(close_mysql_stmt())) {
    LOG_WARN("close mysql stmt failed", K(ret));
  }
  free_resouce();
  return ret;
}

void ObMySQLProcStatement::free_resouce()
{
  if (NULL != bind_params_) {
    alloc_->free(bind_params_);
    bind_params_ = NULL;
    stmt_param_count_ = 0;
  }
  if (NULL != result_params_) {
    alloc_->free(result_params_);
    result_params_ = NULL;
    result_column_count_ = 0;
  }
  param_.close();
  result_.close();
  in_out_map_.reset();
  proc_ = NULL;
  out_param_start_pos_ = 0;
  out_param_cur_pos_ = 0;
  com_datas_.reset();
}

int ObMySQLProcStatement::close_mysql_stmt()
{
  int ret = OB_SUCCESS;
  if (NULL != stmt_) {
    if (0 != mysql_stmt_close(stmt_)) {
      ret = -mysql_errno(conn_->get_handler());
      LOG_WARN("fail to close stmt", "info", mysql_error(conn_->get_handler()), K(ret));
    }
  }
  return ret;
}

int ObMySQLProcStatement::execute_stmt_v2_interface()
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObMySQLProcStatement::handle_data_truncated(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  MYSQL_BIND *mysql_bind = result_.get_bind();
  if (OB_ISNULL(mysql_bind)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mysql_bind is NULL", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < result_column_count_; i++) {
    MYSQL_BIND &res_bind = mysql_bind[i];
    if (*res_bind.is_null) {
      result_params_[i].is_null_ = 1;
    } else {
      if (res_bind.buffer_length < *res_bind.length) {
        void *res_buffer = NULL;
        if (OB_ISNULL(res_buffer = allocator.alloc(*res_bind.length))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc memory failed", K(ret));
        } else {
          res_bind.buffer = res_buffer;
          res_bind.buffer_length = *res_bind.length;
        }
      }
      if (OB_SUCC(ret)) {
        if (0 != mysql_stmt_fetch_column(stmt_, &res_bind, i, 0)) {
          ret = -mysql_stmt_errno(stmt_);
          LOG_WARN("failed to fetch column", K(ret), "info", mysql_stmt_error(stmt_));
        } else {
          result_params_[i].buffer_type_ = res_bind.buffer_type;
          result_params_[i].buffer_ = res_bind.buffer;
          result_params_[i].buffer_len_ = res_bind.buffer_length;
          result_params_[i].length_ = *res_bind.length;
          result_params_[i].is_unsigned_ = res_bind.is_unsigned;
          result_params_[i].is_null_ = *res_bind.is_null;
        }
      }
    }
  }
  return ret;
}





int ObMySQLProcStatement::get_anonymous_param_count(ParamStore &params,
                                                    const share::schema::ObRoutineInfo &routine_info,
                                                    const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                                    bool is_sql,
                                                    int64_t &param_cnt,
                                                    int64_t &out_param_start_pos,
                                                    int64_t &basic_param_start_pos,
                                                    int64_t &basic_return_value_pos)
{
  int ret = OB_SUCCESS;
  param_cnt = 0;
  out_param_start_pos = 0;
  basic_param_start_pos = 0;
  basic_return_value_pos = 0;
  const share::schema::ObRoutineParam *r_param = NULL;
  const pl::ObUserDefinedType *udt = NULL;
  bool return_basic = false;
  if (routine_info.is_function()) {
    const share::schema::ObRoutineParam *r_param = routine_info.get_routine_params().at(0);
    CK (OB_NOT_NULL(r_param));
    if (OB_FAIL(ret)) {
    } else {
      param_cnt++;
      out_param_start_pos++;
      return_basic = true;
    }
  }
  int64_t start_idx = routine_info.is_function() ? 1 : 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
    if (!params.at(i).is_pl_mock_default_param()) {
      r_param = routine_info.get_routine_params().at(start_idx + i);
      CK (OB_NOT_NULL(r_param));
      if (OB_SUCC(ret)) {
        param_cnt++;
        out_param_start_pos++;
      }
    }
  }
  if (OB_SUCC(ret) && return_basic) {
    if (is_sql) {
      basic_return_value_pos = out_param_start_pos - 1;
    } else {
      basic_return_value_pos = basic_param_start_pos;
    }
  }
  return ret;
}

int64_t ObMySQLProcStatement::get_alloca_size_by_mysql_type(enum_field_types buffer_type)
{
  int64_t len = 0;
  switch (buffer_type)
  {
    case MYSQL_TYPE_DATETIME:
      len = sizeof(MYSQL_TIME);
      break;
    case MYSQL_TYPE_FLOAT:
      len = sizeof(float);
      break;
    case MYSQL_TYPE_DOUBLE:
      len = sizeof(double);
      break;
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
      len = sizeof(int64_t);
      break;
    case MYSQL_TYPE_TINY:
      len = sizeof(int);
      break;
    default:
      break;
  }
  return len;
}

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase
