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
#ifndef OCEANBASE_LIB_GEO_OB_GEO_DENORMALIZE_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_DENORMALIZE_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"
namespace oceanbase
{
namespace common
{

// NOTE: for st_transform only, which need denormalize cartesian tree geometry
//       the denormalize action for other gis expr is within wkb visitor
class ObGeoDeNormalizeVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoDeNormalizeVisitor() {}
  virtual ~ObGeoDeNormalizeVisitor() {}
  bool prepare(ObGeometry *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianPoint *geo);
  int visit(ObCartesianMultipoint *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianLineString *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianLinearring *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianMultilinestring *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianPolygon *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianMultipolygon *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObCartesianGeometrycollection *geo) { UNUSED(geo); return OB_SUCCESS; }

private:
  int denormalize(ObCartesianPoint *point);

private:
  DISALLOW_COPY_AND_ASSIGN(ObGeoDeNormalizeVisitor);
};

} // namespace common
} // namespace oceanbase

#endif //OCEANBASE_LIB_GEO_OB_GEO_DENORMALIZE_VISITOR_
