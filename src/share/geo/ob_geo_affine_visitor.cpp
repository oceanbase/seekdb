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
#include "ob_geo_affine_visitor.h"
#include "seekdb/geo/tile_grid.hpp"

namespace oceanbase
{
namespace common
{

bool ObGeoAffineVisitor::prepare(ObGeometry *geo)
{
  bool bret = true;
  if ((OB_ISNULL(geo) || OB_ISNULL(affine_))) {
    bret = false;
  }
  return bret;
}

template<typename PtType>
void ObGeoAffineVisitor::affine(PtType *point)
{
  double x = point->x();
  double y = point->y();
  seekdb::geo::cartesian::affine_xy(x, y, affine_->x_fac1, affine_->y_fac1,
      affine_->x_fac2, affine_->y_fac2, affine_->x_off, affine_->y_off);
  point->x(x);
  point->y(y);
}

int ObGeoAffineVisitor::visit(ObGeographPoint *geo)
{
  affine(geo);
  return OB_SUCCESS;
}

// for st_transform
int ObGeoAffineVisitor::visit(ObCartesianPoint *geo)
{
  affine(geo);
  return OB_SUCCESS;
}
}  // namespace common
}  // namespace oceanbase
