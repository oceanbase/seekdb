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

#define USING_LOG_PREFIX SERVER

#include "ob_mysql_result_set.h"
#include "observer/mysql/obsm_utils.h"
#include "observer/ob_server.h"

using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::obmysql;

int ObMySQLResultSet::to_mysql_field(const ObField &field, ObMySQLField &mfield)
{
  int ret = OB_SUCCESS;
  mfield.dname_ = field.dname_;
  mfield.tname_ = field.tname_;
  mfield.org_tname_ = field.org_tname_;
  mfield.cname_ = field.cname_;
  mfield.org_cname_ = field.org_cname_;

  if (OB_SUCC(ret)) {
    mfield.accuracy_ = field.accuracy_;
    // mfield.type_ = oceanbase::obmysql::MYSQL_TYPE_LONG;
    // mfield.default_value_ = field.default_value_;
    // To distinguish between binary and nonbinary data for string data types,
    // check whether the charsetnr value is 63. Also, flag must be set to binary accordingly
    mfield.charsetnr_ = field.charsetnr_;
    mfield.flags_ = field.flags_;
    mfield.length_ = field.length_;
    ObScale decimals = mfield.accuracy_.get_scale();
    ObPrecision pre = mfield.accuracy_.get_precision();
    // TIMESTAMP, UNSIGNED are directly mapped through map
    if (0 == field.type_name_.case_compare("SYS_REFCURSOR")) {
      mfield.type_ = MYSQL_TYPE_CURSOR;
    } else {
      ret = ObSMUtils::get_mysql_type(field.type_.get_type(), mfield.type_, mfield.flags_, decimals);
    }

    mfield.type_owner_ = field.type_owner_;
    mfield.type_name_ = field.type_name_;
   //  In this scenario, the precsion and scale of number are undefined, 
   //  and the internal implementation of ob is represented by an illegal value (-1, -85). 
   //  However, oracle is represented by 0. In order to be compatible with 
   //  the behavior of oracle, it is corrected to 0 here.
    if ((ObNumberType == field.type_.get_type() 
          || ObUNumberType == field.type_.get_type()) 
        && lib::is_oracle_mode()) { // was decimal
      decimals = (decimals==NUMBER_SCALE_UNKNOWN_YET ? 0:decimals);
      pre = (pre==PRECISION_UNKNOWN_YET ? 0:pre);
    }
    mfield.accuracy_.set_precision(pre);
    mfield.accuracy_.set_scale(decimals);
    mfield.inout_mode_ = field.inout_mode_;
    if (OB_SUCC(ret)
        && ObExtendType == field.type_.get_type() && mfield.type_name_.empty()) {
      // anonymous collection
      uint16_t flags;
      ObScale num_decimals;
      ret = ObSMUtils::get_mysql_type(
        field.default_value_.get_type(), mfield.default_value_, flags, num_decimals);
    }
    if (field.is_hidden_rowid_) {
      mfield.inout_mode_ |= 0x04;
    }
  }
  LOG_TRACE("to mysql field", K(ret), K(mfield), K(field));
  return ret;
}

int ObMySQLResultSet::to_new_result_field(const ObField &field, ObMySQLField &mfield)
{
  int ret = OB_SUCCESS;
  if (lib::is_mysql_mode()) {
    if (OB_FAIL(to_mysql_field(field, mfield))) {
      LOG_WARN("fail to mysql field", K(ret), K(mfield), K(field));
    }
  } else if (lib::is_oracle_mode()) {
    if (OB_FAIL(to_oracle_field(field, mfield))) {
      LOG_WARN("fail to oracle field", K(ret), K(mfield), K(field));
    }
  } else {
    // for pg mode.
  }
  return ret;
}

int ObMySQLResultSet::to_oracle_field(const ObField &field, ObMySQLField &mfield)
{
  int ret = OB_SUCCESS;
  mfield.dname_ = field.dname_;
  mfield.tname_ = field.tname_;
  mfield.org_tname_ = field.org_tname_;
  mfield.cname_ = field.cname_;
  mfield.org_cname_ = field.org_cname_;

  if (OB_SUCC(ret)) {
    mfield.accuracy_ = field.accuracy_;
    // mfield.type_ = oceanbase::obmysql::MYSQL_TYPE_LONG;
    // mfield.default_value_ = field.default_value_;

    // To distinguish between binary and nonbinary data for string data types,
    // check whether the charsetnr value is 63. Also, flag must be set to binary accordingly
    mfield.charsetnr_ = field.charsetnr_;
    mfield.flags_ = field.flags_;
    mfield.length_ = field.length_;
    ObScale decimals = mfield.accuracy_.get_scale();
    ObPrecision pre = mfield.accuracy_.get_precision();
    // TIMESTAMP, UNSIGNED are directly mapped through map
    if (0 == field.type_name_.case_compare("SYS_REFCURSOR")) {
      mfield.type_ = MYSQL_TYPE_CURSOR;
    } else {
      ret = ObSMUtils::get_mysql_type(field.type_.get_type(), mfield.type_, mfield.flags_, decimals);
    }
    LOG_DEBUG("debug to oracle field in middle", K(ret), K(mfield), K(field), K(mfield.length_), K(mfield.accuracy_));
    mfield.type_owner_ = field.type_owner_;
    mfield.type_name_ = field.type_name_;
   //  In this scenario, the precsion and scale of number are undefined, 
   //  and the internal implementation of ob is represented by an illegal value (-1, -85). 
   //  However, oracle is represented by 0. In order to be compatible with 
   //  the behavior of oracle, it is corrected to 0 here.
    if ((ObNumberType == field.type_.get_type() 
          || ObUNumberType == field.type_.get_type())) { // was decimal
      decimals = (decimals==NUMBER_SCALE_UNKNOWN_YET ? 0:decimals);
      pre = (pre==PRECISION_UNKNOWN_YET ? 0:pre);
    }
    if (EMySQLFieldType::MYSQL_TYPE_OB_NVARCHAR2 == mfield.type_
          || EMySQLFieldType::MYSQL_TYPE_OB_NCHAR == mfield.type_) {
      int64_t mbmaxlen = 1;
      if (OB_FAIL(common::ObCharset::get_mbmaxlen_by_coll(static_cast<ObCollationType>(mfield.charsetnr_), mbmaxlen))) {
        LOG_WARN("fail to get mbmaxlen", K(field.charsetnr_), K(ret));
      }
      mfield.length_ = mfield.length_ / mbmaxlen;
    }
    switch_ps(pre, decimals, mfield.type_);
    mfield.accuracy_.set_precision(pre);
    mfield.accuracy_.set_scale(decimals);
    mfield.inout_mode_ = field.inout_mode_;
    if (OB_SUCC(ret)
        && ObExtendType == field.type_.get_type() && mfield.type_name_.empty()) {
      // anonymous collection
      uint16_t flags;
      ObScale num_decimals;
      ret = ObSMUtils::get_mysql_type(
        field.default_value_.get_type(), mfield.default_value_, flags, num_decimals);
    }
    if (field.is_hidden_rowid_) {
      mfield.inout_mode_ |= 0x04;
    }
  }
  LOG_DEBUG("debug to oracle field", K(ret), K(mfield), K(field), K(mfield.length_), K(mfield.accuracy_));
  return ret;
}

// oracle mode need special handling
void ObMySQLResultSet::switch_ps(ObPrecision &pre, ObScale &scale, EMySQLFieldType type)
{
  switch(type)
  {
    case EMySQLFieldType::MYSQL_TYPE_FLOAT:
    case EMySQLFieldType::MYSQL_TYPE_DOUBLE:
      scale = (scale == ORA_NUMBER_SCALE_UNKNOWN_YET ? 0 : scale);
      pre = (pre==PRECISION_UNKNOWN_YET ? 0:pre);
      break;
    case EMySQLFieldType::MYSQL_TYPE_DATETIME:
      pre = (pre==DATETIME_MIN_LENGTH ? 0:pre);
      break;
    case EMySQLFieldType::MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case EMySQLFieldType::MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
    case EMySQLFieldType::MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
      pre = 0;
      break;
    case EMySQLFieldType::MYSQL_TYPE_OB_RAW:
      pre = (pre==PRECISION_UNKNOWN_YET ? 0:pre);
      break;
    default:
      break;
  }

}

int ObMySQLResultSet::next_field(ObMySQLField &obmf)
{
  int ret = OB_SUCCESS;
  int64_t field_cnt = 0;
  const ColumnsFieldIArray *fields = get_field_columns();
  if (OB_ISNULL(fields)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    field_cnt = get_field_cnt();
    if (field_index_ >= field_cnt) {
      ret = OB_ITER_END;
    } else {
      const ObField &field = fields->at(field_index_++);
      if (OB_FAIL(to_mysql_field(field, obmf))) {
        // do nothing
      } else {
        replace_lob_type(get_session(), field, obmf);
      }
    }
  }
  set_errcode(ret);
  return ret;
}

int ObMySQLResultSet::next_param(ObMySQLField &obmf)
{
  int ret = OB_SUCCESS;
  const ParamsFieldIArray *params = get_param_fields();
  if (OB_ISNULL(params)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t param_cnt = params->count();
    if (param_index_ >= param_cnt) {
      ret = OB_ITER_END;
    }
    if (OB_SUCC(ret)) {
      ObField field;
      ret = params->at(param_index_++, field);
      if (OB_SUCC(ret)) {
        if (OB_FAIL(to_mysql_field(field, obmf))) {
          // do nothing
        } else {
          replace_lob_type(get_session(), field, obmf);
        }
      }
    }
  }
  set_errcode(ret);
  return ret;
}

