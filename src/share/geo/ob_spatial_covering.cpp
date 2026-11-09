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

#define USING_LOG_PREFIX LIB
#include "ob_spatial_covering.h"
#include "rust/spatial_covering.h"
#include "share/geo/ob_srs_info.h"

namespace oceanbase {
namespace common {
namespace {

int covering_error(const spatial::CoveringResult &result)
{
  switch (result.code) {
    case spatial::ErrorCode::InvalidWkb:
      return OB_ERR_GIS_INVALID_DATA;
    case spatial::ErrorCode::InvalidArgument:
      return OB_INVALID_ARGUMENT;
    case spatial::ErrorCode::Unsupported:
      return OB_NOT_SUPPORTED;
    default:
      return OB_ERR_UNEXPECTED;
  }
}

int copy_cells(const rust::Vec<uint64_t> &source, ObSpatialCellIds &target)
{
  INIT_SUCC(ret);
  if (OB_FAIL(target.reserve(source.size()))) {
  } else {
    for (const uint64_t cell : source) {
      if (OB_FAIL(target.push_back(cell))) {
        break;
      }
    }
  }
  return ret;
}

} // namespace

int build_spatial_covering(const ObString &swkb,
                           bool is_geog,
                           const ObSrsBoundsItem *bounds,
                           ObSpatialCoveringKind kind,
                           double distance_radians,
                           ObSpatialCovering &covering)
{
  INIT_SUCC(ret);
  covering.cells_.clear();
  covering.ancestors_.clear();
  covering.ranges_.clear();
  covering.mbr_ = ObSpatialMBR();
  if (OB_ISNULL(swkb.ptr())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!is_geog && OB_ISNULL(bounds)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    spatial::CoveringKind rust_kind;
    switch (kind) {
      case ObSpatialCoveringKind::INDEX:
        rust_kind = spatial::CoveringKind::Index;
        break;
      case ObSpatialCoveringKind::QUERY:
        rust_kind = spatial::CoveringKind::Query;
        break;
      case ObSpatialCoveringKind::COVERED_BY:
        rust_kind = spatial::CoveringKind::CoveredBy;
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
        break;
    }
    if (OB_SUCC(ret)) {
      const spatial::Bounds rust_bounds = OB_ISNULL(bounds)
          ? spatial::Bounds {0.0, 0.0, 0.0, 0.0}
          : spatial::Bounds {bounds->minX_, bounds->maxX_, bounds->minY_, bounds->maxY_};
      const rust::Slice<const uint8_t> bytes(
          reinterpret_cast<const uint8_t *>(swkb.ptr()), static_cast<size_t>(swkb.length()));
      auto result = spatial::spatial_cover(
          bytes, is_geog, OB_NOT_NULL(bounds), rust_bounds, rust_kind, distance_radians);
      if (result.code != spatial::ErrorCode::None) {
        ret = covering_error(result);
        LOG_WARN("failed to build spatial covering", K(ret), "reason", result.message.c_str());
      } else if (OB_FAIL(copy_cells(result.cells, covering.cells_))) {
      } else if (OB_FAIL(copy_cells(result.ancestors, covering.ancestors_))) {
      } else if (OB_FAIL(covering.ranges_.reserve(result.ranges.size()))) {
      } else {
        for (const auto &range : result.ranges) {
          if (OB_FAIL(covering.ranges_.push_back(ObSpatialCellRange {range.start, range.end}))) {
            break;
          }
        }
        if (OB_SUCC(ret)) {
          covering.mbr_.x_min_ = result.mbr.x_min;
          covering.mbr_.x_max_ = result.mbr.x_max;
          covering.mbr_.y_min_ = result.mbr.y_min;
          covering.mbr_.y_max_ = result.mbr.y_max;
          covering.mbr_.is_point_ = result.mbr.is_point;
          covering.mbr_.is_geog_ = result.mbr.geographic;
        }
      }
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
