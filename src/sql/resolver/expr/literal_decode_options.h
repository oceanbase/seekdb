/*
 * Copyright (c) 2026 OceanBase.
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

#include "common/sql_mode/ob_sql_mode.h"
#include "lib/charset/ob_charset.h"
#include "share/ob_compatibility_control.h"
#include "sql/resolver/expr/numeric_literal.h"
#include "rust/sql_parser.h"

namespace oceanbase
{
namespace common
{
class ObTimeZoneInfo;
}
namespace sql
{
using NumericSignMode = peg::NumericSignMode;

struct LiteralDecodeOptions
{
  NumericLiteralOptions numeric;
  common::ObCollationType connection_collation;
  const common::ObTimeZoneInfo *time_zone;
  ObSQLMode sql_mode;
  share::ObCompatType compat_type;
  bool mysql_dates;
  bool placeholder_as_int;
  const common::ObString *charset;
  NumericSignMode numeric_sign;
};
}
}
