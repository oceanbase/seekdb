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

#define USING_LOG_PREFIX LIB
#include "ob_geo_zoom_in_visitor.h"

namespace oceanbase {
namespace common {

template<typename T_Point>
int ObGeoZoomInVisitor::zoom_in_point(T_Point *geo)
{
  if (!is_calc_zoom_) {
    uint32_t count = 0;
    while (count++ < zoom_in_value_) {
      double longti = geo->x();
      double lati = geo->y();
      longti *= 10;
      lati *= 10;
      geo->x(longti);
      geo->y(lati); 
    }
  } else {
    uint32_t count = 0;
    double nx_tmp = geo->x();
    double ny_tmp = geo->y();
    while (nx_tmp != 0.0 && std::fabs(nx_tmp) < ZOOM_IN_THRESHOLD) {
      nx_tmp *= 10;
      count++;
    }
    zoom_in_value_ = count > zoom_in_value_ ? count : zoom_in_value_;
    count = 0;
    while (ny_tmp != 0.0 && std::fabs(ny_tmp) < ZOOM_IN_THRESHOLD) {
      ny_tmp *= 10;
      count++;
    }
    zoom_in_value_ = count > zoom_in_value_ ? count : zoom_in_value_;
  }
  return OB_SUCCESS;
}

int ObGeoZoomInVisitor::visit(ObGeographPoint *geo)
{
  return zoom_in_point(geo);
}

int ObGeoZoomInVisitor::visit(ObIWkbGeogPoint *geo)
{
  return zoom_in_point(geo);
}

int ObGeoZoomInVisitor::visit(ObCartesianPoint *geo)
{
  return zoom_in_point(geo);
}

int ObGeoZoomInVisitor::visit(ObIWkbGeomPoint *geo)
{
  return zoom_in_point(geo);
}
} // namespace common
} // namespace oceanbase
