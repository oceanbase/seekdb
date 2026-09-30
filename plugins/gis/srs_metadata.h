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
#include "srs_parser.h"
#include "seekdb/geo/srs_semantics.hpp"
#include <cstdint>
#include <utility>

namespace seekdb::gis::srs {
enum class PrepareStatus { ok, invalid_value, invalid_authority, missing_parameter };
struct Metadata {
  uint32_t srid = 0;
  bool geographic = false, is_wgs84 = false;
  int projection_method = 0; // Original unknown-projection fallback is zero.
  double semi_major = NAN, inverse_flattening = NAN, prime_meridian = NAN;
  double angular_unit = NAN, linear_unit = 1;
  std::array<double, 7> towgs84{{NAN, NAN, NAN, NAN, NAN, NAN, NAN}};
  int axis0 = 0, axis1 = 0, geographic_axis0 = 0, geographic_axis1 = 0;
  std::vector<std::pair<int, double>> parameters;
  bool has_towgs84() const { return !std::isnan(towgs84[0]); }
  seekdb::geo::srs::Coordinates coordinates() const
  { return {geographic, axis0, axis1, angular_unit, prime_meridian}; }
};
// Original semantic preparation, not a projection engine. Retains unknown
// method fallback and ordered required parameters; failed preparation does not
// overwrite out. Allocation errors are handled by the enclosing ABI adapter.
PrepareStatus prepare(uint64_t srid, const CoordinateSystem &, Metadata &out);

// Decimal authority parsing mirrors the original binary-charset strntoll:
// ASCII whitespace/sign, numeric prefix, EDOM/ERANGE and signed-64-bit limits.
// Integer narrowing at the legacy EPSG call sites is retained explicitly.
int authority_code(std::string_view text, int &out);
} // namespace seekdb::gis::srs

