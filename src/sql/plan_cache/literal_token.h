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

#include "lib/string/ob_string.h"
#include "rust/sql_parser.h"

namespace oceanbase::sql
{
struct LiteralToken
{
  peg::LiteralKind kind;
  uint64_t integer;
  common::ObString text;
  common::ObString source;
  bool concatenated;
  int32_t first_part_length;

  bool is_numeric() const
  {
    return kind == peg::LiteralKind::Integer || kind == peg::LiteralKind::Unsigned
        || kind == peg::LiteralKind::Decimal || kind == peg::LiteralKind::Float;
  }

  bool display_source() const
  {
    return kind == peg::LiteralKind::Binary || kind == peg::LiteralKind::Date
        || kind == peg::LiteralKind::Time || kind == peg::LiteralKind::Timestamp;
  }

  TO_STRING_KV("kind", static_cast<int>(kind), K(integer), K(text), K(source));
};
}
