/*
 * Copyright (c) 2026 OceanBase.
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

namespace oceanbase { namespace sql {

// Initial native defaults are context-independent numeric/boolean/NULL
// literals. General expressions need definition-time binding and durable
// dependencies: never resolve their unbound text in the caller's database.
// Check wire/catalog values too, not only CREATE's parser node. This is an
// admission check, not an evaluator; SQL still handles numeric conversion.
struct NativeFunctionDefault final
{
  static bool is_null(const common::ObString &text)
  { return trim(text).case_compare("NULL") == 0; }

  static bool supported(const common::ObString &text)
  {
    if (text.length() <= 0 || text.ptr() == nullptr) return false;
    const auto trimmed = trim(text);
    const char *begin = trimmed.ptr(), *end = begin + trimmed.length();
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (trimmed.case_compare("NULL") == 0 || trimmed.case_compare("TRUE") == 0 ||
        trimmed.case_compare("FALSE") == 0) return true;
    if (begin < end && (*begin == '+' || *begin == '-')) {
      ++begin;
      while (begin < end && space(*begin)) ++begin;
    }
    bool digits = false;
    while (begin < end && digit(*begin)) { ++begin; digits = true; }
    if (begin < end && *begin == '.') {
      ++begin;
      while (begin < end && digit(*begin)) { ++begin; digits = true; }
    }
    if (!digits) return false;
    if (begin < end && (*begin == 'e' || *begin == 'E')) {
      ++begin;
      if (begin < end && (*begin == '+' || *begin == '-')) ++begin;
      const char *exponent = begin;
      while (begin < end && digit(*begin)) ++begin;
      if (exponent == begin) return false;
    }
    return begin == end;
  }

private:
  static bool space(char c)
  { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }

  static common::ObString trim(const common::ObString &text)
  {
    if (text.length() <= 0 || text.ptr() == nullptr) return common::ObString();
    const char *begin = text.ptr(), *end = begin + text.length();
    while (begin < end && space(*begin)) ++begin;
    while (end > begin && space(end[-1])) --end;
    return common::ObString(end - begin, begin);
  }
};

} }
