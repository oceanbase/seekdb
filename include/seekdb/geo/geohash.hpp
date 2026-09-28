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

#include <cmath>
#include <cstdint>

namespace seekdb { namespace geo { namespace geohash {

struct Bounds { double xmin, ymin, xmax, ymax; };
// ObGeoBoxUtil comparisons used by ObExprPrivSTGeoHash. Encoding itself uses
// exact >= comparisons; using this tolerance there changes boundary hashes.
constexpr double tolerance = 5e-14;
inline bool less(double a, double b) { return a + tolerance < b; }
inline bool greater(double a, double b) { return a - tolerance > b; }

inline bool valid_bounds(const Bounds &box)
{
  return std::isfinite(box.xmin) && std::isfinite(box.xmax) &&
      std::isfinite(box.ymin) && std::isfinite(box.ymax) &&
      !greater(box.xmin, box.xmax) && !greater(box.ymin, box.ymax) &&
      !less(box.xmin, -180) && !greater(box.xmax, 180) &&
      !less(box.ymin, -90) && !greater(box.ymax, 90);
}

// Extracted from calc_precision, including the original lon-then-lat step and
// point tolerance. A geometry crossing a cell split may have precision zero.
inline int automatic_precision(const Bounds &box, Bounds &cell)
{
  if (std::abs(box.xmin - box.xmax) <= tolerance &&
      std::abs(box.ymin - box.ymax) <= tolerance) return 20;
  cell = {-180, -90, 180, 90};
  int bits = 0;
  for (;;) {
    const double lon_width = cell.xmax - cell.xmin;
    const double lat_width = cell.ymax - cell.ymin;
    if (greater(box.xmin, cell.xmin + lon_width / 2)) cell.xmin += lon_width / 2;
    else if (less(box.xmax, cell.xmax - lon_width / 2)) cell.xmax -= lon_width / 2;
    else break;
    ++bits;
    if (greater(box.ymin, cell.ymin + lat_width / 2)) cell.ymin += lat_width / 2;
    else if (less(box.ymax, cell.ymax - lat_width / 2)) cell.ymax -= lat_width / 2;
    else break;
    ++bits;
  }
  return bits / 5;
}

// Extracted from calc_geohash; append returns zero or the caller's native error
// code. uint64_t bit iteration avoids overflowing precision * 5 for int32 input.
template <typename Append>
int encode(const Bounds &box, uint32_t precision, Append append)
{
  const char base32[] = "0123456789bcdefghjkmnpqrstuvwxyz";
  const double lon = box.xmin + (box.xmax - box.xmin) / 2;
  const double lat = box.ymin + (box.ymax - box.ymin) / 2;
  double lo[2] = {-180, -90}, hi[2] = {180, 90};
  const double coordinate[2] = {lon, lat};
  unsigned character = 0, bit = 0;
  for (uint64_t i = 0; i < uint64_t(precision) * 5; ++i) {
    const unsigned axis = i & 1;
    const double midpoint = (lo[axis] + hi[axis]) / 2;
    if (coordinate[axis] >= midpoint) {
      character |= 1u << (4 - bit);
      lo[axis] = midpoint;
    } else hi[axis] = midpoint;
    if (bit < 4) ++bit;
    else {
      const int status = append(base32[character]);
      if (status != 0) return status;
      bit = character = 0;
    }
  }
  return 0;
}

}}} // namespace seekdb::geo::geohash
