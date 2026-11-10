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

#ifndef OCEANBASE_COMMON_OB_OBJ_CAST_
#define OCEANBASE_COMMON_OB_OBJ_CAST_

#include "common/object/ob_obj_type.h"
#include "common/object/ob_type_metadata.h"
#include "common/datum/datum_parameter.h"
#include "share/datum/datum_cast_context.h"
#include "share/datum/number_cast_constants.h"
#include "common/ob_accuracy.h"
#include "common/ob_zerofill_info.h"
#include "common/timezone/ob_timezone_info.h"
#include "common/timezone/ob_time_convert.h"
#include "lib/charset/ob_charset.h"
#include "share/geo/ob_geo_common.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace common
{
class ObILobReadService;

class ObISrsProvider;

class ObIDatumCastRuntime;

class ObExpectType
{
public:
  ObExpectType()
    : type_(ObMaxType),
      cs_type_(CS_TYPE_INVALID),
      type_infos_(NULL)
      {}
  explicit ObExpectType(const SqlValueType type)
    : type_(type),
      cs_type_(CS_TYPE_INVALID),
      type_infos_(NULL)
      {}
  explicit ObExpectType(const SqlValueType type, const ObCollationType cs_type)
    : type_(type),
      cs_type_(cs_type),
      type_infos_(NULL)
      {}
  ~ObExpectType()  { reset(); }
  void reset()
  {
    type_ = ObMaxType;
    cs_type_ = CS_TYPE_INVALID;
    type_infos_ = NULL;
  }
  OB_INLINE void set_type(SqlValueType type) { type_ = type; }
  OB_INLINE void set_collation_type(ObCollationType cs_type) { cs_type_ = cs_type; }
  OB_INLINE void set_type_infos(const ObIArray<ObString> *type_infos) { type_infos_ = type_infos; }
  OB_INLINE SqlValueType get_type() const { return type_; }
  OB_INLINE SqlTypeClass get_type_class() const { return sql_type_class(type_); }
  OB_INLINE ObCollationType get_collation_type() const { return cs_type_; }
  OB_INLINE const ObIArray<ObString> *get_type_infos() const { return type_infos_; }

  TO_STRING_KV(K(type_), K(cs_type_), KPC(type_infos_));
private:
  DISALLOW_COPY_AND_ASSIGN(ObExpectType);
private:
  SqlValueType type_;
  ObCollationType cs_type_;
  const ObIArray<ObString> *type_infos_;
};


// CM_STRING_INTEGER_TRUNC only affect string to [unsigned] integer cast.
// ignore CM_STRING_INTEGER_TRUNC if not string to integer cast (is_str_integer_cast is false)
// e.g:
//  cast number to int will invoke this functon, but it's not string to integer cast.
int common_string_unsigned_integer(const ObCastMode &cast_mode,
                                 const SqlValueType &in_type,
                                 const ObCollationType &in_cs_type,
                                 const ObString &in_str,
                                 const bool is_str_integer_cast,
                                 uint64_t &out_val);
int common_string_integer(const ObCastMode &cast_mode,
                                 const SqlValueType &in_type,
                                 const ObCollationType &in_cs_type,
                                 const ObString &in_str,
                                 const bool is_str_integer_cast,
                                 int64_t &out_val);


//==============================


int get_bit_len(const ObString &str, int32_t &bit_len);
int get_bit_len(uint64_t value, int32_t &bit_len);
class ObTypeCastRules
{
public:
  static int is_cast_monotonic(SqlValueType t1, SqlValueType t2, bool &is_monotonic);
  static int is_order_consistent(const SqlTypeMeta &from,
                                 const SqlTypeMeta &to,
                                 bool &result);
  static int is_const_consistent(const SqlTypeMeta &const_mt,
                                 const SqlTypeMeta &column_mt,
                                 const SqlValueType calc_type,
                                 const ObCollationType calc_collation,
                                 bool &result);
private:
  inline static int64_t get_idx_of_collate(ObCollationType cs_type)
  {
    int64_t idx = -1;
    switch(cs_type) {
      case CS_TYPE_UTF8MB4_GENERAL_CI:
        idx = 0;
        break;
      case CS_TYPE_UTF8MB4_BIN:
        idx = 1;
        break;
      case CS_TYPE_BINARY:
        idx = 2;
        break;
      default:
        idx = -1;
    }
    return idx;
  }
private:
  static const int64_t VALID_OC_COLLATION_TYPES = 3;
  static const bool CAST_MONOTONIC[ObMaxTC][ObMaxTC];
  static const bool ORDER_CONSISTENT[ObMaxTC][ObMaxTC];
  static const bool ORDER_CONSISTENT_WITH_BOTH_STRING[VALID_OC_COLLATION_TYPES][VALID_OC_COLLATION_TYPES][VALID_OC_COLLATION_TYPES];
};

class DatumBoolEvaluator
{
public:
  static int is_true(const DatumParameter &value, ObCastMode cast_mode, bool &result);
  inline static int is_true(const DatumParameter &value, bool &result)
  { return is_true(value, CM_WARN_ON_FAIL, result); }

};

class ObGeoCastUtils
{
public:
  static common::ObGeoType get_geo_type_from_cast_mode(uint64_t cast_mode);
  static int set_geo_type_to_cast_mode(common::ObGeoType geo_type,
                                       uint64_t &cast_mode);
  static void geo_cast_error_handle(int err_code,
                                    common::ObGeoType src_type,
                                    common::ObGeoType dst_type,
                                    const common::ObGeoErrLogInfo &log_info);
};

} // end namespace common
} // end namespace oceanbase

#endif //OCEANBASE_COMMON_OB_OBJ_CAST_
