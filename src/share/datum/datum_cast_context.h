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

#pragma once
#include "lib/ob_define.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "common/ob_zerofill_info.h"
#include "common/timezone/ob_time_convert.h"
namespace oceanbase { namespace common {
class ObILobReadService;
class ObISrsProvider;
class ObIDatumCastRuntime;
#define DOUBLE_TRUE_VALUE_THRESHOLD (1e-50)

#define OB_IS_DOUBLE_ZERO(d)  (d < DOUBLE_TRUE_VALUE_THRESHOLD && d > -DOUBLE_TRUE_VALUE_THRESHOLD)

#define OB_IS_DOUBLE_NOT_ZERO(d)  (d >= DOUBLE_TRUE_VALUE_THRESHOLD || d <= -DOUBLE_TRUE_VALUE_THRESHOLD)

#define CM_NONE                          (0ULL)
#define CM_WARN_ON_FAIL                  (1ULL << 0)
#define CM_NULL_ON_WARN                  (1ULL << 1)
#define CM_NO_RANGE_CHECK                (1ULL << 2)
#define CM_NO_CAST_INT_UINT              (1ULL << 3)
#define CM_ZERO_FILL                     (1ULL << 4)
#define CM_FORMAT_NUMBER_WITH_LIMIT      (1ULL << 5)
#define CM_CHARSET_CONVERT_IGNORE_ERR    (1ULL << 6)
#define CM_STRICT_MODE                   (1ULL << 8)
#define CM_SET_MIN_IF_OVERFLOW           (1ULL << 9)
#define CM_ERROR_ON_SCALE_OVER           (1ULL << 10)
#define CM_STRICT_JSON                   (1ULL << 11)

#define CM_ADD_ZEROFILL                  (1ULL << 47)
#define CM_CS_LEVEL_RESERVED1            (1ULL << 48)
#define CM_CS_LEVEL_RESERVED2            (1ULL << 49)
#define CM_CS_LEVEL_RESERVED3            (1ULL << 50)
#define CM_CS_LEVEL_SHIFT                48
#define CM_CS_LEVEL_MASK                 7ULL
#define CM_TIME_TRUNCATE_FRACTIONAL      (1ULL << 51)
#define CM_TO_COLUMN_CS_LEVEL            (1ULL << 52)
#define CM_ERROR_FOR_DIVISION_BY_ZERO    (1ULL << 53)
#define CM_NO_ZERO_IN_DATE               (1ULL << 54) // reserve
#define CM_NO_ZERO_DATE                  (1ULL << 55)
#define CM_ALLOW_INVALID_DATES           (1ULL << 56)
#define CM_GEOMETRY_TYPE_RESERVED1       (1ULL << 12)
#define CM_GEOMETRY_TYPE_RESERVED2       (1ULL << 13)
#define CM_GEOMETRY_TYPE_RESERVED3       (1ULL << 14)
#define CM_GEOMETRY_TYPE_RESERVED4       (1ULL << 15)
#define CM_GEOMETRY_TYPE_RESERVED5       (1ULL << 16)
#define CM_SQL_TO_JSON_SCALAR            (1ULL << 17)
#define CM_CONST_TO_DECIMAL_INT_DOWN     (1ULL << 18)
#define CM_CONST_TO_DECIMAL_INT_EQ        (1ULL << 19)
#define CM_BY_TRANSFORMER                (1ULL << 20)
#define CM_CONST_TO_DECIMAL_INT_UP       (1ULL << 21)
#define CM_INTERNAL_CAST_IGNORE          (1ULL << 23)
#define CM_DEMOTE_CAST                   (1ULL << 24)
// string->integer(int/uint) when default rounding (round to nearest) is performed,
// If this flag is set, truncation (round to zero) will be performed
// ceil(round to +inf) and floor(round to -inf) are not currently supported
#define CM_STRING_INTEGER_TRUNC          (1ULL << 57)
#define CM_COLUMN_CONVERT                (1ULL << 58)
#define CM_ENABLE_BLOB_CAST              (1ULL << 59)
#define CM_EXPLICIT_CAST                 (1ULL << 60)
// bit 61 is retired and left unused
#define CM_INSERT_UPDATE_SCOPE           (1ULL << 62)
#define CM_INTERNAL_CALL                 (1ULL << 63)

typedef uint64_t ObCastMode;

#define CM_IS_WARN_ON_FAIL(mode)              ((CM_WARN_ON_FAIL & (mode)) != 0)
#define CM_IS_ERROR_ON_FAIL(mode)             (!CM_IS_WARN_ON_FAIL(mode))
#define CM_SET_WARN_ON_FAIL(mode)             (CM_WARN_ON_FAIL | (mode))
#define CM_IS_NULL_ON_WARN(mode)              ((CM_NULL_ON_WARN & (mode)) != 0)
#define CM_IS_ZERO_ON_WARN(mode)              (!CM_IS_NULL_ON_WARN(mode))
#define CM_SKIP_RANGE_CHECK(mode)             ((CM_NO_RANGE_CHECK & (mode)) != 0)
#define CM_NEED_RANGE_CHECK(mode)             (!CM_SKIP_RANGE_CHECK(mode))
#define CM_SKIP_CAST_INT_UINT(mode)           ((CM_NO_CAST_INT_UINT & (mode)) != 0)
#define CM_NEED_CAST_INT_UINT(mode)           (!CM_SKIP_CAST_INT_UINT(mode))
#define CM_UNSET_NO_CAST_INT_UINT(mode)       ((~CM_NO_CAST_INT_UINT & (mode)) != 0)
#define CM_IS_COLUMN_CONVERT(mode)            ((CM_COLUMN_CONVERT & (mode)) != 0)
#define CM_IS_BLOB_CAST_ENABLED(mode)         ((CM_ENABLE_BLOB_CAST & (mode)) != 0)
#define CM_IS_EXPLICIT_CAST(mode)             ((CM_EXPLICIT_CAST & (mode)) != 0)
#define CM_IS_IMPLICIT_CAST(mode)             (!CM_IS_EXPLICIT_CAST(mode))
#define CM_IS_INTERNAL_CALL(mode)             ((CM_INTERNAL_CALL & (mode)) != 0)
#define CM_IS_EXTERNAL_CALL(mode)             (!CM_IS_INTERNAL_CALL(mode))
#define CM_IS_STRICT_MODE(mode)               ((CM_STRICT_MODE & (mode)) != 0)
#define CM_IS_TIME_TRUNCATE_FRACTIONAL(mode)  ((CM_TIME_TRUNCATE_FRACTIONAL & (mode)) != 0)
#define CM_IS_ERROR_FOR_DIVISION_BY_ZERO(mode)    \
  ((CM_ERROR_FOR_DIVISION_BY_ZERO & (mode)) != 0)
#define CM_IS_NO_ZERO_IN_DATE(mode)           ((CM_NO_ZERO_IN_DATE & (mode)) != 0)
#define CM_IS_NO_ZERO_DATE(mode)              ((CM_NO_ZERO_DATE & (mode)) != 0)
#define CM_IS_ALLOW_INVALID_DATES(mode)       ((CM_ALLOW_INVALID_DATES & (mode)) != 0)
#define CM_IS_STRING_INTEGER_TRUNC(mode)      ((CM_STRING_INTEGER_TRUNC & (mode)) != 0)
#define CM_UNSET_STRING_INTEGER_TRUNC(mode)   ((~CM_STRING_INTEGER_TRUNC & (mode)))
#define CM_IS_IGNORE_ON_TRUNC(mode)           (!CM_IS_FAIL_ON_ROUNDING(mode))
#define CM_IS_ZERO_FILL(mode)                 ((CM_ZERO_FILL & (mode)) != 0)
#define CM_IS_FORMAT_NUMBER_WITH_LIMIT(mode)      \
  ((CM_FORMAT_NUMBER_WITH_LIMIT & (mode)) != 0)
#define CM_IS_IGNORE_CHARSET_CONVERT_ERR(mode)    \
  ((CM_CHARSET_CONVERT_IGNORE_ERR & (mode)) != 0)
#define CM_IS_SET_MIN_IF_OVERFLOW(mode)       ((CM_SET_MIN_IF_OVERFLOW & (mode)) != 0)
#define CM_IS_ERROR_ON_SCALE_OVER(mode)       ((CM_ERROR_ON_SCALE_OVER & (mode)) != 0)
#define CM_IS_STRICT_JSON(mode)               ((CM_STRICT_JSON & (mode)) != 0)
#define CM_IS_JSON_VALUE(mode)                CM_IS_ERROR_ON_SCALE_OVER(mode)
#define CM_IS_TO_COLUMN_CS_LEVEL(mode)        ((CM_TO_COLUMN_CS_LEVEL & (mode)) != 0)
// for json type cast
#define CM_IS_SQL_AS_JSON_SCALAR(mode)        ((CM_SQL_TO_JSON_SCALAR & (mode)) != 0)
#define CM_SET_SQL_AS_JSON_SCALAR(mode)       (CM_SQL_TO_JSON_SCALAR | (mode))
// for geomerty type cast
#define CM_IS_GEOMETRY_GEOMETRY(mode)             ((((mode) >> 12) & 0x1F) == 0)
#define CM_IS_GEOMETRY_POINT(mode)                ((((mode) >> 12) & 0x1F) == 1)
#define CM_IS_GEOMETRY_LINESTRING(mode)           ((((mode) >> 12) & 0x1F) == 2)
#define CM_IS_GEOMETRY_POLYGON(mode)              ((((mode) >> 12) & 0x1F) == 3)
#define CM_IS_GEOMETRY_MULTIPOINT(mode)           ((((mode) >> 12) & 0x1F) == 4)
#define CM_IS_GEOMETRY_MULTILINESTRING(mode)      ((((mode) >> 12) & 0x1F) == 5)
#define CM_IS_GEOMETRY_MULTIPOLYGON(mode)         ((((mode) >> 12) & 0x1F) == 6)
#define CM_IS_GEOMETRY_GEOMETRYCOLLECTION(mode)   ((((mode) >> 12) & 0x1F) == 7)
#define CM_SET_GEOMETRY_GEOMETRY(mode)            ((mode) &= 0xFFFE0FFF, (mode) |= (0 << 12))
#define CM_SET_GEOMETRY_POINT(mode)               ((mode) &= 0xFFFE0FFF, (mode) |= (1 << 12))
#define CM_SET_GEOMETRY_LINESTRING(mode)          ((mode) &= 0xFFFE0FFF, (mode) |= (2 << 12))
#define CM_SET_GEOMETRY_POLYGON(mode)             ((mode) &= 0xFFFE0FFF, (mode) |= (3 << 12))
#define CM_SET_GEOMETRY_MULTIPOINT(mode)          ((mode) &= 0xFFFE0FFF, (mode) |= (4 << 12))
#define CM_SET_GEOMETRY_MULTILINESTRING(mode)     ((mode) &= 0xFFFE0FFF, (mode) |= (5 << 12))
#define CM_SET_GEOMETRY_MULTIPOLYGON(mode)        ((mode) &= 0xFFFE0FFF, (mode) |= (6 << 12))
#define CM_SET_GEOMETRY_GEOMETRYCOLLECTION(mode)  ((mode) &= 0xFFFE0FFF, (mode) |= (7 << 12))
#define CM_IS_DEMOTE_CAST(mode)                   ((CM_DEMOTE_CAST & (mode)) != 0)

#define CM_GET_CS_LEVEL(mode)                     (((mode) >> CM_CS_LEVEL_SHIFT) & CM_CS_LEVEL_MASK)
#define CM_SET_CS_LEVEL(mode, level) \
  ((mode) &= ~(CM_CS_LEVEL_MASK << CM_CS_LEVEL_SHIFT), \
  (mode) |= ((level & CM_CS_LEVEL_MASK) << CM_CS_LEVEL_SHIFT))
#define CM_IS_ADD_ZEROFILL(mode)                 ((CM_ADD_ZEROFILL & (mode)) != 0)

#define CM_IS_CONST_TO_DECIMAL_INT(mode)                                                           \
  ((((mode)&CM_CONST_TO_DECIMAL_INT_UP) != 0) || (((mode)&CM_CONST_TO_DECIMAL_INT_DOWN) != 0)      \
   || (((mode)&CM_CONST_TO_DECIMAL_INT_EQ) != 0))
#define CM_IS_BY_TRANSFORMER(mode) ((CM_BY_TRANSFORMER & (mode)) != 0)
#define CM_SET_BY_TRANSFORMERN(mode)  (CM_BY_TRANSFORMER | (mode))
#define CM_IS_INTERNAL_CAST_IGNORE(mode)         ((CM_INTERNAL_CAST_IGNORE & (mode)) != 0)

struct DatumCastParams
{
  // add params when necessary
  DEFINE_ALLOCATOR_WRAPPER
  DatumCastParams()
    : allocator_(NULL),
      allocator_v2_(NULL),
      cur_time_(0),
      cast_mode_(CM_NONE),
      warning_(OB_SUCCESS),
      zf_info_(NULL),
      dest_collation_(CS_TYPE_INVALID),
      expect_obj_collation_(CS_TYPE_INVALID),
      res_accuracy_(NULL),
      dtc_params_(),
      format_number_with_limit_(true),
      is_ignore_(false),
      runtime_(nullptr),
      gen_query_range_(false),
      dest_max_length_(LENGTH_UNKNOWN_YET),
      srs_provider_(nullptr),
      lob_read_service_(nullptr),
      json_max_depth_(100)
  {}

  DatumCastParams(ObIAllocator *allocator_v2, const ObDataTypeCastParams *dtc_params,
                  ObCastMode cast_mode, ObCollationType dest_collation,
                  ObAccuracy *res_accuracy = NULL)
    : allocator_(NULL),
      allocator_v2_(allocator_v2),
      cur_time_(0),
      cast_mode_(cast_mode),
      warning_(OB_SUCCESS),
      zf_info_(NULL),
      dest_collation_(dest_collation),
      expect_obj_collation_(dest_collation),
      res_accuracy_(res_accuracy),
      dtc_params_(),
      format_number_with_limit_(true),
      is_ignore_(false),
      runtime_(nullptr),
      gen_query_range_(false),
      dest_max_length_(LENGTH_UNKNOWN_YET),
      srs_provider_(nullptr),
      lob_read_service_(nullptr),
      json_max_depth_(100)
  {
    if (NULL != dtc_params) {
      dtc_params_ = *dtc_params;
    }
  }

  DatumCastParams(ObIAllocator *allocator_v2, const ObDataTypeCastParams *dtc_params,
                  int64_t cur_time, ObCastMode cast_mode,
                  ObCollationType dest_collation,
                  const ObZerofillInfo *zf_info = NULL,
                  ObAccuracy *res_accuracy = NULL)
    : allocator_(NULL),
      allocator_v2_(allocator_v2),
      cur_time_(cur_time),
      cast_mode_(cast_mode),
      warning_(OB_SUCCESS),
      zf_info_(zf_info),
      dest_collation_(dest_collation),
      expect_obj_collation_(dest_collation),
      res_accuracy_(res_accuracy),
      dtc_params_(),
      format_number_with_limit_(true),
      is_ignore_(false),
      runtime_(nullptr),
      gen_query_range_(false),
      dest_max_length_(LENGTH_UNKNOWN_YET),
      srs_provider_(nullptr),
      lob_read_service_(nullptr),
      json_max_depth_(100)
  {
    if (NULL != dtc_params) {
      dtc_params_ = *dtc_params;
    }
  }

  void *alloc(const int64_t size) const
  {
    void *ret = NULL;
    if (NULL != allocator_v2_) {
      ret = allocator_v2_->alloc(size);
    } else if (NULL != allocator_) {
      ret = allocator_->alloc(size);
    }
    return ret;
  }
  void *alloc(const int64_t size, const lib::ObMemAttr &attr) const
  {
    UNUSED(attr);
    return alloc(size);
  }

  void set_allow_invalid_dates_cast_mode()
  {
    cast_mode_ |= CM_ALLOW_INVALID_DATES;
  }

  TO_STRING_KV(K(cur_time_),
               KP(cast_mode_),
               K(warning_),
               K(dest_collation_),
               K(expect_obj_collation_),
               K(res_accuracy_),
               K(format_number_with_limit_),
               K(is_ignore_),
               K_(dest_max_length));

  IAllocator *allocator_;
  ObIAllocator *allocator_v2_;
  int64_t cur_time_;
  ObCastMode cast_mode_;
  int warning_;
  const ObZerofillInfo *zf_info_;
  ObCollationType dest_collation_;//seems like a global collection for one statement, not for each column
  ObCollationType expect_obj_collation_;//for each column obj
  ObAccuracy *res_accuracy_;
  ObDataTypeCastParams dtc_params_;
  bool format_number_with_limit_;
  bool is_ignore_;
  ObIDatumCastRuntime *runtime_;
  bool gen_query_range_;
  int32_t dest_max_length_;
  ObISrsProvider *srs_provider_;
  ObILobReadService *lob_read_service_;
  int32_t json_max_depth_;
};

typedef DatumCastParams ObCastCtx;
} }
