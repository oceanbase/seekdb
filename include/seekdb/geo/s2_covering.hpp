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

// Source-level extraction of ObWkbToS2Visitor's index-covering algorithms.
// S2 and STL types are private implementation details, never plugin ABI types.
#include <cfloat>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>
#include "s2/s2cell_id.h"
#include "s2/s2cell_union.h"
#include "s2/s2latlng_rect.h"
#include "s2/s2latlng_rect_bounder.h"
#include "s2/s2region_coverer.h"

namespace seekdb::geo::s2_index {

inline constexpr uint64_t outside_bounds_cell = UINT64_MAX;
inline constexpr double bounds_margin = 0.01;

struct CellInfo {
  uint64_t cell = 0, range_min = 0, range_max = 0;
  uint32_t ancestor_count = 0;
  std::array<uint64_t, 30> ancestors{};
};

inline bool cell_info(uint64_t id, CellInfo &out)
{
  CellInfo value{};
  value.cell = id;
  if (id == outside_bounds_cell) {
    value.range_min = value.range_max = id;
  } else {
    const S2CellId cell(id);
    // level()/parent()/range_* must not be called on S2CellId::None or an
    // arbitrary invalid integer (including the outside-bounds sentinel).
    if (!cell.is_valid()) return false;
    value.range_min = cell.range_min().id();
    value.range_max = cell.range_max().id();
    for (int level = cell.level() - 1; level >= 0; --level) {
      value.ancestors[value.ancestor_count++] = cell.parent(level).id();
    }
  }
  out = value;
  return true;
}

struct ProjectionBounds {
  double xmin, xmax, ymin, ymax;
};

inline bool valid_bounds(const ProjectionBounds &bounds)
{
  return std::isfinite(bounds.xmin) && std::isfinite(bounds.xmax) &&
         std::isfinite(bounds.ymin) && std::isfinite(bounds.ymax) &&
         bounds.xmax > bounds.xmin && bounds.ymax > bounds.ymin &&
         std::isfinite(bounds.xmax - bounds.xmin) &&
         std::isfinite(bounds.ymax - bounds.ymin);
}

inline bool outside_bounds(const ProjectionBounds &bounds, double x, double y)
{
  const double dx = bounds_margin * (bounds.xmax - bounds.xmin);
  const double dy = bounds_margin * (bounds.ymax - bounds.ymin);
  return !valid_bounds(bounds) || !std::isfinite(x) || !std::isfinite(y) ||
         x < bounds.xmin + dx || x > bounds.xmax - dx ||
         y < bounds.ymin + dy || y > bounds.ymax - dy;
}

inline double st_to_uv(double value)
{
  return value >= 0.5 ? (1.0 / 3.0) * (4.0 * value * value - 1.0)
      : (1.0 / 3.0) * (1.0 - 4.0 * (1.0 - value) * (1.0 - value));
}

inline bool project_point(const ProjectionBounds &bounds, double x, double y, S2Point &point)
{
  if (outside_bounds(bounds, x, y)) return false;
  const double s = (x - bounds.xmin) / (bounds.xmax - bounds.xmin);
  const double t = (y - bounds.ymin) / (bounds.ymax - bounds.ymin);
  point = S2Point(1, st_to_uv(s), st_to_uv(t));
  return true;
}

inline S2RegionCoverer::Options options(bool query_window = false)
{
  S2RegionCoverer::Options result;
  result.set_max_cells(query_window ? 50 : 4);
  result.set_max_level(30);
  result.set_level_mod(1);
  return result;
}

inline bool full_range(const S2CellUnion &cells, bool geographic)
{
  if (geographic) {
    if (cells.size() != 6) return false;
    for (const auto cell : cells) if (cell.level() != 0) return false;
    return true;
  }
  for (const auto cell : cells) if (cell.face() != 0) return true;
  return false;
}

// Preserve the original per-region covering + union and full-range fallback.
// A fallback uses the margin-expanded accumulated edge bound, not a centroid
// and not a single representative cell. Publish only after allocations succeed.
inline void cover_regions(const std::vector<std::unique_ptr<S2Region>> &regions,
                          const S2RegionCoverer::Options &options, bool geographic,
                          const S2LatLngRectBounder &bounder,
                          S2CellUnion &cells, S2LatLngRect &mbr,
                          std::vector<S2CellId> &vertex_cells)
{
  S2RegionCoverer coverer(options);
  S2CellUnion next;
  for (const auto &region : regions) next = next.Union(coverer.GetCovering(*region));
  if (full_range(next, geographic)) {
    const auto margin = S2LatLng::FromDegrees(0.00001, 0.00001);
    const auto rect = bounder.GetBound().Expanded(margin);
    next = coverer.GetCovering(rect);
    std::vector<S2CellId> replacement;
    for (int i = 0; i < 4; ++i) {
      replacement.push_back(S2CellId(rect.GetVertex(i)).parent(options.max_level()));
    }
    vertex_cells.swap(replacement);
    mbr = rect;
  }
  cells = std::move(next);
}

inline S2CellUnion query_cover(const S2CellUnion &source, bool multiple_regions,
                              bool need_buffer, S1Angle distance)
{
  // Do not expand the stored cover in-place. Obtaining write/query/ancestor
  // views repeatedly must not grow the same query window multiple times.
  S2CellUnion cells = source;
  if (need_buffer) cells.Expand(distance, 2);
  if (multiple_regions) cells.Normalize();
  return cells;
}

inline std::vector<uint64_t> cell_ids(const S2CellUnion &source,
    const S2RegionCoverer::Options &options, bool multiple_regions,
    bool invalid, bool was_reset, bool query, bool need_buffer, S1Angle distance)
{
  if (invalid) return {outside_bounds_cell};
  const auto cover = query_cover(source, multiple_regions, need_buffer, distance);
  std::vector<uint64_t> result;
  S2CellId previous = S2CellId::None();
  for (const auto cell : cover) {
    result.push_back(cell.id());
    if (query) {
      for (int level = cell.level() - options.level_mod(); level >= options.min_level();
           level -= options.level_mod()) {
        const auto parent = cell.parent(level);
        if (previous != S2CellId::None() && previous.level() > level &&
            previous.parent(level) == parent) break;
        result.push_back(parent.id());
      }
    }
    previous = cell;
  }
  if (was_reset) result.push_back(outside_bounds_cell);
  return result;
}

struct CellSets {
  std::vector<uint64_t> cells;
  std::vector<uint64_t> ancestors;
};

inline CellSets cells_and_ancestors(const S2CellUnion &source,
    const S2RegionCoverer::Options &options, bool multiple_regions,
    bool invalid, bool was_reset, bool need_buffer, S1Angle distance)
{
  CellSets result;
  if (invalid) {
    result.cells.push_back(outside_bounds_cell);
    return result;
  }
  const auto cover = query_cover(source, multiple_regions, need_buffer, distance);
  std::unordered_set<uint64_t> seen;
  S2CellId previous = S2CellId::None();
  for (const auto cell : cover) {
    if (seen.insert(cell.id()).second) {
      result.cells.push_back(cell.id());
      for (int level = cell.level() - options.level_mod(); level >= options.min_level();
           level -= options.level_mod()) {
        const auto parent = cell.parent(level);
        if (previous != S2CellId::None() && previous.level() > level &&
            previous.parent(level) == parent) break;
        if (seen.insert(parent.id()).second) result.ancestors.push_back(parent.id());
      }
    }
    previous = cell;
  }
  if (was_reset) result.cells.push_back(outside_bounds_cell);
  return result;
}

// This is the original visitor's "inner cover": normalized cells of the input
// vertices. It is NOT S2RegionCoverer::GetInteriorCovering; query code relies on
// that distinction for the covered-by path.
inline std::vector<uint64_t> vertex_cell_ids(const std::vector<S2CellId> &vertices, bool invalid)
{
  if (invalid) return {outside_bounds_cell};
  S2CellUnion cells(vertices);
  cells.Normalize();
  std::vector<uint64_t> result;
  for (const auto cell : cells) result.push_back(cell.id());
  return result;
}

inline S2LatLngRect geographic_mbr(const S2LatLngRect &source, bool invalid,
                                   bool was_reset, bool need_buffer, S1Angle distance)
{
  if (invalid || was_reset) return S2LatLngRect::Full();
  if (source.is_empty()) return source;
  auto result = need_buffer ? source.ExpandedByDistance(distance) : source;
  return result.ExpandedByDistance(S1Angle::Degrees(DBL_EPSILON));
}

} // namespace seekdb::geo::s2_index
