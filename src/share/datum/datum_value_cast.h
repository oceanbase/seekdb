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
#include "common/datum/datum_value.h"
#include "share/datum/datum_cast_context.h"
namespace oceanbase { namespace common {
// Native common-value conversion. The result owns its payload in result_owner;
// it never borrows the caller's input, conversion scratch or temporary arena.
// Supported input families are being migrated without an Obj fallback.
class DatumValueCaster
{
public:
  static int to_type(SqlValueType target, DatumCastParams &context,
      const DatumValue &input, ObIAllocator &result_owner, DatumValue &output);
  // Convert a numeric day interval to a datetime payload. This is distinct
  // from to_type(..., ObDateTimeType), which interprets numbers as calendar
  // date/time values.
  static int to_datetime_interval(SqlValueType target, DatumCastParams &context,
      const DatumValue &input, DatumValue &output);
};
// Null-safe SQL comparison: retain directly comparable input types; otherwise
// convert both inputs to the inferred comparison type and collation first.
class DatumValueComparator
{
public:
  static int compare(const DatumValue &left, const DatumValue &right,
      DatumCastParams &context, SqlValueType comparison_type,
      ObCollationType comparison_collation, int &result);
  static int equal(const DatumValue &left, const DatumValue &right,
      DatumCastParams &context, SqlValueType comparison_type,
      ObCollationType comparison_collation, bool &result);
};
// Native constant/parameter truth evaluation. NULL remains unknown, so both
// is_true and is_false return false for it.
class DatumValueEvaluator
{
public:
  static int is_true(const DatumValue &value, ObCastMode mode, bool &result);
  static int is_false(const DatumValue &value, ObCastMode mode, bool &result);
  static int is_true(const DatumValue &value, bool &result)
  { return is_true(value, CM_WARN_ON_FAIL, result); }
  static int is_false(const DatumValue &value, bool &result)
  { return is_false(value, CM_WARN_ON_FAIL, result); }
};
} }
