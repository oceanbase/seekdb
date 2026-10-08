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
#include <cmath>
#include <cstdint>

namespace seekdb::geo::srs {
// The ordinal values match the legacy WKT directions. These helpers operate
// only on numeric metadata; no host/S2/Boost object enters the plugin ABI.
struct Coordinates {
  bool geographic;
  int axis0, axis1;
  double angular_unit, prime_meridian;
  bool latitude_first() const { return geographic && (axis0 == 2 || axis0 == 4); }
  bool north() const { return (latitude_first() && axis0 == 4) || axis1 == 4; }
  bool east() const { return (latitude_first() && axis1 == 1) || axis0 == 1; }
  bool usable() const { return geographic && angular_unit > 0.0; }
  bool to_radians(double value, double &out) const
  { if (!usable()) return false; out = value * angular_unit; return true; }
  bool from_radians(double value, double &out) const
  { if (!usable()) return false; out = value / angular_unit; return true; }
  bool latitude_to_radians(double value, double &out) const
  { if (!usable()) return false; const double r = value * angular_unit; out = north() ? r : -r; return true; }
  bool latitude_from_radians(double value, double &out) const
  { if (!usable()) return false; const double r = value / angular_unit; out = north() ? r : -r; return true; }
  bool longitude_to_radians(double value, double &out) const
  { if (!usable()) return false; out = ((east() ? value : -value) + prime_meridian) * angular_unit; return true; }
  bool longitude_from_radians(double value, double &out) const
  { if (!usable()) return false; const double r = value / angular_unit - prime_meridian; out = east() ? r : -r; return true; }
};

inline double semi_minor_axis(bool geographic, double semi_major, double inverse_flattening)
{
  return geographic ? (inverse_flattening != 0.0 ? semi_major * (1 - 1 / inverse_flattening) : semi_major) : 0.0;
}

inline bool valid_geographic(uint32_t srid, double semi_major, double inverse_flattening,
                            double prime_meridian, double angular_unit)
{
  return srid < UINT32_MAX && std::isfinite(semi_major) && std::isfinite(inverse_flattening) &&
      std::isfinite(prime_meridian) && std::isfinite(angular_unit);
}

// Preserve original authority-check order and early exits. Check returns 0 on
// success, otherwise its caller-specific numeric error, and sets the bool.
template <typename Geographic, typename Check>
int is_wgs84(const Geographic &rs, Check check, bool &out)
{
  int ret = check(rs.authority, "EPSG", 4326, false, out);
  if (ret != 0 || !out) return ret;
  if (int(rs.axis.x.direction) != 4 || int(rs.axis.y.direction) != 1) { out = false; return 0; }
  ret = check(rs.datum_info.spheroid.authority, "EPSG", 7030, true, out);
  if (ret != 0 || !out) return ret;
  if (rs.datum_info.spheroid.semi_major_axis != 6378137.0 ||
      rs.datum_info.spheroid.inverse_flattening != 298.257223563) { out = false; return 0; }
  ret = check(rs.datum_info.authority, "EPSG", 6326, true, out);
  if (ret != 0 || !out) return ret;
  ret = check(rs.primem.authority, "EPSG", 8901, true, out);
  if (ret != 0 || !out) return ret;
  if (rs.primem.longtitude != 0.0) { out = false; return 0; }
  ret = check(rs.unit.authority, "EPSG", 9122, true, out);
  if (ret != 0 || !out) return ret;
  if (rs.unit.conversion_factor != 0.017453292519943278) { out = false; return 0; }
  if (rs.datum_info.towgs84.is_valid)
    for (double v : rs.datum_info.towgs84.value) if (v != 0.0) { out = false; break; }
  return 0;
}
} // namespace seekdb::geo::srs

