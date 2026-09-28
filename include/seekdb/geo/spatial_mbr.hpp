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
#include <cstddef>
#include <cstring>

namespace seekdb::geo::index_mbr {
struct Box {
  double xmin, xmax, ymin, ymax;
};

inline bool finite(const Box &box)
{
  return std::isfinite(box.xmin) && std::isfinite(box.xmax) &&
         std::isfinite(box.ymin) && std::isfinite(box.ymax);
}

// Original ObSpatialMBR row encoding: a point is [xmin,ymin]; all other
// geometry is [ymin,ymax,xmin,xmax]. Native-endian doubles, not SPI structs.
// Longitude can wrap. Interpretation/axis validation belongs to the filter.
inline bool decode(const void *data, size_t size, bool point, Box &result)
{
  if (data == nullptr || size != (point ? 2 : 4) * sizeof(double)) return false;
  double values[4]{};
  std::memcpy(values, data, size);
  const Box box = point ? Box{values[0], values[0], values[1], values[1]}
                        : Box{values[2], values[3], values[0], values[1]};
  if (!finite(box)) return false;
  result = box;
  return true;
}

inline bool encode(const Box &box, bool point, void *data, size_t capacity, size_t &size)
{
  size = 0;
  const size_t needed = (point ? 2 : 4) * sizeof(double);
  if (!finite(box) || data == nullptr || capacity < needed) return false;
  const double values[] = {point ? box.xmin : box.ymin, point ? box.ymin : box.ymax,
                           box.xmin, box.xmax};
  std::memcpy(data, values, needed);
  size = needed;
  return true;
}
} // namespace seekdb::geo::index_mbr
