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
#include "ob_geo_close_ring_visitor.h"


namespace oceanbase {
namespace common {

template<typename PolyTree>
int ObGeoCloseRingVisitor::visit_poly(PolyTree *geo)
{
  int ret = OB_SUCCESS;
  if (geo->front() != geo->back()) {
    if (OB_FAIL(geo->push_back(geo->front()))) {
      LOG_WARN("fail to push back point", K(ret));
    }
  }
  if (OB_SUCC(ret) && geo->size() < 4) {
    ret = OB_ERR_GIS_INVALID_DATA;
    LOG_WARN("invalid geometry polygon", K(ret), K(geo->size()));
  }
  return ret;
}

int ObGeoCloseRingVisitor::visit(ObGeographLinearring *geo)
{
  return visit_poly(geo);
}

int ObGeoCloseRingVisitor::visit(ObCartesianLinearring *geo)
{
  return visit_poly(geo);
}

} // namespace common
} // namespace oceanbase
