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

#include <cstdint>
#include "lib/string/ob_string.h"
#include "share/statement/ob_stmt_type.h"

namespace oceanbase::sql
{
enum class NumericKind
{
  Integer,
  Unsigned,
  Decimal,
  Float,
  Double,
};

struct NumericLiteral
{
  NumericKind kind;
  uint64_t integer;
  common::ObString text;
};

struct NumericLiteralOptions
{
  stmt::StmtType statement_type;
  bool decimal_int;
  bool from_pl;
  bool arithmetic;
  int8_t min_integer_precision;
};
}
