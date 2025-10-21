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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_LATLONG_CHECK_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_LATLONG_CHECK_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"

namespace oceanbase
{
namespace common
{

class ObGeoLatlongCheckVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoLatlongCheckVisitor(const ObSrsItem *srs)
    : srs_(srs),
      changed_(false) {}
  virtual ~ObGeoLatlongCheckVisitor() {}
  bool has_changed() { return changed_; }
  bool prepare(ObGeometry *geo) override;
  int visit(ObIWkbGeogPoint *geo) override;
  int visit(ObGeometry *geo) override { UNUSED(geo); return OB_SUCCESS; } // all cartesian geometry is in range
  bool is_end(ObGeometry *geo) override { UNUSED(geo); return OB_SUCCESS; }
  int visit(ObGeographPoint *geo) override;

  template<typename Geo_type>
  int calculate_point_range(Geo_type *geo);

  static double ob_normalize_latitude(double lat);
  static double ob_normalize_longitude(double lon);
private:
  const ObSrsItem *srs_;
  bool changed_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoLatlongCheckVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
