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
#include "lib/geo/ob_geo_cache_point.h"

namespace oceanbase
{
namespace common
{

int ObCachedGeoPoint::intersects(ObGeometry& geo, ObGeoEvalCtx& gis_context, bool &res)
{
  int ret = OB_SUCCESS;
  bool is_intersects = false;
  if (!is_inited() && OB_FAIL(init())) {
    LOG_WARN("cached polygon init failed", K(ret));
  } else if (OB_FAIL(ObCachedGeomBase::check_any_vertexes_in_geo(geo, res))) {
    LOG_WARN("fail to check whether is there any point from cached poly in geo.", K(ret));
  }

  return ret;
}

} // namespace common
} // namespace oceanbase
