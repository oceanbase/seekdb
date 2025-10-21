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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_LONGTITUDE_CORRECT_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_LONGTITUDE_CORRECT_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"

namespace oceanbase
{


namespace common
{

class ObGeoLongtitudeCorrectVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoLongtitudeCorrectVisitor(const ObSrsItem *srs)
    : srs_(srs) {}
  virtual ~ObGeoLongtitudeCorrectVisitor() {}
  bool prepare(ObGeometry *geo) { UNUSED(geo); return true; }

  int visit(ObGeographLineString *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObGeographLinearring *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObGeographPolygon *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObGeometrycollection *geo) { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObGeographPoint *geo);
private:
  const ObSrsItem *srs_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoLongtitudeCorrectVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
