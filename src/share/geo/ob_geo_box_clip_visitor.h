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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_BOX_CLIP_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_BOX_CLIP_VISITOR_

#include "share/geo/ob_geo_visitor.h"
#include "share/geo/ob_geo_utils.h"
#include "share/geo/ob_geo_common.h"

namespace oceanbase
{
namespace common
{
enum ObBoxPosition {
  INVALID = 0,
  INSIDE = 1,
  OUTSIDE = 2,
  LEFT_EDGE = 4,
  RIGHT_EDGE = 8,
  TOP_EDGE = 16,
  BOTTOM_EDGE = 32,
  TOPLEFT_CORNER = TOP_EDGE | LEFT_EDGE,
  TOPRIGHT_CORNER = TOP_EDGE | RIGHT_EDGE,
  BOTTOMLEFT_CORNER = BOTTOM_EDGE | LEFT_EDGE,
  BOTTOMRIGHT_CORNER = BOTTOM_EDGE | RIGHT_EDGE,
};

class ObGeoBoxClipVisitor : public ObEmptyGeoVisitor
{
public:
  explicit ObGeoBoxClipVisitor(const ObGeogBox &box, lib::MemoryContext &mem_ctx)
      : xmin_(box.xmin),
        ymin_(box.ymin),
        xmax_(box.xmax),
        ymax_(box.ymax),
        res_geo_(nullptr),
        allocator_(&mem_ctx->get_arena_allocator()),
        mem_ctx_(&mem_ctx)
  {
  }
  virtual ~ObGeoBoxClipVisitor()
  {}

  bool prepare(ObGeometry *geo) override;

  int visit(ObCartesianPoint *geo) override;
  int visit(ObCartesianLineString *geo) override;
  int visit(ObCartesianPolygon *geo) override;
  int visit(ObCartesianMultipoint *geo) override;
  int visit(ObCartesianMultilinestring *geo) override;
  int visit(ObCartesianMultipolygon *geo) override;
  int visit(ObCartesianGeometrycollection *geo)
  {
    UNUSED(geo);
    return OB_SUCCESS;
  }

  bool is_end(ObCartesianLineString *geo) override
  {
    UNUSED(geo);
    return true;
  }
  bool is_end(ObCartesianPolygon *geo) override
  {
    UNUSED(geo);
    return true;
  }
  bool is_end(ObCartesianMultipoint *geo) override
  {
    UNUSED(geo);
    return true;
  }
  bool is_end(ObCartesianMultilinestring *geo) override
  {
    UNUSED(geo);
    return true;
  }
  bool is_end(ObCartesianMultipolygon *geo) override
  {
    UNUSED(geo);
    return true;
  }

  int finish(ObGeometry *geo) override
  {
    UNUSED(geo);
    return OB_SUCCESS;
  }

  int get_geometry(ObGeometry *&geo);

private:
  ObBoxPosition get_position(double x, double y);
  int line_visit(
      const ObCartesianLineString &line, ObCartesianMultilinestring *&mls, bool &completely_inside);

  int visit_polygon(ObCartesianPolygon &poly, ObCartesianMultipolygon *&mpy);
  ObGeoType get_result_basic_type();

  double xmin_;
  double ymin_;
  double xmax_;
  double ymax_;
  ObCartesianGeometrycollection *res_geo_;
  ObIAllocator *allocator_;
  lib::MemoryContext *mem_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoBoxClipVisitor);
};

}  // namespace common
}  // namespace oceanbase
#endif
