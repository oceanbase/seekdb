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

#ifndef OCEANBASE_SRC_SQL_OB_SPI_PARAM_H_
#define OCEANBASE_SRC_SQL_OB_SPI_PARAM_H_

#include "common/object/ob_object.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "lib/number/ob_number_v2.h"

namespace oceanbase
{
namespace sql
{

/**
 * ObSPIParam - Single parameter wrapper for parameterized queries
 *
 * Provides type-safe factory methods for creating query parameters,
 * similar to pymysql's parameter binding.
 *
 * Example usage:
 *   ObSPIParam param = ObSPIParam::from_int(1001);
 *   ObSPIParam str_param = ObSPIParam::from_string("John's Company");
 */
class ObSPIParam
{
public:
  // Parameter mode for IN/OUT parameters
  enum ParamMode {
    SPI_PARAM_IN = 0,      // Input parameter
    SPI_PARAM_OUT = 1,     // Output parameter
    SPI_PARAM_INOUT = 2    // Input/Output parameter
  };

  ObSPIParam();
  ~ObSPIParam();
  ObSPIParam(const ObSPIParam &other);
  ObSPIParam &operator=(const ObSPIParam &other);

  //========== Static factory methods - Type-safe parameter creation ==========

  // NULL value
  static ObSPIParam null();

  // Integer types
  static ObSPIParam from_int(int64_t value);
  static ObSPIParam from_uint(uint64_t value);
  static ObSPIParam from_int32(int32_t value);
  static ObSPIParam from_tinyint(int8_t value);
  static ObSPIParam from_smallint(int16_t value);

  // Floating point types
  static ObSPIParam from_float(float value);
  static ObSPIParam from_double(double value);

  // String types (deep copy for safety)
  static ObSPIParam from_string(const char* value);
  static ObSPIParam from_string(const common::ObString &value);
  static ObSPIParam from_varchar(const char* value, int64_t len);

  // Date/time types
  static ObSPIParam from_datetime(int64_t usec);
  static ObSPIParam from_timestamp(int64_t usec);
  static ObSPIParam from_date(int32_t date);
  static ObSPIParam from_time(int64_t time);

  // High precision number type
  static ObSPIParam from_number(const common::number::ObNumber &num);

  // Binary type
  static ObSPIParam from_blob(const void* data, int64_t len);

  //========== Member accessors ==========

  const common::ObObjParam &get_obj_param() const { return obj_param_; }
  common::ObObjParam &get_obj_param() { return obj_param_; }
  ParamMode get_mode() const { return mode_; }
  void set_mode(ParamMode mode) { mode_ = mode; }
  bool is_null() const { return obj_param_.is_null(); }
  common::ObObjType get_type() const { return obj_param_.get_type(); }

  // Result getters for OUT parameters
  int get_result_int(int64_t &value) const;
  int get_result_uint(uint64_t &value) const;
  int get_result_double(double &value) const;
  int get_result_string(common::ObString &value) const;

  TO_STRING_KV(K_(obj_param), K_(mode));

private:
  common::ObObjParam obj_param_;
  ParamMode mode_;
};

/**
 * ObSPIParamList - Parameter list container for parameterized queries
 *
 * Provides fluent API for building parameter lists, similar to pymysql's
 * parameter tuple.
 *
 * Example usage:
 *   ObArenaAllocator allocator;
 *   ObSPIParamList params(allocator);
 *   params.add_int(1001)
 *         .add_string("John's Company")
 *         .add_double(5000.50);
 */
class ObSPIParamList
{
public:
  explicit ObSPIParamList(common::ObIAllocator &allocator);
  ~ObSPIParamList();

  //========== Fluent API for adding parameters ==========

  ObSPIParamList &add_null();
  ObSPIParamList &add_int(int64_t value);
  ObSPIParamList &add_uint(uint64_t value);
  ObSPIParamList &add_int32(int32_t value);
  ObSPIParamList &add_float(float value);
  ObSPIParamList &add_double(double value);
  ObSPIParamList &add_string(const char* value);
  ObSPIParamList &add_string(const common::ObString &value);
  ObSPIParamList &add_datetime(int64_t usec);
  ObSPIParamList &add_timestamp(int64_t usec);
  ObSPIParamList &add_date(int32_t date);
  ObSPIParamList &add_time(int64_t time);
  ObSPIParamList &add_number(const common::number::ObNumber &num);
  ObSPIParamList &add_blob(const void* data, int64_t len);
  ObSPIParamList &add_param(const ObSPIParam &param);

  // Batch add for FORALL scenarios
  int add_batch(const common::ObIArray<ObSPIParam> &params);

  //========== Access methods ==========

  int64_t count() const { return params_.count(); }
  const ObSPIParam &at(int64_t idx) const { return params_.at(idx); }
  ObSPIParam &at(int64_t idx) { return params_.at(idx); }
  bool is_empty() const { return params_.empty(); }

  // Convert to ParamStore (compatible with existing interfaces)
  int to_param_store(common::ParamStore &param_store) const;

  // Get internal array reference
  const common::ObSEArray<ObSPIParam, 8> &get_params() const { return params_; }

  // Get last error code (for fluent API error checking)
  int get_last_error() const { return last_error_; }
  bool has_error() const { return last_error_ != common::OB_SUCCESS; }

  // Clear all parameters
  void reset();

  // Get allocator
  common::ObIAllocator &get_allocator() { return allocator_; }

  TO_STRING_KV(K_(params), K_(last_error));

private:
  // Deep copy string to allocator for safety
  int deep_copy_string(const common::ObString &src, common::ObString &dst);

  common::ObSEArray<ObSPIParam, 8> params_;
  common::ObIAllocator &allocator_;
  int last_error_;  // Error state for fluent API
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SRC_SQL_OB_SPI_PARAM_H_
