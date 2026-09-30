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
#include <string_view>

namespace seekdb::geo::pg {
// Copied from the original latitude/longitude visitor; the legacy visitor and
// plugin call the same implementation. In particular, retain the 1e-10 edge
// tolerance and the original 2D-versus-3D caller normalization policy.
inline double normalize_latitude(double lat)
{
  bool modified = false;
  const double TOLERANCE = 1e-10; // according to pg
  if (lat > 90.0 && (lat - 90) <= TOLERANCE) {
    lat = 90.0;
    modified = true;
  } else if (lat < -90.0 && (-90 - lat) <= TOLERANCE) {
    lat = -90.0;
    modified = true;
  }

  if (!modified) {
    if (lat > 360.0) {
      lat = std::remainder(lat, 360.0);
    }

    if (lat < -360.0) {
      lat = std::remainder(lat, -360.0);
    }

    if (lat > 180.0) {
      lat = 180.0 - lat;
    }

    if (lat < -180.0) {
      lat = -180.0 - lat;
    }

    if (lat > 90.0) {
      lat = 180.0 - lat;
    }

    if (lat < -90.0) {
      lat = -180.0 - lat;
    }
  }

  return lat;
}

inline double normalize_longitude(double lon)
{
  bool modified = false;
  const double TOLERANCE = 1e-10; // according to pg
  if (lon > 180.0 && (lon - 180) <= TOLERANCE) {
    lon = 180.0;
    modified = true;
  } else if (lon < -180.0 && (-180 - lon) <= TOLERANCE) {
    lon = -180.0;
    modified = true;
  }

  if (!modified) {
    if (lon > 360.0) {
      lon = std::remainder(lon, 360.0);
    }

    if (lon < -360.0) {
      lon = std::remainder(lon, -360.0);
    }

    if (lon > 180.0) {
      lon = -360.0 + lon;
    }

    if (lon < -180.0) {
      lon = 360 + lon;
    }

    if (lon == -180.0) {
      lon = 180.0;
    }

    if (lon == -360.0) {
      lon = 0.0;
    }
  }

  return lon;
}


inline bool parse_srid_prefix(std::string_view prefix, uint32_t &srid)
{
  const auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
  while (!prefix.empty() && space(prefix.front())) prefix.remove_prefix(1);
  while (!prefix.empty() && space(prefix.back())) prefix.remove_suffix(1);
  if (prefix.size() < 6) return false;
  constexpr std::string_view key = "srid=";
  for (size_t i = 0; i < key.size(); ++i) {
    char c = prefix[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != key[i]) return false;
  }
  uint32_t value = 0;
  for (size_t i = key.size(); i < prefix.size(); ++i) {
    const char c = prefix[i];
    if (c < '0' || c > '9' || value > (UINT32_MAX - uint32_t(c - '0')) / 10)
      return false;
    value = value * 10 + uint32_t(c - '0');
  }
  srid = value;
  return true;
}
} // namespace seekdb::geo::pg
