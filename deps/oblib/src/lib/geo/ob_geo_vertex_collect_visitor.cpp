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
#include "ob_geo_vertex_collect_visitor.h"

namespace oceanbase {
namespace common {

bool ObGeoVertexCollectVisitor::prepare(ObGeometry *geo)
{
  bool bret = true;
  if (OB_ISNULL(geo)) {
    bret = false;
  }
  return bret;
}

int ObGeoVertexCollectVisitor::visit(ObIWkbPoint *geo)
{
  int ret = OB_SUCCESS;
  ObPoint2d vertex;
  vertex.x = geo->x();
  vertex.y = geo->y();
  if (OB_FAIL(vertexes_.push_back(vertex))) {
    LOG_WARN("failed to add vertex to cached geo", K(ret));
  } else {
    if (std::isnan(x_min_)) {
      x_min_ = vertex.x;
    } else {
      x_min_ = std::min(x_min_, vertex.x);
    }
    if (std::isnan(x_max_)) {
      x_max_ = vertex.x;
    } else {
      x_max_ = std::max(x_max_, vertex.x);
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
