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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_CLOSE_RING_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_CLOSE_RING_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"

namespace oceanbase
{
namespace common
{

class ObGeoCloseRingVisitor : public ObEmptyGeoVisitor
{
public:
  // need_convert: Configure whether conversion is required when srs type is GEOGRAPHIC_SRS.
  explicit ObGeoCloseRingVisitor() {}
  virtual ~ObGeoCloseRingVisitor() {}
  bool prepare(ObGeometry *geo) override { return geo != nullptr; }

  // tree
  int visit(ObPolygon *geo) { UNUSED(geo); return OB_SUCCESS; }

  int visit(ObGeometrycollection *geo) { UNUSED(geo); return OB_SUCCESS; }

  int visit(ObGeographLinearring *geo) override;
  int visit(ObCartesianLinearring *geo) override;

  int visit(ObMultipolygon *geo) { UNUSED(geo); return OB_SUCCESS; }

  bool is_end(ObLinearring *geo) override { UNUSED(geo); return true; }

private:
  template<typename PolyTree>
  int visit_poly(PolyTree *geo);
  DISALLOW_COPY_AND_ASSIGN(ObGeoCloseRingVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
