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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_VERTEX_COLLECT_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_VERTEX_COLLECT_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_geo_utils.h"

namespace oceanbase
{
namespace common
{

class ObGeoVertexCollectVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoVertexCollectVisitor(ObVertexes &vertexes) : vertexes_(vertexes), x_min_(NAN), x_max_(NAN) {}
  virtual ~ObGeoVertexCollectVisitor() {}
  bool prepare(ObGeometry *geo);  
  int visit(ObIWkbPoint *geo);
  int visit(ObIWkbGeometry *geo) { UNUSED(geo); return OB_SUCCESS; }
  inline double get_x_min() { return x_min_; }
  inline double get_x_max() { return x_max_; }

private:
  ObVertexes &vertexes_;
  double x_min_;
  double x_max_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoVertexCollectVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
