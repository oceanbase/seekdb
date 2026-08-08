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
#include "query/protocol/ob_mysql_protocol_util.h"
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
    ret = ObSMUtils::get_mysql_type(field.type_.get_type(), mfield.type_, mfield.flags_, decimals);

    mfield.type_owner_ = field.type_owner_;
    mfield.type_name_ = field.type_name_;
   //  In this scenario, the precision and scale of number are undefined, and
   //  the internal implementation of OB is represented by an illegal value
   //  (-1, -85). This result-set metadata path normalizes them to 0.
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
  }
  return ret;
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
        replace_lob_type(obmf);
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
          replace_lob_type(obmf);
        }
      }
    }
  }
  set_errcode(ret);
  return ret;
}
