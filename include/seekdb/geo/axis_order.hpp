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
#include <string_view>

namespace seekdb::geo::srs {
enum class AxisOrder { srid_defined, long_lat, lat_long };
// The original ObGeoExprUtils option grammar, with bounded ASCII scanning.
// No locale-dependent ctype call or access beyond the supplied byte range.
inline bool parse_axis_order(std::string_view text, AxisOrder &order)
{
  const auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
  const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; };
  const auto equal = [&](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != b[i]) return false;
    return true;
  };
  while (!text.empty() && space(text.front())) text.remove_prefix(1);
  while (!text.empty() && space(text.back())) text.remove_suffix(1);
  order = AxisOrder::srid_defined;
  if (text.empty()) return true;
  const auto separator = text.find('=');
  if (separator == std::string_view::npos) return false;
  auto key = text.substr(0, separator), value = text.substr(separator + 1);
  while (!key.empty() && space(key.back())) key.remove_suffix(1);
  while (!value.empty() && space(value.front())) value.remove_prefix(1);
  if (!equal(key, "axis-order")) return false;
  if (equal(value, "long-lat")) order = AxisOrder::long_lat;
  else if (equal(value, "lat-long")) order = AxisOrder::lat_long;
  else if (!equal(value, "srid-defined")) return false;
  return true;
}
} // namespace seekdb::geo::srs

