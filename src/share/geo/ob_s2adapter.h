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

#ifndef OCEANBASE_LIB_GEO_OB_S2ADAPTER_
#define OCEANBASE_LIB_GEO_OB_S2ADAPTER_

#include "share/geo/ob_spatial_mbr.h"
// winuser.h defines DIFFERENCE as a raster-op code (value 11),
// which conflicts with S2's OpType::DIFFERENCE enum. Undefine it here
// and restore it after all S2 includes so Windows code still compiles.
#ifdef _WIN32
#ifdef DIFFERENCE
#pragma push_macro("DIFFERENCE")
#undef DIFFERENCE
#define OB_S2_PUSHED_DIFFERENCE_
#endif
#endif
#include "s2/s1angle.h"
#include "s2/s2region_coverer.h"
#ifdef _WIN32
#ifdef OB_S2_PUSHED_DIFFERENCE_
#pragma pop_macro("DIFFERENCE")
#undef OB_S2_PUSHED_DIFFERENCE_
#endif
#endif

namespace oceanbase {
namespace common {

struct ObSrsBoundsItem;
class ObGeometry;
class ObWkbToS2Visitor;

class ObS2Adapter final
{
public:
  ObS2Adapter(ObIAllocator *allocator, bool is_geog, bool is_query_window = false);
  ObS2Adapter(ObIAllocator *allocator, bool is_geog, double distance);

  ~ObS2Adapter();
  static void get_child_of_cellid(uint64_t id, uint64_t &child_start, uint64_t &child_end);
  int64_t get_ancestors(uint64_t cell, ObS2Cellids &cells);
  int64_t init(const ObString &wkb, const ObSrsBoundsItem *bound = NULL);
  int64_t get_cellids(ObS2Cellids &cells, bool is_query);
  int64_t get_cellids_and_unrepeated_ancestors(ObS2Cellids &cells, ObS2Cellids &ancestors);
  int64_t get_inner_cover_cellids(ObS2Cellids &cells);
  int64_t get_mbr(ObSpatialMBR &mbr);
private:
  S2RegionCoverer::Options options_;
  ObIAllocator *allocator_;
  ObWkbToS2Visitor *visitor_;
  ObGeometry *geo_;
  bool is_geog_;
  bool need_buffer_;
  S1Angle distance_;
  DISALLOW_COPY_AND_ASSIGN(ObS2Adapter);
};
} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_OB_S2ADAPTER_
