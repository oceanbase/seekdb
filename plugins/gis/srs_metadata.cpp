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
#include "srs_metadata.h"
#include "seekdb/geo/srs_projection_parameters.hpp"
#include <cerrno>
#include <limits>

namespace seekdb::gis::srs {
int authority_code(std::string_view text, int &out)
{
  size_t pos = 0;
  while (pos < text.size() && (text[pos] == ' ' || (text[pos] >= '\t' && text[pos] <= '\r'))) ++pos;
  bool negative = false;
  if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) negative = text[pos++] == '-';
  const size_t first = pos;
  uint64_t value = 0;
  const uint64_t limit = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0x7fffffffffffffff);
  bool overflow = false;
  for (; pos < text.size() && text[pos] >= '0' && text[pos] <= '9'; ++pos) {
    const unsigned digit = text[pos] - '0';
    if (value > (limit - digit) / 10) overflow = true;
    else if (!overflow) value = value * 10 + digit;
  }
  if (pos == first) return EDOM;
  if (overflow) return ERANGE;
  const int64_t signed_value = negative
      ? (value == UINT64_C(0x8000000000000000) ? INT64_MIN : -static_cast<int64_t>(value))
      : static_cast<int64_t>(value);
  out = static_cast<int>(signed_value);
  return 0;
}

static bool equal_ascii(std::string_view a, std::string_view b)
{
  if (a.size() != b.size()) return false;
  const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; };
  for (size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
  return true;
}

static int check_authority(const Authority &auth, const char *name, int code, bool optional, bool &match)
{
  match = true;
  if (!auth.is_valid) match = optional;
  else {
    int parsed = 0;
    const int ret = authority_code(auth.org_code, parsed);
    if (ret != 0) { match = false; return ret; }
    match = equal_ascii(auth.org_name, name) && parsed == code;
  }
  return 0;
}

PrepareStatus prepare(uint64_t srid, const CoordinateSystem &parsed, Metadata &out)
{
  if (srid >= UINT32_MAX) return PrepareStatus::invalid_value;
  const auto *projected = boost::get<Projected>(&parsed);
  int method = 0;
  if (projected && authority_code(projected->projection.authority.org_code, method) != 0)
    return PrepareStatus::invalid_authority;
  const auto &geo = projected ? projected->projected_rs : boost::get<Geographic>(parsed);
  Metadata result;
  result.srid = static_cast<uint32_t>(srid);
  result.geographic = projected == nullptr;
  result.semi_major = geo.datum_info.spheroid.semi_major_axis;
  result.inverse_flattening = geo.datum_info.spheroid.inverse_flattening;
  result.prime_meridian = geo.primem.longtitude;
  result.angular_unit = geo.unit.conversion_factor;
  result.geographic_axis0 = int(geo.axis.x.direction);
  result.geographic_axis1 = int(geo.axis.y.direction);
  if (!seekdb::geo::srs::valid_geographic(result.srid, result.semi_major,
      result.inverse_flattening, result.prime_meridian, result.angular_unit)) return PrepareStatus::invalid_value;
  if (geo.datum_info.towgs84.is_valid) {
    for (size_t i = 0; i < result.towgs84.size(); ++i) {
      if (std::isinf(geo.datum_info.towgs84.value[i])) return PrepareStatus::invalid_value;
      result.towgs84[i] = geo.datum_info.towgs84.value[i];
    }
  }
  if (seekdb::geo::srs::is_wgs84(geo, check_authority, result.is_wgs84) != 0) return PrepareStatus::invalid_authority;
  result.axis0 = result.geographic_axis0; result.axis1 = result.geographic_axis1;
  if (projected) {
    result.linear_unit = projected->unit.conversion_factor;
    result.axis0 = int(projected->axis.x.direction); result.axis1 = int(projected->axis.y.direction);
    if (std::isnan(result.linear_unit) || ((result.axis0 == 0) != (result.axis1 == 0))) return PrepareStatus::invalid_value;
    const auto *schema = seekdb::geo::srs::find_projection(method);
    if (schema != nullptr) {
      result.projection_method = method;
      for (size_t i = 0; i < schema->count; ++i) result.parameters.emplace_back(schema->codes[i], NAN);
      for (const auto &parameter : projected->proj_params) {
        if (!equal_ascii(parameter.authority.org_name, "EPSG")) continue;
        int code = 0;
        if (authority_code(parameter.authority.org_code, code) != 0) return PrepareStatus::invalid_authority;
        for (auto &required : result.parameters) {
          if (required.first == code) { required.second = parameter.value; break; }
        }
      }
      for (const auto &required : result.parameters)
        if (std::isnan(required.second)) return PrepareStatus::missing_parameter;
    }
  }
  out = std::move(result);
  return PrepareStatus::ok;
}
} // namespace seekdb::gis::srs
