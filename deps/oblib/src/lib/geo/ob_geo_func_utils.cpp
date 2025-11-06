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

#include "lib/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

int ObGeoFuncUtils::apply_bg_to_tree(const ObGeometry *g1, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  ObGeoToTreeVisitor geom_visitor(context.get_allocator());
  ObGeometry *geo1 = const_cast<ObGeometry *>(g1);
  if (OB_FAIL(geo1->do_visit(geom_visitor))) {
    LOG_WARN("failed to convert bin to tree", K(ret));
  } else {
    result = geom_visitor.get_geometry();
  }
  return ret;
}

} // sql
} // oceanbase
