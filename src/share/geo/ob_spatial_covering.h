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

#include "lib/container/ob_vector.h"
#include "share/geo/ob_spatial_mbr.h"

namespace oceanbase {
namespace common {

struct ObSrsBoundsItem;
typedef common::ObVector<uint64_t> ObSpatialCellIds;

struct ObSpatialCellRange {
  uint64_t start_;
  uint64_t end_;
};

typedef common::ObVector<ObSpatialCellRange> ObSpatialCellRanges;

enum class ObSpatialCoveringKind {
  INDEX,
  QUERY,
  COVERED_BY,
};

struct ObSpatialCovering {
  ObSpatialCellIds cells_;
  ObSpatialCellIds ancestors_;
  ObSpatialCellRanges ranges_;
  ObSpatialMBR mbr_;
};

int build_spatial_covering(const ObString &swkb,
                           bool is_geog,
                           const ObSrsBoundsItem *bounds,
                           ObSpatialCoveringKind kind,
                           double distance_radians,
                           ObSpatialCovering &covering);

} // namespace common
} // namespace oceanbase
