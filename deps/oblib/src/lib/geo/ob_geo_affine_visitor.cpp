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
  point->x(affine_->x_fac1 * x + affine_->y_fac1 * y + affine_->x_off);
  point->y(affine_->x_fac2 * x + affine_->y_fac2 * y + affine_->y_off);
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
