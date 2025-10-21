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
#include "ob_geo_denormalize_visitor.h"

namespace oceanbase {
namespace common {

int ObGeoDeNormalizeVisitor::denormalize(ObCartesianPoint *point)
{
  INIT_SUCC(ret);
  double nx = 1.0;
  double ny = 1.0;

  nx = point->x() * 180.0 / M_PI;
  ny = point->y() * 180.0 / M_PI;

  point->set<0>(nx);
  point->set<1>(ny);

  return ret;
}

int ObGeoDeNormalizeVisitor::visit(ObCartesianPoint *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(denormalize(geo))){
    LOG_WARN("failed to denormalize cartesian point", K(ret));
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
