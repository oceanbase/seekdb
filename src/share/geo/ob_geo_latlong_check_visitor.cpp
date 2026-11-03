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

#define USING_LOG_PREFIX SQL
#include "ob_geo_latlong_check_visitor.h"
#include "seekdb/geo/pg_coordinate_io.hpp"

namespace oceanbase {
namespace common {

double ObGeoLatlongCheckVisitor::ob_normalize_latitude(double lat)
{
  return seekdb::geo::pg::normalize_latitude(lat);
}

double ObGeoLatlongCheckVisitor::ob_normalize_longitude(double lon)
{
  return seekdb::geo::pg::normalize_longitude(lon);
}

bool ObGeoLatlongCheckVisitor::prepare(ObGeometry *geo)
{
  UNUSED(geo);
  int res = true;
  if (srs_ == NULL || srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    res = false;
  }
  return res;
}

template<typename Geo_type>
int ObGeoLatlongCheckVisitor::calculate_point_range(Geo_type *geo)
{
  double longti = geo->x();
  double lati = geo->y();
  if (longti < -180.0 || longti > 180.0 || 
      lati < -90.0 || lati > 90.0 ) {
    longti = ob_normalize_longitude(longti);
    lati = ob_normalize_latitude(lati);
    geo->x(longti);
    geo->y(lati);
    changed_ = true;
  }
  return OB_SUCCESS;
}

int ObGeoLatlongCheckVisitor::visit(ObIWkbGeogPoint *geo)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(srs_)) {
    ret = OB_ERR_NULL_VALUE;
  } else if (srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is projected type", K(srs_));
  } else if (OB_FAIL(calculate_point_range(geo))){
  }
  return ret;
}

int ObGeoLatlongCheckVisitor::visit(ObGeographPoint *geo)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(srs_)) {
    ret = OB_ERR_NULL_VALUE;
  } else if (srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is projected type", K(srs_));
  } else if (OB_FAIL(calculate_point_range(geo))) {
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
