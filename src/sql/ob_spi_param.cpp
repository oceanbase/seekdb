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

#define USING_LOG_PREFIX SQL

#include "sql/ob_spi_param.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/utility.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

//============================================================================
// ObSPIParam Implementation
//============================================================================

ObSPIParam::ObSPIParam()
    : obj_param_(),
      mode_(SPI_PARAM_IN)
{
  obj_param_.set_null();
}

ObSPIParam::~ObSPIParam()
{
}

ObSPIParam::ObSPIParam(const ObSPIParam &other)
    : obj_param_(other.obj_param_),
      mode_(other.mode_)
{
}

ObSPIParam &ObSPIParam::operator=(const ObSPIParam &other)
{
  if (this != &other) {
    obj_param_ = other.obj_param_;
    mode_ = other.mode_;
  }
  return *this;
}

ObSPIParam ObSPIParam::null()
{
  ObSPIParam param;
  param.obj_param_.set_null();
  return param;
}

ObSPIParam ObSPIParam::from_int(int64_t value)
{
  ObSPIParam param;
  param.obj_param_.set_int(value);
  return param;
}

ObSPIParam ObSPIParam::from_uint(uint64_t value)
{
  ObSPIParam param;
  param.obj_param_.set_uint64(value);
  return param;
}

ObSPIParam ObSPIParam::from_int32(int32_t value)
{
  ObSPIParam param;
  param.obj_param_.set_int32(value);
  return param;
}

ObSPIParam ObSPIParam::from_tinyint(int8_t value)
{
  ObSPIParam param;
  param.obj_param_.set_tinyint(value);
  return param;
}

ObSPIParam ObSPIParam::from_smallint(int16_t value)
{
  ObSPIParam param;
  param.obj_param_.set_smallint(value);
  return param;
}

ObSPIParam ObSPIParam::from_float(float value)
{
  ObSPIParam param;
  param.obj_param_.set_float(value);
  return param;
}

ObSPIParam ObSPIParam::from_double(double value)
{
  ObSPIParam param;
  param.obj_param_.set_double(value);
  return param;
}

ObSPIParam ObSPIParam::from_string(const char* value)
{
  ObSPIParam param;
  if (OB_ISNULL(value)) {
    param.obj_param_.set_null();
  } else {
    param.obj_param_.set_varchar(value);
    param.obj_param_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  }
  return param;
}

ObSPIParam ObSPIParam::from_string(const ObString &value)
{
  ObSPIParam param;
  param.obj_param_.set_varchar(value);
  param.obj_param_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  return param;
}

ObSPIParam ObSPIParam::from_varchar(const char* value, int64_t len)
{
  ObSPIParam param;
  if (OB_ISNULL(value) || len < 0) {
    param.obj_param_.set_null();
  } else {
    param.obj_param_.set_varchar(value, static_cast<ObString::obstr_size_t>(len));
    param.obj_param_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  }
  return param;
}

ObSPIParam ObSPIParam::from_datetime(int64_t usec)
{
  ObSPIParam param;
  param.obj_param_.set_datetime(usec);
  return param;
}

ObSPIParam ObSPIParam::from_timestamp(int64_t usec)
{
  ObSPIParam param;
  param.obj_param_.set_timestamp(usec);
  return param;
}

ObSPIParam ObSPIParam::from_date(int32_t date)
{
  ObSPIParam param;
  param.obj_param_.set_date(date);
  return param;
}

ObSPIParam ObSPIParam::from_time(int64_t time)
{
  ObSPIParam param;
  param.obj_param_.set_time(time);
  return param;
}

ObSPIParam ObSPIParam::from_number(const number::ObNumber &num)
{
  ObSPIParam param;
  param.obj_param_.set_number(num);
  return param;
}

ObSPIParam ObSPIParam::from_blob(const void* data, int64_t len)
{
  ObSPIParam param;
  if (OB_ISNULL(data) || len < 0) {
    param.obj_param_.set_null();
  } else {
    ObString blob_data(static_cast<ObString::obstr_size_t>(len),
                       static_cast<const char*>(data));
    param.obj_param_.set_lob_value(ObLongTextType, blob_data.ptr(), blob_data.length());
    param.obj_param_.set_collation_type(CS_TYPE_BINARY);
    param.obj_param_.set_collation_level(CS_LEVEL_IMPLICIT);
  }
  return param;
}

int ObSPIParam::get_result_int(int64_t &value) const
{
  int ret = OB_SUCCESS;
  if (obj_param_.is_null()) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("parameter is null", K(ret));
  } else if (!obj_param_.is_integer_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameter is not integer type", K(ret), K_(obj_param));
  } else {
    value = obj_param_.get_int();
  }
  return ret;
}

int ObSPIParam::get_result_uint(uint64_t &value) const
{
  int ret = OB_SUCCESS;
  if (obj_param_.is_null()) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("parameter is null", K(ret));
  } else if (!obj_param_.is_uint64()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameter is not uint64 type", K(ret), K_(obj_param));
  } else {
    value = obj_param_.get_uint64();
  }
  return ret;
}

int ObSPIParam::get_result_double(double &value) const
{
  int ret = OB_SUCCESS;
  if (obj_param_.is_null()) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("parameter is null", K(ret));
  } else if (!obj_param_.is_double()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameter is not double type", K(ret), K_(obj_param));
  } else {
    value = obj_param_.get_double();
  }
  return ret;
}

int ObSPIParam::get_result_string(ObString &value) const
{
  int ret = OB_SUCCESS;
  if (obj_param_.is_null()) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("parameter is null", K(ret));
  } else if (!obj_param_.is_string_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameter is not string type", K(ret), K_(obj_param));
  } else {
    value = obj_param_.get_string();
  }
  return ret;
}

//============================================================================
// ObSPIParamList Implementation
//============================================================================

ObSPIParamList::ObSPIParamList(ObIAllocator &allocator)
    : params_(),
      allocator_(allocator),
      last_error_(OB_SUCCESS)
{
  params_.set_tenant_id(MTL_ID());
}

ObSPIParamList::~ObSPIParamList()
{
  reset();
}

void ObSPIParamList::reset()
{
  params_.reset();
  last_error_ = OB_SUCCESS;
}

int ObSPIParamList::deep_copy_string(const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  if (src.empty()) {
    dst.reset();
  } else {
    char *buf = static_cast<char*>(allocator_.alloc(src.length()));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for string", K(ret), K(src.length()));
    } else {
      MEMCPY(buf, src.ptr(), src.length());
      dst.assign_ptr(buf, src.length());
    }
  }
  return ret;
}

ObSPIParamList &ObSPIParamList::add_null()
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::null());
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_int(int64_t value)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_int(value));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_uint(uint64_t value)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_uint(value));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_int32(int32_t value)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_int32(value));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_float(float value)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_float(value));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_double(double value)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_double(value));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_string(const char* value)
{
  if (OB_SUCCESS == last_error_) {
    if (OB_ISNULL(value)) {
      last_error_ = params_.push_back(ObSPIParam::null());
    } else {
      ObString src(value);
      ObString dst;
      last_error_ = deep_copy_string(src, dst);
      if (OB_SUCCESS == last_error_) {
        ObSPIParam param;
        param.get_obj_param().set_varchar(dst);
        param.get_obj_param().set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
        last_error_ = params_.push_back(param);
      }
    }
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_string(const ObString &value)
{
  if (OB_SUCCESS == last_error_) {
    ObString dst;
    last_error_ = deep_copy_string(value, dst);
    if (OB_SUCCESS == last_error_) {
      ObSPIParam param;
      param.get_obj_param().set_varchar(dst);
      param.get_obj_param().set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
      last_error_ = params_.push_back(param);
    }
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_datetime(int64_t usec)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_datetime(usec));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_timestamp(int64_t usec)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_timestamp(usec));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_date(int32_t date)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_date(date));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_time(int64_t time)
{
  if (OB_SUCCESS == last_error_) {
    last_error_ = params_.push_back(ObSPIParam::from_time(time));
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_number(const number::ObNumber &num)
{
  if (OB_SUCCESS == last_error_) {
    // Deep copy number
    number::ObNumber copied_num;
    last_error_ = copied_num.from(num, allocator_);
    if (OB_SUCCESS == last_error_) {
      ObSPIParam param;
      param.get_obj_param().set_number(copied_num);
      last_error_ = params_.push_back(param);
    }
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_blob(const void* data, int64_t len)
{
  if (OB_SUCCESS == last_error_) {
    if (OB_ISNULL(data) || len < 0) {
      last_error_ = params_.push_back(ObSPIParam::null());
    } else if (0 == len) {
      ObSPIParam param;
      param.get_obj_param().set_lob_value(ObLongTextType, "", 0);
      param.get_obj_param().set_collation_type(CS_TYPE_BINARY);
      param.get_obj_param().set_collation_level(CS_LEVEL_IMPLICIT);
      last_error_ = params_.push_back(param);
    } else {
      // Deep copy blob data
      char *buf = static_cast<char*>(allocator_.alloc(len));
      if (OB_ISNULL(buf)) {
        last_error_ = OB_ALLOCATE_MEMORY_FAILED;
        int ret = last_error_;
        LOG_WARN("failed to allocate memory for blob", K(ret), K(len));
      } else {
        MEMCPY(buf, data, len);
        ObSPIParam param;
        param.get_obj_param().set_lob_value(ObLongTextType, buf, len);
        param.get_obj_param().set_collation_type(CS_TYPE_BINARY);
        param.get_obj_param().set_collation_level(CS_LEVEL_IMPLICIT);
        last_error_ = params_.push_back(param);
      }
    }
  }
  return *this;
}

ObSPIParamList &ObSPIParamList::add_param(const ObSPIParam &param)
{
  if (OB_SUCCESS == last_error_) {
    ObSPIParam copied_param;
    int ret = deep_copy_objparam(allocator_, param.get_obj_param(), copied_param.get_obj_param());
    if (OB_FAIL(ret)) {
      last_error_ = ret;
      LOG_WARN("failed to deep copy param", K_(last_error));
    } else {
      copied_param.set_mode(param.get_mode());
      last_error_ = params_.push_back(copied_param);
    }
  }
  return *this;
}

int ObSPIParamList::add_batch(const ObIArray<ObSPIParam> &params)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    add_param(params.at(i));
    ret = last_error_;
  }
  return ret;
}

int ObSPIParamList::to_param_store(common::ParamStore &param_store) const
{
  int ret = OB_SUCCESS;
  param_store.reset();

  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    const ObSPIParam &param = params_.at(i);
    ObObjParam obj_param = param.get_obj_param();

    // Set parameter flag
    obj_param.set_param_meta();

    if (OB_FAIL(param_store.push_back(obj_param))) {
      LOG_WARN("failed to push param to store", K(ret), K(i));
    }
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
