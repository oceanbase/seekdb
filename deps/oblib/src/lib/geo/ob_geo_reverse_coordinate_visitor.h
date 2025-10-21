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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_REVERSE_COORDINATE_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_REVERSE_COORDINATE_VISITOR_
#include "lib/geo/ob_geo_visitor.h"

namespace oceanbase
{


namespace common
{

class ObGeoReverseCoordinateVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoReverseCoordinateVisitor() {}
  virtual ~ObGeoReverseCoordinateVisitor() {}
  bool prepare(ObGeometry *geo);  
  int visit(ObIWkbGeogPoint *geo);
  int visit(ObIWkbGeogLineString *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObIWkbGeogLinearRing *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObIWkbGeogPolygon *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObIWkbGeogMultiPoint *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObIWkbGeogMultiLineString *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObIWkbGeogMultiPolygon *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObIWkbGeogCollection *geo) { UNUSED(geo); return OB_SUCCESS; }

private:
  int reverse_point_coordinate(ObIWkbGeogPoint *geo);

private:
  DISALLOW_COPY_AND_ASSIGN(ObGeoReverseCoordinateVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
